/**
 * @file    sonoff_net.c
 * @brief   网络管理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026 深圳松诺技术有限公司
 */
#include <stddef.h>
#include <stdint.h>

#include "sonoff_net.h"
#include "sonoff_log.h"
#include "sonoff_net_adapter.h"
#include "sonoff_wifi.h"

static const char *tag = "SNF-NET";

typedef struct {
    SnfNetState state;                   /* 当前网络状态 */
    uint32_t ipv4;                       /* 当前IPv4地址, 网络字节序 */
    SnfNetEventCB event_callback;        /* 网络状态事件回调 */
    int init;                     /* 网络管理模块初始化状态 */
} SnfNetManageState;

static SnfNetManageState net_manage_state = {
    .state = SNF_NET_STATE_IDLE,
    .ipv4 = 0,
    .event_callback = NULL,
    .init = 0,
};

static void snfNetNotifyState(SnfNetState state, const void *event_data)
{
    SnfNetManageState *net_state = &net_manage_state;
    SnfNetEventCB callback = net_state->event_callback;

    if (net_state->state == state)
    {
        return;
    }

    net_state->state = state;
    if (callback != NULL)
    {
        callback(state, event_data);
    }
}

static void snfNetWifiEventCallback(int event, const void *event_data, void *user_data)
{
    SnfNetManageState *net_state = &net_manage_state;

    (void)event_data;
    (void)user_data;

    switch (event)
    {
        case SNF_WIFI_EVT_STA_CONNECTING:
            if ((net_state->state != SNF_NET_STATE_IDLE)
                && (net_state->state != SNF_NET_STATE_AP_STARTING)
                && (net_state->state != SNF_NET_STATE_AP_READY))
            {
                snfNetNotifyState(SNF_NET_STATE_CONNECTING, NULL);
            }
            break;

        case SNF_WIFI_EVT_STA_CONNECTED:
            if ((net_state->state != SNF_NET_STATE_IDLE)
                && (net_state->state != SNF_NET_STATE_AP_STARTING)
                && (net_state->state != SNF_NET_STATE_AP_READY)
                && (net_state->state != SNF_NET_STATE_READY))
            {
                snfNetNotifyState(SNF_NET_STATE_WAIT_IP, NULL);
            }
            break;

        case SNF_WIFI_EVT_STA_CONNECT_FAILED:
        case SNF_WIFI_EVT_STA_DISCONNECTED:
            if ((net_state->state == SNF_NET_STATE_IDLE)
                || (net_state->state == SNF_NET_STATE_AP_STARTING)
                || (net_state->state == SNF_NET_STATE_AP_READY))
            {
                break;
            }

            net_state->ipv4 = 0;
            snfNetNotifyState(SNF_NET_STATE_RECONNECT_WAIT, NULL);
            break;

        case SNF_WIFI_EVT_AP_STARTED:
            if (snfWifiGetMode() == SNF_WIFI_MANAGE_MODE_AP)
            {
                snfNetNotifyState(SNF_NET_STATE_AP_READY, NULL);
            }
            break;

        case SNF_WIFI_EVT_AP_START_FAILED:
        case SNF_WIFI_EVT_AP_STOPPED:
            if ((net_state->state == SNF_NET_STATE_AP_STARTING)
                || (net_state->state == SNF_NET_STATE_AP_READY))
            {
                snfNetNotifyState(SNF_NET_STATE_IDLE, NULL);
            }
            break;

        default:
            break;
    }
}

static void snfNetAdapterEventCallback(SnfNetAdapterEvt event, const void *event_data)
{
    SnfNetManageState *net_state = &net_manage_state;

    if (event == SNF_NET_ADAPTER_EVT_GOT_IP)
    {
        if ((net_state->state == SNF_NET_STATE_IDLE)
            || (net_state->state == SNF_NET_STATE_RECONNECT_WAIT)
            || (net_state->state == SNF_NET_STATE_AP_STARTING)
            || (net_state->state == SNF_NET_STATE_AP_READY))
        {
            return;
        }

        if (event_data != NULL)
        {
            net_state->ipv4 = *(const uint32_t *)event_data;
        }

        snfNetNotifyState(SNF_NET_STATE_READY, &net_state->ipv4);
    }
    else if (event == SNF_NET_ADAPTER_EVT_LOST_IP)
    {
        if ((net_state->state != SNF_NET_STATE_WAIT_IP)
            && (net_state->state != SNF_NET_STATE_READY))
        {
            return;
        }

        net_state->ipv4 = 0;
        snfNetNotifyState(SNF_NET_STATE_RECONNECT_WAIT, NULL);
    }
}

static SnfNetAdapterIf snfNetGetCurrentInterface(void)
{
    SnfNetManageState *net_state = &net_manage_state;

    if ((net_state->state == SNF_NET_STATE_AP_STARTING)
        || (net_state->state == SNF_NET_STATE_AP_READY))
    {
        return SNF_NET_ADAPTER_IF_AP;
    }

    return SNF_NET_ADAPTER_IF_STA;
}

int snfNetStaConnect(const SnfWifiStaConfig *config)
{
    SnfNetManageState *net_state = &net_manage_state;
    int ret;

    if ((net_state->init == 0) || (config == NULL))
    {
        return -1;
    }

    ret = snfWifiStaConnect(config);
    if (ret != 0)
    {
        return ret;
    }

    net_state->ipv4 = 0;
    snfNetNotifyState(SNF_NET_STATE_CONNECTING, NULL);

    return 0;
}

int snfNetStaDisconnect(void)
{
    SnfNetManageState *net_state = &net_manage_state;
    int ret;

    if (net_state->init == 0)
    {
        return -1;
    }

    ret = snfWifiStaDisconnect();
    if (ret != 0)
    {
        return ret;
    }

    net_state->ipv4 = 0;
    snfNetNotifyState(SNF_NET_STATE_IDLE, NULL);

    return 0;
}

int snfNetApStart(const SnfWifiApConfig *config)
{
    SnfNetManageState *net_state = &net_manage_state;
    int ret;

    if ((net_state->init == 0) || (config == NULL))
    {
        return -1;
    }

    ret = snfWifiApStart(config);
    if (ret != 0)
    {
        return ret;
    }

    net_state->ipv4 = 0;
    snfNetNotifyState(SNF_NET_STATE_AP_STARTING, NULL);

    return 0;
}

int snfNetApStop(void)
{
    SnfNetManageState *net_state = &net_manage_state;
    int ret;

    if (net_state->init == 0)
    {
        return -1;
    }

    ret = snfWifiApStop();
    if (ret != 0)
    {
        return ret;
    }

    net_state->ipv4 = 0;
    snfNetNotifyState(SNF_NET_STATE_IDLE, NULL);

    return 0;
}

int snfNetRegisterEventCallback(SnfNetEventCB callback)
{
    SnfNetManageState *net_state = &net_manage_state;

    if (callback == NULL)
    {
        return -1;
    }

    net_state->event_callback = callback;

    return 0;
}

int snfNetScan(void)
{
    SnfNetManageState *net_state = &net_manage_state;

    if (net_state->init == 0)
    {
        return -1;
    }

    return snfWifiScan();
}

int snfNetGetState(void)
{
    SnfNetManageState *net_state = &net_manage_state;

    return net_state->state;
}

int snfNetGetIpv4(uint32_t *ip)
{
    SnfNetManageState *net_state = &net_manage_state;

    if ((net_state->init == 0) || (ip == NULL))
    {
        return -1;
    }

    return snfNetAdapterGetIpv4(snfNetGetCurrentInterface(), ip);
}

int snfNetGetIpv6(uint8_t *ip)
{
    SnfNetManageState *net_state = &net_manage_state;

    if ((net_state->init == 0) || (ip == NULL))
    {
        return -1;
    }

    return snfNetAdapterGetIpv6(snfNetGetCurrentInterface(), ip);
}

int snfNetGetMac(uint8_t *mac)
{
    SnfNetManageState *net_state = &net_manage_state;

    if ((net_state->init == 0) || (mac == NULL))
    {
        return -1;
    }

    return snfNetAdapterGetMac(snfNetGetCurrentInterface(), mac);
}

int snfNetInit(void)
{
    SnfNetManageState *net_state = &net_manage_state;
    int ret;

    if (net_state->init != 0)
    {
        return 0;
    }

    LOG_I(tag, "network manager init");

    ret = snfNetAdapterInit();
    if (ret != 0)
    {
        LOG_E(tag, "network adapter init failed, ret=%d", ret);
        return -1;
    }

    ret = snfNetAdapterRegisterEventCallback(snfNetAdapterEventCallback);
    if (ret != 0)
    {
        LOG_E(tag, "network adapter callback register failed, ret=%d", ret);
        return -2;
    }

    ret = snfWifiInit();
    if (ret != 0)
    {
        LOG_E(tag, "wifi init failed, ret=%d", ret);
        return -3;
    }

    ret = snfWifiRegisterEventCallback(snfNetWifiEventCallback, NULL);
    if (ret != 0)
    {
        LOG_E(tag, "wifi callback register failed, ret=%d", ret);
        return -4;
    }

    net_state->init = 1;

    return 0;
}
