/**
 * @file    sonoff_wifi.c
 * @brief   WIFI状态管理模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-25
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>
#include <string.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <semphr.h>

#include "sonoff_wifi.h"
#include "sonoff_wifi_adapter.h"
#include "sonoff_log.h"
#include "sonoff_task_def.h"

static const char *tag = "SNF-WIFI";

#define SNF_WIFI_EVENT_QUEUE_SIZE           (10)        /* 事件队列长度 */

/**
 * @brief WIFI管理任务内部事件.
 */
typedef enum {
    /* cmd */
    SNF_WIFI_EVT_AP_START = 0,
    SNF_WIFI_EVT_STA_CONNECT,
    SNF_WIFI_EVT_SET_TO_IDLE,
    SNF_WIFI_EVT_STA_DISCONNECT,
    SNF_WIFI_EVT_SCAN,
    SNF_WIFI_EVT_STA_STOP,
    SNF_WIFI_EVT_AP_STOP,

    /* callback event */
    SNF_WIFI_EVT_CB_CONNECTED,
    SNF_WIFI_EVT_CB_DISCONNECTED,
    SNF_WIFI_EVT_CB_SCAN_DONE,
} SnfWifiInternalEventId;

/**
 * @brief WIFI管理任务事件数据.
 */
typedef struct
{
    SnfWifiApConfig ap_config;                  /* AP配置 */
    SnfWifiStaConfig sta_config;                /* STA配置 */
    SnfWifiStaDisconnectedEvent disconnect_event;/* STA断开信息 */
} SnfWifiEventData;

/**
 * @brief WIFI管理任务内部事件消息.
 */
typedef struct
{
    SnfWifiInternalEventId id;                  /* 事件标识 */
    uint32_t sta_generation;                    /* STA请求代次 */
    SnfWifiEventData data;                      /* 事件数据 */
} SnfWifiInternalEvent;

/**
 * @brief WIFI管理任务状态.
 */
typedef struct
{
    TaskHandle_t task_handle;                   /* 管理任务句柄 */
    QueueHandle_t event_queue;                  /* 管理任务事件队列 */
    SemaphoreHandle_t mutex;                    /* 信息获取互斥锁 */
    SnfWifiStaConfig sta_config;                /* 当前STA配置 */
    SnfWifiManageMode mode;                     /* 当前工作模式 */
    SnfWifiLinkStatus link_status;              /* 当前STA链路状态 */
    uint32_t sta_generation;                    /* 过滤前一轮STA请求的排队回调 */
    int scan_status;                            /* 当前扫描状态 */
    SnfWifiEventCB event_callback;              /* 事件回调 */
    void *event_user_data;                      /* 事件回调用户数据 */
} SnfWifiManageState;

static SnfWifiManageState wifi_manage_state = {
    .task_handle = NULL,
    .event_queue = NULL,
    .mutex = NULL,
    .sta_config = {
        .ssid = "",
        .password = "",
    },
    .mode = SNF_WIFI_MANAGE_MODE_IDLE,
    .link_status = SNF_WIFI_LINK_IDLE,
    .sta_generation = 0,
    .scan_status = 0,
    .event_callback = NULL,
    .event_user_data = NULL,
};

/**
 * @brief 向管理队列提交请求或回调.
 *
 * @param [in] event - 待复制的消息.
 * @return 0表示已入队, 负数表示未接受.
 */
static int eventSend(const SnfWifiInternalEvent *event)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if ((event == NULL) || (manage_state->event_queue == NULL) || (manage_state->task_handle == NULL))
    {
        return -1;
    }

    if (xQueueSend(manage_state->event_queue, event, 0) != pdPASS)
    {
        LOG_E(tag, "wifi event queue send failed");
        return -2;
    }

    return 0;
}

/**
 * @brief 获取状态锁.
 *
 * @return 0表示成功, 负数表示失败.
 */
static int mutexTake(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if ((manage_state->mutex == NULL)
        || (xSemaphoreTake(manage_state->mutex, portMAX_DELAY) != pdPASS))
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 释放状态锁.
 *
 * @return 0表示成功, 负数表示失败.
 */
static int mutexGive(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if (xSemaphoreGive(manage_state->mutex) != pdPASS)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 复制SDK事件到管理队列.
 *
 * @param [in] adapter_event - 适配层事件.
 * @param [in] event_data - 仅回调期间有效的数据.
 * @param [in] user_data - 注册时传入的数据.
 */
static void adapterEventCallback(int adapter_event, const void *event_data, void *user_data)
{
    SnfWifiManageState *manage_state = user_data;
    SnfWifiInternalEvent event = {0};

    switch (adapter_event)
    {
        case SNF_WIFI_ADP_EVT_CONNECTED:
            event.id = SNF_WIFI_EVT_CB_CONNECTED;
            break;
        case SNF_WIFI_ADP_EVT_DISCONNECTED:
            event.id = SNF_WIFI_EVT_CB_DISCONNECTED;
            if (event_data != NULL)
            {
                memcpy(&event.data.disconnect_event, event_data, sizeof(event.data.disconnect_event));
            }
            break;
        case SNF_WIFI_ADP_EVT_SCAN_DONE:
            event.id = SNF_WIFI_EVT_CB_SCAN_DONE;
            break;
        default:
            return;
    }

    if (mutexTake() != 0)
    {
        return;
    }
    event.sta_generation = manage_state->sta_generation;
    mutexGive();
    eventSend(&event);
}

/**
 * @brief 在状态锁外调用已注册的回调.
 *
 * @param [in] event - 对外事件.
 * @param [in] event_data - 仅回调期间有效的数据.
 */
static void eventCallback(SnfWifiExternalEventId event, const void *event_data)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    SnfWifiEventCB callback;
    void *user_data;

    if (mutexTake() != 0)
    {
        return;
    }
    callback = manage_state->event_callback;
    user_data = manage_state->event_user_data;
    mutexGive();

    if (callback != NULL)
    {
        callback(event, event_data, user_data);
    }
}

/**
 * @brief 记录并通知控制失败.
 *
 * @param [in] event - 失败事件.
 * @param [in] adapter_error - 适配层错误码.
 */
static void eventFailed(SnfWifiExternalEventId event, int adapter_error)
{
    LOG_E(tag, "event %d failed, ret=%d", event, adapter_error);
    eventCallback(event, &adapter_error);
}

/**
 * @brief 根据SDK接口事实更新WIFI模式及链路状态, 解锁后通知模式变化.
 *
 * @param [in] link_status - STA链路状态.
 * @return 0表示成功, 负数表示失败.
 */
static int stateUpdate(SnfWifiLinkStatus link_status)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    SnfWifiModeChangedEvent changed = {.current = SNF_WIFI_MANAGE_MODE_IDLE};

    if (snfWifiAdapterStaIsStarted())
    {
        changed.current = SNF_WIFI_MANAGE_MODE_STA;
    }
    if (snfWifiAdapterApIsStarted())
    {
        changed.current = (changed.current == SNF_WIFI_MANAGE_MODE_STA)
                          ? SNF_WIFI_MANAGE_MODE_AP_STA : SNF_WIFI_MANAGE_MODE_AP;
    }

    if (mutexTake() != 0)
    {
        return -1;
    }
    changed.previous = manage_state->mode;
    manage_state->mode = changed.current;
    if ((changed.current == SNF_WIFI_MANAGE_MODE_IDLE)
        || (changed.current == SNF_WIFI_MANAGE_MODE_AP))
    {
        link_status = SNF_WIFI_LINK_IDLE;
    }
    manage_state->link_status = link_status;
    mutexGive();

    if (changed.previous != changed.current)
    {
        eventCallback(SNF_WIFI_EVT_MODE_CHANGED, &changed);
    }

    return 0;
}

/**
 * @brief 配置并启动AP, 保留STA.
 *
 * @param [in] config - AP配置.
 */
static void apStart(const SnfWifiApConfig *config)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int adapter_error;

    eventCallback(SNF_WIFI_EVT_AP_STARTING, NULL);
    adapter_error = snfWifiAdapterApSetConfig(config);
    if ((adapter_error == SNF_WIFI_ADAPTER_OK) && !snfWifiAdapterApIsStarted())
    {
        adapter_error = snfWifiAdapterApStart();
    }
    if (stateUpdate(manage_state->link_status) != 0)
    {
        return;
    }
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        eventFailed(SNF_WIFI_EVT_AP_START_FAILED, adapter_error);
        return;
    }

    eventCallback(SNF_WIFI_EVT_AP_STARTED, NULL);
}

/**
 * @brief 配置并连接STA, 保留AP.
 *
 * @param [in] config - STA配置.
 */
static void staConnect(const SnfWifiStaConfig *config)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int adapter_error;

    adapter_error = snfWifiAdapterStaSetConfig(config);
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        eventFailed(SNF_WIFI_EVT_STA_CONNECT_FAILED, adapter_error);
        return;
    }

    if (mutexTake() != 0)
    {
        return;
    }
    manage_state->sta_generation++;
    memcpy(&manage_state->sta_config, config, sizeof(manage_state->sta_config));
    mutexGive();

    if (!snfWifiAdapterStaIsStarted())
    {
        adapter_error = snfWifiAdapterStaStart();
    }
    if (stateUpdate(SNF_WIFI_LINK_CONNECTING) != 0)
    {
        return;
    }
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        if (stateUpdate(SNF_WIFI_LINK_DISCONNECTED) != 0)
        {
            return;
        }
        eventFailed(SNF_WIFI_EVT_STA_CONNECT_FAILED, adapter_error);
        return;
    }

    /* 先通知连接意图, 防止SDK的IP事件先于网络层进入连接状态. */
    eventCallback(SNF_WIFI_EVT_STA_CONNECTING, NULL);
    adapter_error = snfWifiAdapterStaConnect();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        if (stateUpdate(SNF_WIFI_LINK_DISCONNECTED) != 0)
        {
            return;
        }
        eventFailed(SNF_WIFI_EVT_STA_CONNECT_FAILED, adapter_error);
    }
}

/** @brief 断开STA连接, 保留两个接口的启用状态. */
static void staDisconnect(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    SnfWifiStaDisconnectedEvent disconnected = {.reason = SNF_WIFI_DISCONNECT_REASON_USER};
    SnfWifiLinkInfo link_info = {0};
    int adapter_error;

    if ((manage_state->mode != SNF_WIFI_MANAGE_MODE_STA)
        && (manage_state->mode != SNF_WIFI_MANAGE_MODE_AP_STA))
    {
        return;
    }

    if (mutexTake() != 0)
    {
        return;
    }
    manage_state->sta_generation++;
    mutexGive();

    adapter_error = snfWifiAdapterStaDisconnect();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        eventFailed(SNF_WIFI_EVT_STA_DISCONNECT_FAILED, adapter_error);
        return;
    }

    if (stateUpdate(SNF_WIFI_LINK_DISCONNECTING) != 0)
    {
        return;
    }
    eventCallback(SNF_WIFI_EVT_STA_DISCONNECTING, NULL);
    /* SDK在已经断开时可能不再发出回调, 主动完成本次断连通知. */
    if (snfWifiAdapterStaGetLinkInfo(&link_info) == SNF_WIFI_ADAPTER_ERR_NOT_INIT)
    {
        if (stateUpdate(SNF_WIFI_LINK_DISCONNECTED) != 0)
        {
            return;
        }
        eventCallback(SNF_WIFI_EVT_STA_DISCONNECTED, &disconnected);
    }
}

/**
 * @brief 关闭请求指定的接口, 同步部分失败后的模式.
 *
 * @param [in] id - STA停止、AP停止或IDLE请求.
 */
static void interfacesStop(SnfWifiInternalEventId id)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    SnfWifiLinkInfo link_info = {0};
    SnfWifiStaDisconnectedEvent disconnected = {.reason = SNF_WIFI_DISCONNECT_REASON_USER};
    int stop_sta = (id != SNF_WIFI_EVT_AP_STOP) && snfWifiAdapterStaIsStarted();
    int stop_ap = (id != SNF_WIFI_EVT_STA_STOP) && snfWifiAdapterApIsStarted();
    int sta_error = SNF_WIFI_ADAPTER_OK;
    int ap_error = SNF_WIFI_ADAPTER_OK;

    if (stop_sta)
    {
        if (mutexTake() != 0)
        {
            return;
        }
        manage_state->sta_generation++;
        mutexGive();
        /* 先断开链路; 即使断连失败也继续关闭接口. */
        snfWifiAdapterStaDisconnect();
        sta_error = snfWifiAdapterStaStop();
    }
    if (stop_ap)
    {
        ap_error = snfWifiAdapterApStop();
    }
    if (stateUpdate(manage_state->link_status) != 0)
    {
        return;
    }

    if (stop_sta)
    {
        if (snfWifiAdapterStaIsStarted())
        {
            if (snfWifiAdapterStaGetLinkInfo(&link_info) == SNF_WIFI_ADAPTER_ERR_NOT_INIT)
            {
                if (stateUpdate(SNF_WIFI_LINK_DISCONNECTED) != 0)
                {
                    return;
                }
                eventCallback(SNF_WIFI_EVT_STA_DISCONNECTED, &disconnected);
            }
            eventFailed(SNF_WIFI_EVT_STA_STOP_FAILED, (sta_error == SNF_WIFI_ADAPTER_OK)
                        ? SNF_WIFI_ADAPTER_ERR_INTERNAL : sta_error);
        }
        else
        {
            eventCallback(SNF_WIFI_EVT_STA_STOPPED, NULL);
        }
    }
    if (stop_ap)
    {
        if (snfWifiAdapterApIsStarted())
        {
            eventFailed(SNF_WIFI_EVT_AP_STOP_FAILED, (ap_error == SNF_WIFI_ADAPTER_OK)
                        ? SNF_WIFI_ADAPTER_ERR_INTERNAL : ap_error);
        }
        else
        {
            eventCallback(SNF_WIFI_EVT_AP_STOPPED, NULL);
        }
    }
}

/**
 * @brief 处理STA链路回调, 丢弃关闭或重新连接前的排队事件.
 *
 * @param [in] event - STA连接或断开事件.
 */
static void staLinkEvent(const SnfWifiInternalEvent *event)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    SnfWifiLinkInfo link_info = {0};
    SnfWifiStaDisconnectedEvent disconnect_event = event->data.disconnect_event;
    int adapter_error;

    if ((event->sta_generation != manage_state->sta_generation)
        || ((manage_state->mode != SNF_WIFI_MANAGE_MODE_STA)
            && (manage_state->mode != SNF_WIFI_MANAGE_MODE_AP_STA)))
    {
        return;
    }

    adapter_error = snfWifiAdapterStaGetLinkInfo(&link_info);
    if (event->id == SNF_WIFI_EVT_CB_CONNECTED)
    {
        if ((manage_state->link_status == SNF_WIFI_LINK_DISCONNECTING)
            || (adapter_error != SNF_WIFI_ADAPTER_OK)
            || (strcmp(link_info.ssid, manage_state->sta_config.ssid) != 0))
        {
            return;
        }

        if (stateUpdate(SNF_WIFI_LINK_CONNECTED) != 0)
        {
            return;
        }
        eventCallback(SNF_WIFI_EVT_STA_CONNECTED, &link_info);
        return;
    }

    /* SDK重新连接时会先主动断开旧链路, 此回调不结束新的连接请求. */
    if ((manage_state->link_status == SNF_WIFI_LINK_CONNECTING)
        && (disconnect_event.reason == SNF_WIFI_DISCONNECT_REASON_USER))
    {
        return;
    }
    if ((adapter_error == SNF_WIFI_ADAPTER_OK)
        || (manage_state->link_status == SNF_WIFI_LINK_DISCONNECTED))
    {
        return;
    }
    if (manage_state->link_status == SNF_WIFI_LINK_DISCONNECTING)
    {
        disconnect_event.reason = SNF_WIFI_DISCONNECT_REASON_USER;
    }
    else if (disconnect_event.reason == SNF_WIFI_DISCONNECT_REASON_USER)
    {
        disconnect_event.reason = SNF_WIFI_DISCONNECT_REASON_UNKNOWN;
    }
    else
    {
        /* 保留SDK提供的断开原因. */
    }
    if (stateUpdate(SNF_WIFI_LINK_DISCONNECTED) != 0)
    {
        return;
    }
    eventCallback(SNF_WIFI_EVT_STA_DISCONNECTED, &disconnect_event);
}

/**
 * @brief 分发一个管理事件.
 *
 * @param [in] event - 已出队的请求或回调.
 */
static void processEvent(const SnfWifiInternalEvent *event)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int adapter_error;

    switch (event->id)
    {
        case SNF_WIFI_EVT_AP_START:
            apStart(&event->data.ap_config);
            break;
        case SNF_WIFI_EVT_STA_CONNECT:
            staConnect(&event->data.sta_config);
            break;
        case SNF_WIFI_EVT_SET_TO_IDLE:
        case SNF_WIFI_EVT_STA_STOP:
        case SNF_WIFI_EVT_AP_STOP:
            interfacesStop(event->id);
            break;
        case SNF_WIFI_EVT_STA_DISCONNECT:
            staDisconnect();
            break;
        case SNF_WIFI_EVT_SCAN:
            if (manage_state->scan_status != 0)
            {
                eventFailed(SNF_WIFI_EVT_SCAN_FAILED, SNF_WIFI_ADAPTER_ERR_BUSY);
                break;
            }
            adapter_error = snfWifiAdapterScan();
            if (adapter_error != SNF_WIFI_ADAPTER_OK)
            {
                eventFailed(SNF_WIFI_EVT_SCAN_FAILED, adapter_error);
                break;
            }
            if (mutexTake() != 0)
            {
                break;
            }
            manage_state->scan_status = 1;
            mutexGive();
            break;
        case SNF_WIFI_EVT_CB_CONNECTED:
        case SNF_WIFI_EVT_CB_DISCONNECTED:
            staLinkEvent(event);
            break;
        case SNF_WIFI_EVT_CB_SCAN_DONE:
            if (mutexTake() != 0)
            {
                break;
            }
            manage_state->scan_status = 0;
            mutexGive();
            eventCallback(SNF_WIFI_EVT_SCAN_DONE, NULL);
            break;
        default:
            LOG_W(tag, "unknown wifi event id: %d", event->id);
            break;
    }
}

/**
 * @brief 等待并处理WIFI管理事件.
 *
 * @param [in] arg - 任务参数.
 */
static void wifiTask(void *arg)
{
    SnfWifiManageState *manage_state = arg;
    SnfWifiInternalEvent event = {0};

    while (1)
    {
        if (xQueueReceive(manage_state->event_queue, &event, portMAX_DELAY) == pdPASS)
        {
            processEvent(&event);
        }
    }
}

int snfWifiGetMode(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int mode = SNF_WIFI_MANAGE_MODE_IDLE;

    if (mutexTake() != 0)
    {
        return mode;
    }
    mode = manage_state->mode;
    mutexGive();

    return mode;
}

int snfWifiStaConnect(const SnfWifiStaConfig *config)
{
    SnfWifiInternalEvent event = {0};

    if (config == NULL)
    {
        return -1;
    }

    event.id = SNF_WIFI_EVT_STA_CONNECT;
    memcpy(&event.data.sta_config, config, sizeof(event.data.sta_config));

    return eventSend(&event);
}

int snfWifiStaDisconnect(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_STA_DISCONNECT;

    return eventSend(&event);
}

int snfWifiStaStop(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_STA_STOP;

    return eventSend(&event);
}

int snfWifiStaGetLinkInfo(SnfWifiLinkInfo *info)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int ret = 0;

    if (info == NULL)
    {
        return -1;
    }
    if (snfWifiAdapterStaGetLinkInfo(info) != SNF_WIFI_ADAPTER_OK)
    {
        return -2;
    }
    if (mutexTake() != 0)
    {
        return -1;
    }
    if ((manage_state->link_status == SNF_WIFI_LINK_IDLE)
        || (manage_state->link_status == SNF_WIFI_LINK_DISCONNECTING)
        || (strcmp(info->ssid, manage_state->sta_config.ssid) != 0))
    {
        ret = -2;
    }
    mutexGive();

    return ret;
}

int snfWifiStaGetRssi(int *rssi)
{
    if (rssi == NULL)
    {
        return -1;
    }
    if (snfWifiGetLinkStatus() != SNF_WIFI_LINK_CONNECTED)
    {
        return -2;
    }
    if (snfWifiAdapterStaGetRssi(rssi) != SNF_WIFI_ADAPTER_OK)
    {
        return -3;
    }

    return 0;
}

int snfWifiApStart(const SnfWifiApConfig *config)
{
    SnfWifiInternalEvent event = {0};

    if (config == NULL)
    {
        return -1;
    }

    event.id = SNF_WIFI_EVT_AP_START;
    memcpy(&event.data.ap_config, config, sizeof(event.data.ap_config));

    return eventSend(&event);
}

int snfWifiApStop(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_AP_STOP;

    return eventSend(&event);
}

int snfWifiSetIdle(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_SET_TO_IDLE;

    return eventSend(&event);
}

int snfWifiScan(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_SCAN;

    return eventSend(&event);
}

int snfWifiScanStatus(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int scan_status;

    if (mutexTake() != 0)
    {
        return 0;
    }
    scan_status = manage_state->scan_status;
    mutexGive();

    return scan_status;
}

int snfWifiScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count, uint16_t *result_count)
{
    if (snfWifiAdapterScanGetResults(results, max_count, result_count) != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "get scan results failed");
        return -1;
    }

    return 0;
}

int snfWifiGetLinkStatus(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int link_status = 0;

    if (mutexTake() != 0)
    {
        return -1;
    }
    link_status = manage_state->link_status;
    mutexGive();

    return link_status;
}

int snfWifiRegisterEventCallback(SnfWifiEventCB callback, void *user_data)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if (callback == NULL)
    {
        return -2;
    }
    if (manage_state->mutex != NULL)
    {
        if (mutexTake() != 0)
        {
            return -1;
        }
    }
    if (manage_state->event_callback != NULL)
    {
        if (manage_state->mutex != NULL)
        {
            mutexGive();
        }
        LOG_E(tag, "event callback already registered");
        return -1;
    }
    manage_state->event_callback = callback;
    manage_state->event_user_data = user_data;
    if (manage_state->mutex != NULL)
    {
        mutexGive();
    }

    return 0;
}

int snfWifiInit(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int adapter_error = SNF_WIFI_ADAPTER_OK;
    
    if (manage_state->task_handle != NULL)
    {
        return 0;
    }

    if (manage_state->event_queue == NULL)
    {
        manage_state->event_queue = xQueueCreate(SNF_WIFI_EVENT_QUEUE_SIZE, sizeof(SnfWifiInternalEvent));

        if (manage_state->event_queue == NULL)
        {
            LOG_E(tag, "event queue init failed");
            return -1;
        }
    }

    if (manage_state->mutex == NULL)
    {
        manage_state->mutex = xSemaphoreCreateMutex();
        if (manage_state->mutex == NULL)
        {
            LOG_E(tag, "mutex init failed");
            return -2;
        }
    }

    adapter_error = snfWifiAdapterInit();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "wifi adapter init failed, ret=%d", adapter_error);
        return -2;
    }

    adapter_error = snfWifiAdapterRegisterEventCallback(adapterEventCallback, manage_state);
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "wifi adapter callback register failed, ret=%d", adapter_error);
        return -3;
    }

    if (xTaskCreate(wifiTask,
                   SONOFF_WIFI_TASK_NAME,
                   SONOFF_WIFI_TASK_STACKSIZE,
                   manage_state,
                   SONOFF_WIFI_TASK_PRIO,
                   &manage_state->task_handle) != pdPASS)
    {
        LOG_E(tag, "wifi task init failed");
        return -4;
    }

    return 0;
}
