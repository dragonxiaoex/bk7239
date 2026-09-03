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

#define SNF_WIFI_EVENT_QUEUE_SIZE   10U

/**
 * @brief WIFI管理任务内部事件.
 */
typedef enum {
    /* cmd */
    SNF_WIFI_EVT_SET_TO_AP = 0,
    SNF_WIFI_EVT_SET_TO_STA,
    SNF_WIFI_EVT_SET_TO_IDLE,
    SNF_WIFI_EVT_DISCONNECT,
    SNF_WIFI_EVT_SCAN,

    /* callback event */
    SNF_WIFI_EVT_CB_CONNECTED,
    SNF_WIFI_EVT_CB_DISCONNECTED,
    SNF_WIFI_EVT_CB_SCAN_DONE,
} SnfWifiInternalEventId;

/**
 * @brief WIFI管理任务事件数据.
 */
typedef struct {
    SnfWifiApConfig ap_config;                      /* AP配置 */
    SnfWifiStaConfig sta_config;                    /* STA配置 */
    SnfWifiLinkInfo link_info;                      /* STA连接信息 */
    SnfWifiStaDisconnectedEvent disconnect_event;   /* STA断开信息 */
} SnfWifiEventData;

/**
 * @brief WIFI管理任务内部事件消息.
 */
typedef struct {
    int id;                                  /* 事件标识 */
    SnfWifiEventData data;                   /* 事件数据 */
} SnfWifiInternalEvent;

/**
 * @brief WIFI管理任务状态.
 */
typedef struct {
    TaskHandle_t task_handle;                /* 管理任务句柄 */
    QueueHandle_t event_queue;               /* 管理任务事件队列 */
    SemaphoreHandle_t mutex;                 /* 信息获取互斥锁 */
    SnfWifiApConfig ap_config;               /* 当前AP配置 */
    SnfWifiStaConfig sta_config;             /* 当前STA配置 */
    SnfWifiLinkInfo link_info;               /* 当前STA连接信息 */
    SnfWifiManageMode mode;                  /* 当前工作模式 */
    int link_status;                         /* 当前链路状态 */
    int scan_status;                         /* 当前扫描状态 */
    SnfWifiEventCB event_callback;           /* 事件回调 */
    void *event_user_data;                   /* 事件回调用户数据 */
} SnfWifiManageState;

static SnfWifiManageState wifi_manage_state = {
    .task_handle = NULL,
    .event_queue = NULL,
    .mutex = NULL,
    .ap_config = {
        .ssid = "",
        .password = "",
        .channel = 0,
        .max_connections = 0,
        .security = SNF_WIFI_SECURITY_OPEN,
    },
    .sta_config = {
        .ssid = "",
        .password = "",
    },
    .link_info = {
        .ssid = "",
        .bssid = {0},
        .rssi = 0,
        .channel = 0,
        .security = SNF_WIFI_SECURITY_UNKNOWN,
    },
    .mode = SNF_WIFI_MANAGE_MODE_IDLE,
    .link_status = SNF_WIFI_LINK_IDLE,
    .scan_status = 0,
    .event_callback = NULL,
    .event_user_data = NULL,
};

static int snfWifiEventSend(const SnfWifiInternalEvent *event)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if ((event == NULL) || (manage_state->event_queue == NULL))
    {
        LOG_E(tag, "wifi event queue not init");
        return -1;
    }

    if (xQueueSend(manage_state->event_queue, event, 0) != pdPASS)
    {
        LOG_E(tag, "wifi event queue send failed");
        return -2;
    }

    return 0;
}

static int snfWifiMutexTake(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if (xSemaphoreTake(manage_state->mutex, portMAX_DELAY) != pdPASS)
    {
        LOG_E(tag, "get mutex failed");
        return -1;
    }

    return 0;
}

static int snfWifiMutexGive(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if (xSemaphoreGive(manage_state->mutex) != pdPASS)
    {
        return -1;
    }

    return 0;
}

static void snfWifiAdapterEventCallback(int adapter_event, const void *event_data, void *user_data)
{
    SnfWifiInternalEvent event = {0};
    int event_id = 0;

    switch (adapter_event)
    {
        case SNF_WIFI_ADP_EVT_CONNECTED:
            event_id = SNF_WIFI_EVT_CB_CONNECTED;
            if (event_data != NULL)
            {
                memcpy(&event.data.link_info, event_data, sizeof(event.data.link_info));
            }
            break;
        case SNF_WIFI_ADP_EVT_DISCONNECTED:
            event_id = SNF_WIFI_EVT_CB_DISCONNECTED;
            if (event_data != NULL)
            {
                memcpy(&event.data.disconnect_event, event_data,
                       sizeof(event.data.disconnect_event));
            }
            break;
        case SNF_WIFI_ADP_EVT_SCAN_DONE:
            event_id = SNF_WIFI_EVT_CB_SCAN_DONE;
            break;
        default:
            break;
    }

    if (event_id != 0)
    {
        event.id = event_id;
        snfWifiEventSend(&event);
    }
}

static void eventCallback(SnfWifiExternalEventId event, const void *event_data)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    SnfWifiEventCB callback = manage_state->event_callback;
    void *user_data = manage_state->event_user_data;

    if (callback != NULL)
    {
        callback(event, event_data, user_data);
    }
}

static void snfWifiProcessEvent(const SnfWifiInternalEvent *event)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int adapter_error;

    if (event == NULL)
    {
        return;
    }

    switch (event->id)
    {
        case SNF_WIFI_EVT_SET_TO_AP:
        {
            adapter_error = snfWifiAdapterApSetConfig(&event->data.ap_config);
            if (adapter_error != SNF_WIFI_ADAPTER_OK)
            {
                LOG_E(tag, "ap set config failed, ret=%d", adapter_error);
                break;
            }

            adapter_error = snfWifiAdapterSetMode(SNF_WIFI_MODE_AP);
            if (adapter_error != SNF_WIFI_ADAPTER_OK)
            {
                LOG_E(tag, "ap set mode failed, ret=%d", adapter_error);
                break;
            }

            snfWifiMutexTake();
            memcpy(&manage_state->ap_config, &event->data.ap_config,
                   sizeof(manage_state->ap_config));
            memset(&manage_state->link_info, 0, sizeof(manage_state->link_info));
            manage_state->mode = SNF_WIFI_MANAGE_MODE_AP;
            manage_state->link_status = SNF_WIFI_LINK_IDLE;
            snfWifiMutexGive();

            eventCallback(SNF_WIFI_EVT_AP_STARTED, NULL);
            break;
        }

        case SNF_WIFI_EVT_SET_TO_STA:
        {
            adapter_error = snfWifiAdapterStaSetConfig(&event->data.sta_config);
            if (adapter_error != SNF_WIFI_ADAPTER_OK)
            {
                LOG_E(tag, "sta set config failed, ret=%d", adapter_error);
                break;
            }

            adapter_error = snfWifiAdapterSetMode(SNF_WIFI_MODE_STA);
            if (adapter_error != SNF_WIFI_ADAPTER_OK)
            {
                LOG_E(tag, "sta set mode failed, ret=%d", adapter_error);
                break;
            }

            snfWifiMutexTake();
            memcpy(&manage_state->sta_config, &event->data.sta_config,
                   sizeof(manage_state->sta_config));
            memset(&manage_state->link_info, 0, sizeof(manage_state->link_info));
            manage_state->mode = SNF_WIFI_MANAGE_MODE_STA;
            manage_state->link_status = SNF_WIFI_LINK_CONNECTING;

            adapter_error = snfWifiAdapterStaConnect();
            if (adapter_error != SNF_WIFI_ADAPTER_OK)
            {
                manage_state->link_status = SNF_WIFI_LINK_DISCONNECTED;
                LOG_E(tag, "sta connect failed, ret=%d", adapter_error);
            }
            snfWifiMutexGive();

            eventCallback(SNF_WIFI_EVT_STA_CONNECTING, NULL);
            break;
        }

        case SNF_WIFI_EVT_SET_TO_IDLE:
        {
            adapter_error = snfWifiAdapterSetMode(SNF_WIFI_MODE_NONE);
            if (adapter_error != SNF_WIFI_ADAPTER_OK)
            {
                LOG_E(tag, "set wifi idle failed, ret=%d", adapter_error);
                break;
            }

            snfWifiMutexTake();
            manage_state->mode = SNF_WIFI_MANAGE_MODE_IDLE;
            memset(&manage_state->link_info, 0, sizeof(manage_state->link_info));
            manage_state->link_status = SNF_WIFI_LINK_IDLE;
            snfWifiMutexGive();
            break;
        }

        case SNF_WIFI_EVT_DISCONNECT:
        {
            snfWifiMutexTake();
            if (manage_state->mode == SNF_WIFI_MANAGE_MODE_STA)
            {
                adapter_error = snfWifiAdapterStaDisconnect();
                if (adapter_error == SNF_WIFI_ADAPTER_OK)
                {
                    memset(&manage_state->link_info, 0, sizeof(manage_state->link_info));
                    manage_state->link_status = SNF_WIFI_LINK_DISCONNECTING;
                }
                else
                {
                    LOG_E(tag, "sta disconnect failed, ret=%d", adapter_error);
                }
            }
            snfWifiMutexGive();
            break;
        }

        case SNF_WIFI_EVT_SCAN:
        {
            adapter_error = snfWifiAdapterScan();
            snfWifiMutexTake();
            if (adapter_error == SNF_WIFI_ADAPTER_OK)
            {
                manage_state->scan_status = 1;
            }
            else
            {
                LOG_E(tag, "wifi scan failed, ret=%d", adapter_error);
            }
            snfWifiMutexGive();
            break;
        }  

        case SNF_WIFI_EVT_CB_CONNECTED:
        {
            snfWifiMutexTake();
            memcpy(&manage_state->link_info, &event->data.link_info,
                    sizeof(manage_state->link_info));
            manage_state->link_status = SNF_WIFI_LINK_CONNECTED;
            snfWifiMutexGive();

            eventCallback(SNF_WIFI_EVT_STA_CONNECTED, &event->data.link_info);
            break;
        } 

        case SNF_WIFI_EVT_CB_DISCONNECTED:
        {
            snfWifiMutexTake();
            memset(&manage_state->link_info, 0, sizeof(manage_state->link_info));
            manage_state->link_status = SNF_WIFI_LINK_DISCONNECTED;
            snfWifiMutexGive();

            eventCallback(SNF_WIFI_EVT_STA_DISCONNECTED, NULL);
            break;
        }

        case SNF_WIFI_EVT_CB_SCAN_DONE:
        {
            snfWifiMutexTake();
            manage_state->scan_status = 0;
            snfWifiMutexGive();

            eventCallback(SNF_WIFI_EVT_SCAN_DONE, NULL);
            break;
        }
            
        default:
            LOG_W(tag, "unknown wifi event id: %d", event->id);
            break;
    }
}

int snfWifiGetMode(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int mode = SNF_WIFI_MANAGE_MODE_IDLE;

    snfWifiMutexTake();
    mode = manage_state->mode;
    snfWifiMutexGive();

    return mode;
}

int snfWifiStaConnect(const SnfWifiStaConfig *config)
{
    SnfWifiInternalEvent event = {0};

    if (config == NULL)
    {
        return -1;
    }

    event.id = SNF_WIFI_EVT_SET_TO_STA;
    memcpy(&event.data.sta_config, config, sizeof(event.data.sta_config));

    return snfWifiEventSend(&event);
}

int snfWifiStaDisconnect(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_DISCONNECT;

    return snfWifiEventSend(&event);
}

int snfWifiStaGetLinkInfo(SnfWifiLinkInfo *info)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if (info == NULL)
    {
        return -1;
    }

    snfWifiMutexTake();
    if ((manage_state->mode != SNF_WIFI_MANAGE_MODE_STA)
        || (manage_state->link_status != SNF_WIFI_LINK_CONNECTED))
    {
        LOG_E(tag, "sta not connected");
        snfWifiMutexGive();

        return -2;
    }
    
    memcpy(info, &manage_state->link_info, sizeof(manage_state->link_info));
    snfWifiMutexGive();

    return 0;
}

int snfWifiStaGetRssi(int *rssi)
{
    int ret = 0;

    if (rssi == NULL)
    {
        return -1;
    }

    snfWifiMutexTake();
    if(snfWifiAdapterStaGetRssi(rssi) != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "get rssi failed");
        ret = -3;
    }
    snfWifiMutexGive();

    return ret;
}

int snfWifiApStart(const SnfWifiApConfig *config)
{
    SnfWifiInternalEvent event = {0};

    if (config == NULL)
    {
        return -1;
    }

    event.id = SNF_WIFI_EVT_SET_TO_AP;
    memcpy(&event.data.ap_config, config, sizeof(event.data.ap_config));

    return snfWifiEventSend(&event);
}

int snfWifiApStop(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_SET_TO_IDLE;

    return snfWifiEventSend(&event);
}

int snfWifiScan(void)
{
    SnfWifiInternalEvent event = {0};

    event.id = SNF_WIFI_EVT_SCAN;

    return snfWifiEventSend(&event);
}

int snfWifiScanStatus(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    return manage_state->scan_status;
}

int snfWifiScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count, uint16_t *result_count)
{
    int ret = 0;
    snfWifiMutexTake();
    if (snfWifiAdapterScanGetResults(results, max_count, result_count) != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "get scan results failed");
        ret = -1;
    }
    snfWifiMutexGive();

    return ret;
}

int snfWifiGetLinkStatus(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int link_status = 0;

    snfWifiMutexTake();
    link_status = manage_state->link_status;
    snfWifiMutexGive();

    return link_status;
}

int snfWifiRegisterEventCallback(SnfWifiEventCB callback, void *user_data)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;

    if (manage_state->event_callback != NULL)
    {
        LOG_E(tag, "event callback already registered");
        return -1;
    }

    if (callback == NULL)
    {
        LOG_E(tag, "event callback is NULL");
        return -2;
    }

    manage_state->event_callback = callback;
    manage_state->event_user_data = user_data;

    return 0;
}

static void snfWifiTask(void *arg)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    SnfWifiInternalEvent event;

    while (1)
    {
        memset(&event, 0, sizeof(event));
        if (xQueueReceive(manage_state->event_queue, &event, portMAX_DELAY) == pdPASS)
        {
            LOG_I(tag, "event id: %d", event.id);
            snfWifiProcessEvent(&event);
        }
    }
}

int snfWifiInit(void)
{
    SnfWifiManageState *manage_state = &wifi_manage_state;
    int adapter_error = SNF_WIFI_ADAPTER_OK;
    
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

    adapter_error = snfWifiAdapterRegisterEventCallback(snfWifiAdapterEventCallback, NULL);
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "wifi adapter callback register failed, ret=%d", adapter_error);
        return -3;
    }

    if (manage_state->task_handle == NULL)
    {
        if (xTaskCreate(snfWifiTask,
                    SONOFF_WIFI_TASK_NAME,
                    SONOFF_WIFI_TASK_STACKSIZE,
                    NULL,
                    SONOFF_WIFI_TASK_PRIO,
                    &manage_state->task_handle) != pdPASS)
        {
            LOG_E(tag, "wifi task init failed");
            return -4;
        }
    }

    return 0;
}
