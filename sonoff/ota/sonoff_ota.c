/**
 * @file    sonoff_ota.c
 * @brief   OTA升级任务与镜像管理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-27
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <semphr.h>

#include "sonoff_log.h"
#include "sonoff_ota.h"
#include "sonoff_ota_adapter.h"
#include "sonoff_ota_parse.h"
#include "sonoff_task_def.h"

static const char *tag = "SNF-OTA";

#define SNF_OTA_EVENT_QUEUE_LENGTH          (10)       /* OTA事件队列长度 */
#define SNF_OTA_DATA_TIMEOUT_MS             (20000)    /* 单次等待镜像数据超时时间 */
#define SNF_OTA_MAX_TIMEOUT_COUNT           (3)        /* 最大连续超时次数 */
#define SNF_OTA_ERASE_SECTOR_SIZE           (4 * 1024) /* OTA擦除sector大小 */
#define SNF_OTA_ERASE_YIELD_MS              (10)       /* 擦除后让出调度, 避免WiFi丢beacon */
#define SNF_OTA_WRITE_YIELD_MS              (1)        /* 每段写入后让出调度, 便于TCP应答 */
#define SNF_OTA_REBOOT_DELAY_MS             (2000)     /* 应用成功后的重启延时 */

/** @brief OTA任务内部事件. */
typedef enum
{
    SNF_OTA_EVT_IMAGE_DATA = 0,
    SNF_OTA_EVT_IMAGE_APPLY,
    SNF_OTA_EVT_ABORT,
} SnfOtaEventId;

/** @brief OTA任务内部事件数据. */
typedef struct
{
    SnfOtaEventId id;
    uint32_t offset;
    const uint8_t *data;
    uint32_t size;
} SnfOtaEvent;

/** @brief OTA任务运行控制块. */
typedef struct
{
    TaskHandle_t task_handle;
    QueueHandle_t event_queue;
    SemaphoreHandle_t mutex;
    SnfOtaConfig config;
    SnfOtaParseContext parser;
} SnfOtaControl;

/** @brief OTA任务运行信息. */
typedef struct
{
    volatile SnfOtaState state;
    volatile uint32_t total_size;        /* 镜像总长度. */
    volatile uint32_t received_size;     /* 已接收数据长度. */
    volatile uint32_t queued_size;       /* 已入队列数据长度. */
    volatile uint32_t next_erase_offset; /* 下次擦除偏移量. */
    volatile uint8_t timeout_count;      /* 超时计数. */
} SnfOtaInfo;

/** @brief OTA任务运行控制块实例. */
static SnfOtaControl ota_control = {
    .task_handle = NULL,
    .event_queue = NULL,
    .mutex = NULL,
    .config = {{0}},
};

static SnfOtaInfo ota_info = {
    .state = SNF_OTA_STATE_IDLE,
    .total_size = 0,
    .received_size = 0,
    .queued_size = 0,
    .next_erase_offset = 0,
    .timeout_count = 0,
};

/**
 * @brief 获取OTA任务锁.
 *
 * @return SNF_OTA_OK表示成功, 其他值表示失败.
 */
static int otaLock(void)
{
    SnfOtaControl *control = &ota_control;

    if (control->mutex == NULL)
    {
        LOG_I(tag, "mutex is not initialized");
        return SNF_OTA_ERR_STATE;
    }

    if (xSemaphoreTake(control->mutex, portMAX_DELAY) != pdPASS)
    {
        LOG_I(tag, "failed to lock OTA");
        return SNF_OTA_ERR_BUSY;
    }

    return SNF_OTA_OK;
}

/** @brief 释放OTA任务锁. */
static void otaUnlock(void)
{
    SnfOtaControl *control = &ota_control;

    if (control->mutex == NULL)
    {
        LOG_I(tag, "mutex is not initialized");
        return;
    }

    if (xSemaphoreGive(control->mutex) != pdPASS)
    {
        LOG_I(tag, "failed to unlock OTA");
    }
}

/**
 * @brief 向OTA事件队列发送事件.
 *
 * 调用前必须已持有OTA任务锁.
 *
 * @param [in] event - 待发送事件.
 * @return SNF_OTA_OK表示成功, 其他值表示失败.
 */
static int otaQueueEventSend(const SnfOtaEvent *event)
{
    SnfOtaControl *control = &ota_control;

    if (event == NULL)
    {
        LOG_I(tag, "event is NULL");
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    if (control->event_queue == NULL)
    {
        LOG_I(tag, "event queue is not initialized");
        return SNF_OTA_ERR_STATE;
    }

    if (xQueueSend(control->event_queue, event, 0) != pdPASS)
    {
        LOG_I(tag, "event queue is full");
        return SNF_OTA_ERR_QUEUE_FULL;
    }

    return SNF_OTA_OK;
}

/**
 * @brief 在已持有任务锁时清除本次OTA会话.
 *
 * 队列在任务退出时删除，本函数只复位句柄、配置和运行信息.
 */
static void otaClearSessionLocked(void)
{
    SnfOtaControl *control = &ota_control;
    SnfOtaInfo *info = &ota_info;

    memset(&control->config, 0, sizeof(SnfOtaConfig));
    snfOtaParseFree(&control->parser);
    memset(info, 0, sizeof(SnfOtaInfo));
    info->state = SNF_OTA_STATE_IDLE;
    control->task_handle = NULL;
}

/**
 * @brief 根据已接收数据计算进度百分比.
 *
 * @param [in] received_size - 已接收数据长度.
 * @param [in] total_size - 镜像总长度.
 * @return 进度百分比.
 */
static uint8_t otaCalculatePercent(uint32_t received_size, uint32_t total_size)
{
    uint64_t percent_value;

    if (total_size == 0)
    {
        return 0;
    }

    percent_value = ((uint64_t)received_size * 100) / total_size;
    if (percent_value > 100)
    {
        return 100;
    }

    return (uint8_t)percent_value;
}

/**
 * @brief 通知OTA状态变化.
 *
 * @param [in] state - 新状态.
 * @param [in] error_code - 事件错误码.
 */
static void otaNotify(SnfOtaState state, SnfOtaErrorCode error_code)
{
    SnfOtaInfo *info = &ota_info;
    SnfOtaControl *control = &ota_control;
    SnfOtaEventCallback callback;
    SnfOtaEventData event_data = {0};

    info->state = state;
    event_data.received_size = info->received_size;
    event_data.total_size = info->total_size;
    event_data.percent = otaCalculatePercent(event_data.received_size,
                                             event_data.total_size);
    event_data.error_code = (uint8_t)error_code;
    callback = control->config.event_callback;

    if (callback != NULL)
    {
        callback(state, &event_data);
    }
}

/**
 * @brief 根据镜像信息执行镜像完整性校验.
 *
 * @param [in] image_info - 镜像信息.
 * @return OTA错误码, SNF_OTA_ERROR_NONE表示成功.
 */
static SnfOtaErrorCode otaVerifyImage(const SnfOtaImageInfo *image_info)
{
    if (image_info == NULL)
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    if (image_info->check_type == SNF_OTA_CHECK_NONE)
    {
        return SNF_OTA_ERROR_NONE;
    }

    return SNF_OTA_ERROR_UNSUPPORTED;
}

/**
 * @brief 擦除写入范围覆盖的OTA sector.
 *
 * @param [in] offset - 写入数据在镜像中的偏移量.
 * @param [in] size - 写入数据长度.
 * @return OTA错误码, SNF_OTA_ERROR_NONE表示成功.
 */
static SnfOtaErrorCode otaEraseBeforeWrite(uint32_t offset, uint32_t size)
{
    SnfOtaInfo *info = &ota_info;
    uint32_t write_end;

    if (size > (UINT32_MAX - offset))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    write_end = offset + size;
    while (write_end > info->next_erase_offset)
    {
        uint32_t erase_offset = info->next_erase_offset;

        if (erase_offset > (UINT32_MAX - SNF_OTA_ERASE_SECTOR_SIZE))
        {
            return SNF_OTA_ERROR_ERASE_FAILED;
        }

        if (snfOtaAdapterErase(erase_offset, SNF_OTA_ERASE_SECTOR_SIZE) != 0)
        {
            return SNF_OTA_ERROR_ERASE_FAILED;
        }

        vTaskDelay(pdMS_TO_TICKS(SNF_OTA_ERASE_YIELD_MS));
        info->next_erase_offset = erase_offset + SNF_OTA_ERASE_SECTOR_SIZE;
    }

    return SNF_OTA_ERROR_NONE;
}

/**
 * @brief 擦除必要sector并写入一段OTA数据.
 *
 * @param [in] event - 镜像数据事件.
 * @return OTA错误码，区分擦除失败和写入失败.
 */
static SnfOtaErrorCode otaEraseAndWrite(const SnfOtaEvent *event)
{
    SnfOtaErrorCode error_code;

    error_code = otaEraseBeforeWrite(event->offset, event->size);
    if (error_code != SNF_OTA_ERROR_NONE)
    {
        LOG_I(tag, "erase failed");
        return error_code;
    }

    if (snfOtaAdapterWrite(event->offset, event->data, event->size) != 0)
    {
        LOG_I(tag, "write failed");
        return SNF_OTA_ERROR_WRITE_FAILED;
    }

    return SNF_OTA_ERROR_NONE;
}

/**
 * @brief 解析升级包并将有效载荷送入现有擦写流程
 *
 * @param [in] event - 包含公司外层包头的输入数据事件.
 * @return OTA错误码.
 */
static SnfOtaErrorCode otaHandlePackage(const SnfOtaEvent *event)
{
    SnfOtaControl *control = &ota_control;
    uint32_t offset = 0;
    uint32_t consumed;
    SnfOtaPayload payload = {0};
    SnfOtaEvent image_event = {0};
    SnfOtaErrorCode error;

    while (offset < event->size)
    {
        error = snfOtaParseData(&control->parser, &event->data[offset], event->size - offset,
                                &consumed, &payload);
        if (error != SNF_OTA_ERROR_NONE)
        {
            LOG_I(tag, "OTA parse failed: offset=%lu, error=%u",
                  (unsigned long)control->parser.offset, (unsigned int)error);
            return error;
        }

        if (payload.size != 0)
        {
            image_event.offset = payload.offset;
            image_event.data = payload.data;
            image_event.size = payload.size;
            error = otaEraseAndWrite(&image_event);
            if (error != SNF_OTA_ERROR_NONE)
            {
                return error;
            }
        }

        offset += consumed;
    }

    return SNF_OTA_ERROR_NONE;
}

/**
 * @brief 处理OTA镜像校验.
 *
 * @return 0表示继续处理事件, -1表示结束OTA任务.
 */
static int otaHandleVerify(void)
{
    SnfOtaControl *control = &ota_control;
    SnfOtaInfo *info = &ota_info;
    SnfOtaErrorCode error_code;

    if (info->received_size != info->total_size)
    {
        LOG_I(tag, "image incomplete");
        otaNotify(SNF_OTA_STATE_VERIFY_FAILED, SNF_OTA_ERROR_IMAGE_INCOMPLETE);
        return -1;
    }

    if (control->config.format == SNF_OTA_FORMAT_PACKAGE)
    {
        error_code = snfOtaParseFinish(&control->parser);
    }
    else
    {
        error_code = otaVerifyImage(&control->config.image_info);
    }

    if (error_code != SNF_OTA_ERROR_NONE)
    {
        otaNotify(SNF_OTA_STATE_VERIFY_FAILED, error_code);
        return -1;
    }

    otaNotify(SNF_OTA_STATE_VERIFY_SUCCESS, SNF_OTA_ERROR_NONE);

    return 0;
}

/**
 * @brief 处理OTA镜像数据事件.
 *
 * @param [in] event - 镜像数据事件.
 * @return 0表示继续处理事件, -1表示结束OTA任务.
 */
static int otaHandleData(const SnfOtaEvent *event)
{
    SnfOtaInfo *info = &ota_info;
    SnfOtaControl *control = &ota_control;
    uint32_t image_size = info->total_size;
    SnfOtaErrorCode error_code;

    if (info->state != SNF_OTA_STATE_RECEIVING)
    {
        LOG_I(tag, "invalid state for OTA data: %d", info->state);
        otaNotify(SNF_OTA_STATE_FAILED, SNF_OTA_ERROR_INVALID_PARAM);
        return -1;
    }

    if ((event == NULL) || (event->data == NULL) || (event->size == 0))
    {
        LOG_I(tag, "invalid OTA data event");
        otaNotify(SNF_OTA_STATE_FAILED, SNF_OTA_ERROR_INVALID_PARAM);
        return -1;
    }

    if (event->offset != info->received_size)
    {
        LOG_I(tag,
              "invalid OTA offset: offset=%lu, received=%lu",
              (unsigned long)event->offset,
              (unsigned long)info->received_size);
        otaNotify(SNF_OTA_STATE_FAILED, SNF_OTA_ERROR_INVALID_PARAM);
        return -1;
    }

    if ((event->offset > image_size) || (event->size > (image_size - event->offset)))
    {
        LOG_I(tag,
              "OTA data exceeds image: offset=%lu, size=%lu, total=%lu",
              (unsigned long)event->offset,
              (unsigned long)event->size,
              (unsigned long)image_size);
        otaNotify(SNF_OTA_STATE_FAILED, SNF_OTA_ERROR_INVALID_PARAM);
        return -1;
    }

    if (control->config.format == SNF_OTA_FORMAT_PACKAGE)
    {
        error_code = otaHandlePackage(event);
    }
    else
    {
        error_code = otaEraseAndWrite(event);
    }

    if (error_code != SNF_OTA_ERROR_NONE)
    {
        LOG_I(tag, "write and check failed");
        otaNotify(SNF_OTA_STATE_FAILED, error_code);
        return -1;
    }

    info->received_size += event->size;
    if (info->received_size < image_size)
    {
        otaNotify(SNF_OTA_STATE_RECEIVING, SNF_OTA_ERROR_NONE);
        vTaskDelay(pdMS_TO_TICKS(SNF_OTA_WRITE_YIELD_MS));
        return 0;
    }

    return otaHandleVerify();
}

/**
 * @brief 处理OTA应用事件.
 *
 * @return 重启接口返回时, 0表示应用成功, -1表示应用失败并结束OTA任务.
 */
static int otaHandleApply(void)
{
    otaNotify(SNF_OTA_STATE_APPLY, SNF_OTA_ERROR_NONE);
    if (snfOtaAdapterSetBootPartition() != 0)
    {
        otaNotify(SNF_OTA_STATE_FAILED, SNF_OTA_ERROR_APPLY_FAILED);
        return -1;
    }

    otaNotify(SNF_OTA_STATE_SUCCESS, SNF_OTA_ERROR_NONE);
    vTaskDelay(pdMS_TO_TICKS(SNF_OTA_REBOOT_DELAY_MS));
    snfOtaAdapterReboot();

    return 0;
}

/**
 * @brief 处理OTA中止事件.
 *
 * @return -1表示结束OTA任务.
 */
static int otaHandleAbort(void)
{
    otaNotify(SNF_OTA_STATE_ABORT, SNF_OTA_ERROR_ABORTED);

    return -1;
}

/**
 * @brief 处理镜像接收超时.
 *
 * @return 0表示继续等待, -1表示结束OTA任务.
 */
static int otaHandleTimeout(void)
{
    SnfOtaInfo *info = &ota_info;

    if (info->state != SNF_OTA_STATE_RECEIVING)
    {
        LOG_I(tag, "OTA is not receiving, state=%d", info->state);
        return 0;
    }

    info->timeout_count++;
    if (info->timeout_count < SNF_OTA_MAX_TIMEOUT_COUNT)
    {
        LOG_W(tag,
              "wait OTA data timeout: %u/%u",
              (unsigned int)info->timeout_count,
              (unsigned int)SNF_OTA_MAX_TIMEOUT_COUNT);
        return 0;
    }

    LOG_I(tag, "receive OTA image timeout");
    otaNotify(SNF_OTA_STATE_FAILED, SNF_OTA_ERROR_TIMEOUT);

    return -1;
}

/**
 * @brief OTA任务主体.
 *
 * @param [in] arg - OTA任务控制块.
 */
static void otaTask(void *arg)
{
    SnfOtaControl *control = &ota_control;
    SnfOtaInfo *info = &ota_info;
    SnfOtaEvent event = {0};
    int ret = 0;

    while (ret == 0)
    {
        if (xQueueReceive(control->event_queue, &event, pdMS_TO_TICKS(SNF_OTA_DATA_TIMEOUT_MS)) != pdPASS)
        {
            ret = otaHandleTimeout();
        }
        else
        {
            switch (event.id)
            {
                case SNF_OTA_EVT_IMAGE_DATA:
                    ret = otaHandleData(&event);
                    break;
                case SNF_OTA_EVT_IMAGE_APPLY:
                    ret = otaHandleApply();
                    break;
                case SNF_OTA_EVT_ABORT:
                    ret = otaHandleAbort();
                    break;
                default:
                    LOG_I(tag, "unknown OTA event id=%d", event.id);
                    otaNotify(SNF_OTA_STATE_FAILED, SNF_OTA_ERROR_UNSUPPORTED);
                    ret = -1;
                    break;
            }

            info->timeout_count = 0;
        }
    }

    if (otaLock() == SNF_OTA_OK)
    {
        if (control->event_queue != NULL)
        {
            vQueueDelete(control->event_queue);
            control->event_queue = NULL;
        }

        otaClearSessionLocked();
        otaUnlock();
    }

    vTaskDelete(NULL);
}

int snfOtaStateIsFailed(SnfOtaState state)
{
    if ((state == SNF_OTA_STATE_FAILED)
        || (state == SNF_OTA_STATE_VERIFY_FAILED)
        || (state == SNF_OTA_STATE_ABORT))
    {
        return -1;
    }

    return 0;
}

uint8_t snfOtaStateIsActive(SnfOtaState state)
{
    if ((state == SNF_OTA_STATE_RECEIVING)
        || (state == SNF_OTA_STATE_VERIFY_SUCCESS)
        || (state == SNF_OTA_STATE_APPLY)
        || (state == SNF_OTA_STATE_SUCCESS))
    {
        return 1;
    }

    return 0;
}

int snfOtaStart(const SnfOtaConfig *ota_config)
{
    SnfOtaControl *control = &ota_control;
    SnfOtaInfo *info = &ota_info;
    uint32_t flash_size;
    int ret;

    if ((ota_config == NULL) || (ota_config->image_info.size == 0))
    {
        LOG_I(tag, "invalid OTA config");
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    if (ota_config->image_info.cipher_type != SNF_OTA_CIPHER_NONE)
    {
        LOG_I(tag, "unsupported OTA cipher type: %u", ota_config->image_info.cipher_type);
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    if ((ota_config->format != SNF_OTA_FORMAT_RAW) && (ota_config->format != SNF_OTA_FORMAT_PACKAGE))
    {
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    if ((ota_config->format == SNF_OTA_FORMAT_PACKAGE)
        && (ota_config->image_info.check_type != SNF_OTA_CHECK_NONE))
    {
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    if (control->mutex == NULL)
    {
        control->mutex = xSemaphoreCreateMutex();
        if (control->mutex == NULL)
        {
            LOG_I(tag, "mutex init failed");
            return SNF_OTA_ERR_NO_MEMORY;
        }
    }

    ret = otaLock();
    if (ret != SNF_OTA_OK)
    {
        return ret;
    }

    if (control->task_handle != NULL)
    {
        LOG_I(tag, "OTA is already started");
        otaUnlock();
        return SNF_OTA_ERR_BUSY;
    }

    if (snfOtaAdapterGetSize(&flash_size) != 0)
    {
        otaUnlock();
        return SNF_OTA_ERR_STATE;
    }

    if ((ota_config->format == SNF_OTA_FORMAT_RAW) && (ota_config->image_info.size > flash_size))
    {
        otaUnlock();
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    if (ota_config->format == SNF_OTA_FORMAT_PACKAGE)
    {
        if (snfOtaParseInit(&control->parser, ota_config->image_info.size, ota_config->file_name,
                            flash_size) != SNF_OTA_ERROR_NONE)
        {
            otaUnlock();
            return SNF_OTA_ERR_INVALID_PARAM;
        }
    }

    if (control->event_queue == NULL)
    {
        control->event_queue = xQueueCreate(SNF_OTA_EVENT_QUEUE_LENGTH,
                                            sizeof(SnfOtaEvent));
        if (control->event_queue == NULL)
        {
            LOG_I(tag, "event queue init failed");
            otaUnlock();
            return SNF_OTA_ERR_NO_MEMORY;
        }
    }

    memcpy(&control->config, ota_config, sizeof(SnfOtaConfig));
    memset(info, 0, sizeof(SnfOtaInfo));
    info->total_size = ota_config->image_info.size;
    info->state = SNF_OTA_STATE_RECEIVING;

    if (xTaskCreate(otaTask,
                    SONOFF_OTA_TASK_NAME,
                    SONOFF_OTA_TASK_STACKSIZE,
                    NULL,
                    SONOFF_OTA_TASK_PRIO,
                    &control->task_handle) != pdPASS)
    {
        LOG_I(tag, "task create failed");
        vQueueDelete(control->event_queue);
        control->event_queue = NULL;
        otaClearSessionLocked();
        otaUnlock();
        return SNF_OTA_ERR_NO_MEMORY;
    }

    otaUnlock();

    return SNF_OTA_OK;
}

int snfOtaWrite(uint32_t offset, const uint8_t *data, uint32_t len)
{
    SnfOtaInfo *info = &ota_info;
    SnfOtaControl *control = &ota_control;
    SnfOtaEvent event = {0};
    uint32_t image_size;
    int ret;

    if ((data == NULL) || (len == 0))
    {
        LOG_I(tag, "invalid OTA data or length");
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    ret = otaLock();
    if (ret != SNF_OTA_OK)
    {
        return ret;
    }

    if (control->task_handle == NULL)
    {
        LOG_I(tag, "OTA is not started");
        otaUnlock();
        return SNF_OTA_ERR_STATE;
    }

    if (info->state != SNF_OTA_STATE_RECEIVING)
    {
        LOG_I(tag, "OTA is not receiving, state=%d", info->state);
        otaUnlock();
        return SNF_OTA_ERR_STATE;
    }

    image_size = info->total_size;
    if ((offset != info->queued_size)
        || (offset > image_size)
        || (len > (image_size - offset)))
    {
        LOG_I(tag,
              "invalid OTA range: offset=%lu, queued=%lu, len=%lu, total=%lu",
              (unsigned long)offset,
              (unsigned long)info->queued_size,
              (unsigned long)len,
              (unsigned long)image_size);
        otaUnlock();
        return SNF_OTA_ERR_INVALID_PARAM;
    }

    event.id = SNF_OTA_EVT_IMAGE_DATA;
    event.offset = offset;
    event.data = data;
    event.size = len;
    ret = otaQueueEventSend(&event);
    if (ret == SNF_OTA_OK)
    {
        info->queued_size += len;
    }

    otaUnlock();

    return ret;
}

int snfOtaApply(void)
{
    SnfOtaInfo *info = &ota_info;
    SnfOtaControl *control = &ota_control;
    SnfOtaEvent event = {0};
    int ret;

    ret = otaLock();
    if (ret != SNF_OTA_OK)
    {
        return ret;
    }

    if (control->task_handle == NULL)
    {
        LOG_I(tag, "OTA is not started");
        otaUnlock();
        return SNF_OTA_ERR_STATE;
    }

    if (info->state != SNF_OTA_STATE_VERIFY_SUCCESS)
    {
        LOG_I(tag, "OTA image is not verified, state=%d", info->state);
        otaUnlock();
        return SNF_OTA_ERR_STATE;
    }

    event.id = SNF_OTA_EVT_IMAGE_APPLY;
    ret = otaQueueEventSend(&event);
    otaUnlock();

    return ret;
}

int snfOtaAbort(void)
{
    SnfOtaControl *control = &ota_control;
    SnfOtaEvent event = {0};
    int ret;

    ret = otaLock();
    if (ret != SNF_OTA_OK)
    {
        return ret;
    }

    if (control->task_handle == NULL)
    {
        LOG_I(tag, "OTA is not started");
        otaUnlock();
        return SNF_OTA_ERR_STATE;
    }

    event.id = SNF_OTA_EVT_ABORT;
    ret = otaQueueEventSend(&event);
    otaUnlock();

    return ret;
}
