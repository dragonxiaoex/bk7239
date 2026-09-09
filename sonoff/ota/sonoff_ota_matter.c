/**
 * @file    sonoff_ota_matter.c
 * @brief   Matter OTA链路适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-02
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "sonoff_log.h"
#include "sonoff_ota.h"
#include "sonoff_ota_matter.h"

/** @brief Matter OTA日志标签. */
static const char *tag = "SNF-OTA-MT";

#define SNF_OTA_MATTER_WAIT_INTERVAL_MS     (5)     /* 状态轮询间隔 */
#define SNF_OTA_MATTER_WAIT_TIMEOUT_MS      (20000) /* 单次写入等待超时 */

/** @brief Matter OTA运行状态. */
typedef struct
{
    volatile SnfOtaState state;
    volatile uint32_t received_size;
    volatile uint8_t percent;
} SnfMatterOtaState;

/** @brief Matter OTA运行状态实例. */
static SnfMatterOtaState matter_ota_state = {
    .state = SNF_OTA_STATE_IDLE,
    .received_size = 0,
    .percent = 0,
};

/**
 * @brief 处理OTA核心模块状态回调.
 *
 * @param [in] state - OTA状态.
 * @param [in] event_data - OTA事件数据.
 */
static void otaMatterEventCallback(SnfOtaState state,
                                   const SnfOtaEventData *event_data)
{
    SnfMatterOtaState *ota_state = &matter_ota_state;

    ota_state->state = state;
    if (event_data != NULL)
    {
        ota_state->received_size = event_data->received_size;
    }

    if ((ota_state->state == SNF_OTA_STATE_RECEIVING)
        && (ota_state->percent != event_data->percent))
    {
        ota_state->percent = event_data->percent;
        LOG_I(tag, "OTA receiving: %u%%", (unsigned int)ota_state->percent);
    }

    if (state != SNF_OTA_STATE_VERIFY_SUCCESS)
    {
        return;
    }

    if (snfOtaApply() != SNF_OTA_OK)
    {
        ota_state->state = SNF_OTA_STATE_FAILED;
    }
}

/**
 * @brief 等待一段Matter镜像数据完成写入.
 *
 * @param [in] expected_size - 期望已写入长度.
 * @return 0表示写入完成, -1表示失败或等待超时.
 */
static int otaMatterWaitWrite(uint32_t expected_size)
{
    SnfMatterOtaState *ota_state = &matter_ota_state;
    TickType_t start_tick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(SNF_OTA_MATTER_WAIT_TIMEOUT_MS))
    {
        if (ota_state->received_size >= expected_size)
        {
            return 0;
        }

        if (snfOtaStateIsFailed(ota_state->state) != 0)
        {
            return -1;
        }

        vTaskDelay(pdMS_TO_TICKS(SNF_OTA_MATTER_WAIT_INTERVAL_MS));
    }

    LOG_E(tag, "Matter OTA wait timeout");

    return -1;
}

int snfOtaMatterAbort(void)
{
    SnfMatterOtaState *ota_state = &matter_ota_state;
    TickType_t start_tick;
    int ret;

    if (snfOtaStateIsActive(ota_state->state) == 0)
    {
        return 0;
    }

    ret = snfOtaAbort();
    if (ret != SNF_OTA_OK)
    {
        LOG_E(tag, "abort Matter OTA failed: %d", ret);
        return -1;
    }

    start_tick = xTaskGetTickCount();
    while ((snfOtaStateIsActive(ota_state->state) != 0)
        && ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(SNF_OTA_MATTER_WAIT_TIMEOUT_MS)))
    {
        vTaskDelay(pdMS_TO_TICKS(SNF_OTA_MATTER_WAIT_INTERVAL_MS));
    }

    if (snfOtaStateIsActive(ota_state->state) != 0)
    {
        LOG_E(tag, "abort failed: timeout");
        return -1;
    }

    return 0;
}

int snfOtaMatterWrite(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (snfOtaWrite(offset, data, len) != SNF_OTA_OK)
    {
        return -1;
    }

    if (otaMatterWaitWrite(offset + len) != 0)
    {
        return -1;
    }

    return 0;
}

int snfOtaMatterStart(uint32_t image_size, uint32_t version)
{
    SnfMatterOtaState *ota_state = &matter_ota_state;
    SnfOtaConfig ota_config = {0};
    int ret;

    if (image_size == 0)
    {
        LOG_E(tag, "invalid Matter OTA image size");
        return -1;
    }

    ota_state->state = SNF_OTA_STATE_IDLE;
    ota_state->received_size = 0;
    ota_state->percent = 0;

    ota_config.image_info.size = image_size;
    ota_config.format = SNF_OTA_FORMAT_PACKAGE;
    memcpy(ota_config.file_name, SNF_OTA_DEFAULT_FILE_NAME, sizeof(SNF_OTA_DEFAULT_FILE_NAME));
    ota_config.image_info.version = version;
    ota_config.image_info.check_type = SNF_OTA_CHECK_NONE;
    ota_config.image_info.cipher_type = SNF_OTA_CIPHER_NONE;
    ota_config.event_callback = otaMatterEventCallback;

    ret = snfOtaStart(&ota_config);
    if (ret != SNF_OTA_OK)
    {
        LOG_E(tag, "start Matter OTA failed: %d", ret);
        return -1;
    }

    return 0;
}
