/**
 * @file    sonoff_wifi.c
 * @brief   wifi状态管理模块
 * 
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-20
 * 
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 * 
 */
#include "stdint.h"
#include "string.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "sonoff_wifi.h"
#include "sonoff_wifi_adapter.h"
#include "sonoff_log.h"
#include "sonoff_task_def.h"

static const char *tag = "wifi";

#define WIFI_EVT_QUEUE_SIZE                         5


typedef struct {
    int id;
    void *data;
} SnfWifiEvent;

typedef struct {
    TaskHandle_t task_handle;
    QueueHandle_t evt_queue;

    SnfWifiApConfig ap_config;
    SnfWifiStaConfig sta_config;
    int cur_mode;
    int cur_status;
    int scan_status;
} SnfWifiManageData;

static SnfWifiManageData wifi_manage_data = {
    .task_handle = NULL,
    .evt_queue = NULL,
    .ap_config = {0},
    .sta_config = {0},
    .cur_mode = SNF_WIFI_MODE_NONE,
    .cur_status = SNF_WIFI_IDLE,
    .scan_status = 0,
};

static void snfWifiTestCallback(SnfWifiAdapterEvt event, const void *event_data, void *user_data)
{
    switch(event)
    {
        case SNF_WIFI_ADP_EVT_CONNECTED:
            LOG_I(tag, "snfWifiAdapterTestCallback connected");
            break;
        case SNF_WIFI_ADP_EVT_DISCONNECTED:
            LOG_I(tag, "snfWifiAdapterTestCallback disconnected");
            break;
        case SNF_WIFI_ADP_EVT_SCAN_DONE:
            LOG_I(tag, "snfWifiAdapterTestCallback scan done");
            break;
        case SNF_WIFI_ADP_EVT_AP_STARTED:
            LOG_I(tag, "snfWifiAdapterTestCallback ap started");
            break;
        case SNF_WIFI_ADP_EVT_AP_STOPPED:
            LOG_I(tag, "snfWifiAdapterTestCallback ap stopped");
            break;
        case SNF_WIFI_ADP_EVT_AP_CLIENT_CONNECTED:
        {
            const SnfWifiApClientEvent *client_event = (const SnfWifiApClientEvent *)event_data;
            LOG_I(tag, "snfWifiAdapterTestCallback ap client connected, mac: %02x:%02x:%02x:%02x:%02x:%02x, ip: %d.%d.%d.%d", 
                        client_event->mac[0], client_event->mac[1], client_event->mac[2], 
                        client_event->mac[3], client_event->mac[4], client_event->mac[5], 
                        (client_event->ip_addr >> 24) & 0xFF, (client_event->ip_addr >> 16) & 0xFF, 
                        (client_event->ip_addr >> 8) & 0xFF, client_event->ip_addr & 0xFF);
            break;
        } 
        case SNF_WIFI_ADP_EVT_AP_CLIENT_DISCONNECTED:
            LOG_I(tag, "snfWifiAdapterTestCallback ap client disconnected");
            break;
        default:
            break;
    }
}

void snfWifiTest(void)
{
    SnfWifiApConfig ap_config;
    int ret = SNF_WIFI_ADAPTER_OK;

    strncpy(ap_config.ssid, "axRzon", sizeof(ap_config.ssid));
    strncpy(ap_config.password, "12345678", sizeof(ap_config.password));
    ap_config.channel = 1;
    ap_config.max_connections = 2;
    ap_config.security = SNF_WIFI_SECURITY_WPA2;

    ret = snfWifiAdapterInit();
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterInit failed, ret=%d", ret);
        return;
    }

    ret = snfWifiAdapterRegisterEventCallback(snfWifiTestCallback, NULL);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterRegisterEventCallback failed, ret=%d", ret);
        return;
    }

    ret = snfWifiAdapterApSetConfig(&ap_config);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterApSetConfig failed, ret=%d", ret);
        return;
    }

    /* ret = snfWifiAdapterSetMode(SNF_WIFI_MODE_AP);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterSetMode failed, ret=%d", ret);
        return;
    }

    LOG_I(tag, "snfWifiAdapterTest success"); */

    SnfWifiStaConfig sta_config;
    //int ret = SNF_WIFI_ADAPTER_OK;

    strncpy(sta_config.ssid, "ROG_2G4", sizeof(sta_config.ssid));
    strncpy(sta_config.password, "dokidokideath", sizeof(sta_config.password));

    /* ret = snfWifiAdapterInit();
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterInit failed, ret=%d", ret);
        return;
    }

    ret = snfWifiAdapterRegisterEventCallback(snfWifiTestCallback, NULL);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterRegisterEventCallback failed, ret=%d", ret);
        return;
    } */

    snfWifiAdapterStaSetConfig(&sta_config);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterStaSetConfig failed, ret=%d", ret);
        return;
    }

    ret = snfWifiAdapterSetMode(SNF_WIFI_MODE_AP_STA);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterSetMode failed, ret=%d", ret);
        return;
    }

    ret = snfWifiAdapterStaConnect();
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "snfWifiAdapterStaConnect failed, ret=%d", ret);
        return;
    }

    LOG_I(tag, "snfWifiAdapterTest success");
}


static int wifiApStart(SnfWifiApConfig *config)
{
    int ret = SNF_WIFI_ADAPTER_OK;

    ret = snfWifiAdapterApSetConfig(config);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "ap set config failed, ret=%d", ret);
        return -1;
    }

    ret = snfWifiAdapterSetMode(SNF_WIFI_MODE_AP);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "ap set mode failed, ret=%d", ret);
        return -2;
    }

    return 0;
}

static int wifiStaStart(SnfWifiStaConfig *config)
{
    int ret = SNF_WIFI_ADAPTER_OK;

    ret = snfWifiAdapterStaSetConfig(config);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "sta set config failed, ret=%d", ret);
        return -1;
    }

    ret = snfWifiAdapterSetMode(SNF_WIFI_MODE_STA);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "sta set mode failed, ret=%d", ret);
        return -2;
    }

    ret = snfWifiAdapterStaConnect();
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "sta connect failed, ret=%d", ret);
        return -3;
    }

    return 0;
}

static int wifiIdle(void)
{
    int ret = SNF_WIFI_ADAPTER_OK;

    ret = snfWifiAdapterSetMode(SNF_WIFI_MODE_NONE);
    if(ret != SNF_WIFI_ADAPTER_OK)
    {
        LOG_E(tag, "set mode failed, ret=%d", ret);
        return -1;
    }

    return 0;
}

int snfWifiEventSend(int id, void *data)
{
    SnfWifiManageData *wifi_data = &wifi_manage_data;

    if (!wifi_data->evt_queue) {
        LOG_E(tag, "event queue not init");
        return -1;
    }

    SnfWifiEvent event;
    event.id = id;
    event.data = data;

    if (xQueueSend(wifi_data->evt_queue, (void *)&event, 5) != pdPASS) {
        LOG_E(tag, "event queue send failed");
        return -2;
    }

    return 0;
}

void snfWifiTask(void *arg)
{
    SnfWifiManageData *wifi_data = &wifi_manage_data;
    SnfWifiEvent event;
    int ret = 0;

    while(1)
    {
        memset(&event, 0, sizeof(SnfWifiEvent));

        if (xQueueReceive(wifi_data->evt_queue, (void *)&event, 1000 / portTICK_PERIOD_MS) == pdPASS) {
            LOG_I(tag, "event id: %d", event.id);

            switch (event.id)
            {
                case SNF_WIFI_EVT_SET_TO_AP:
                {
                    if(event.data == NULL)
                    {
                        break;
                    }

                    if(wifi_data->cur_mode == SNF_WIFI_MODE_AP)
                    {
                        LOG_I(tag, "ap has started, will change config restart");
                    }

                    memcpy(&wifi_data->ap_config, (SnfWifiApConfig *)event.data, sizeof(SnfWifiApConfig));

                    ret = wifiApStart(&wifi_data->ap_config);
                    if(ret != 0)
                    {
                        LOG_E(tag, "ap start failed");
                        break;
                    }

                    wifi_data->cur_mode = SNF_WIFI_MODE_AP;
                    break;
                }

                case SNF_WIFI_EVT_SET_TO_STA:
                {
                    if(event.data == NULL)
                    {
                        break;
                    }

                    memcpy(&wifi_data->sta_config, (SnfWifiStaConfig *)event.data, sizeof(SnfWifiStaConfig));

                    ret = wifiStaStart(&wifi_data->sta_config);
                    if(ret != 0)
                    {
                        LOG_E(tag, "sta start failed, ret=%d", ret);
                        break;
                    }

                    wifi_data->cur_mode = SNF_WIFI_MODE_STA;
                    break;
                }

                case SNF_WIFI_EVT_SET_TO_IDLE:
                {
                    ret = wifiIdle();
                    if(ret != 0)
                    {
                        LOG_E(tag, "idle failed, ret=%d", ret);
                        break;
                    }

                    wifi_data->cur_mode = SNF_WIFI_MODE_NONE;
                    wifi_data->cur_status = SNF_WIFI_IDLE;
                    break;
                }

                case SNF_WIFI_EVT_DISCONNECT:
                {
                    if(wifi_data->cur_mode == SNF_WIFI_MODE_STA)
                    {
                        snfWifiAdapterStaDisconnect();
                    }

                    wifi_data->cur_status = SNF_WIFI_DISCONNECTED;
                    break;
                }

                case SNF_WIFI_EVT_SCAN:
                {
                    break;
                }

                default:
                    LOG_I(tag, "unknown event id: %d", event.id);
                    break;
            }
        }
        else 
        {
            /* poll wifi status */
        }



        
    }
}

int snfWifiInit(void)
{
    SnfWifiManageData *wifi_data = &wifi_manage_data;
    if(!wifi_data->task_handle)
    {
        if( xTaskCreate(snfWifiTask,
                        SONOFF_WIFI_TASK_NAME,
                        SONOFF_WIFI_TASK_STACKSIZE,
                        NULL,
                        SONOFF_WIFI_TASK_PRIO,
                        &wifi_data->task_handle) != pdPASS ) 
        {
            LOG_E(tag, "task init failed");
            return -1;
        }
    }

    if (!wifi_data->evt_queue) {
        wifi_data->evt_queue = xQueueCreate(WIFI_EVT_QUEUE_SIZE, sizeof(wifi_event_t));

        if (wifi_data->evt_queue == NULL) {
            LOG_E(tag, "event queue init failed");
            return -1;
        }
    }

    return 0;
}
