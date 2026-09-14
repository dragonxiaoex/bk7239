/**
 * @file    sonoff_net.c
 * @brief   网络管理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include "sonoff_net.h"
#include "sonoff_log.h"
#include "sonoff_net_adapter.h"
#include "sonoff_wifi.h"

static const char *tag = "SNF-NET";

/** @brief AP静态IPv4配置. */
#define SNF_NET_AP_IP_ADDR              "192.168.100.1"     /* IPv4地址 */
#define SNF_NET_AP_NETMASK              "255.255.255.0"     /* 子网掩码 */
#define SNF_NET_AP_GATEWAY              "192.168.100.1"     /* 网关 */
#define SNF_NET_AP_DNS                  "192.168.100.1"     /* DNS */

/** @brief NET仅保存各接口的IP阶段, 无线模式和链路状态向WIFI查询. */
typedef struct
{
    SnfNetState state[2];                       /* 仅使用IDLE、WAIT_IP、READY */
    uint32_t ipv4[2];                           /* 各接口IPv4地址, 网络字节序 */
    SemaphoreHandle_t mutex;                    /* 网络状态互斥锁 */
    SnfNetEventCB event_callback;               /* 兼容旧应用的状态回调 */
    SnfNetNotifyCB notify_callback;             /* 带接口标识的统一通知 */
    void *notify_user_data;                     /* 统一通知用户数据 */
    int32_t init;                               /* 初始化状态 */
} SnfNetManageState;

static const SnfNetAdapterIpv4Config ap_ip_config = {
    .ip = SNF_NET_AP_IP_ADDR,
    .mask = SNF_NET_AP_NETMASK,
    .gateway = SNF_NET_AP_GATEWAY,
    .dns = SNF_NET_AP_DNS,
};

static SnfNetManageState net_manage_state = {0};

/**
 * @brief 获取网络状态锁.
 *
 * @return 0表示成功, 负数表示锁未创建或获取失败.
 */
static int mutexTake(void)
{
    SnfNetManageState *net_state = &net_manage_state;

    if ((net_state->mutex == NULL)
        || (xSemaphoreTake(net_state->mutex, portMAX_DELAY) != pdPASS))
    {
        return -1;
    }

    return 0;
}

/** @brief 释放已获取的网络状态锁. */
static void mutexGive(void)
{
    xSemaphoreGive(net_manage_state.mutex);
}

/**
 * @brief 在网络锁外转发统一通知, 事件数据由调用栈持有.
 * @param [in] event - 网络或无线事件.
 */
static void eventNotify(const SnfNetEvent *event)
{
    SnfNetManageState *net_state = &net_manage_state;
    SnfNetNotifyCB callback;
    void *user_data;

    if (mutexTake() != 0)
    {
        return;
    }
    callback = net_state->notify_callback;
    user_data = net_state->notify_user_data;
    mutexGive();
    if (callback != NULL)
    {
        callback(event, user_data);
    }
}

/**
 * @brief 更新单个接口的IP阶段, 无线过程状态由查询接口即时映射.
 * @param [in] interface - 所属接口.
 * @param [in] state - IDLE、WAIT_IP或READY.
 * @param [in] ipv4 - READY时的IPv4地址, 其他阶段传0.
 */
static void stateNotify(SnfNetInterface interface, SnfNetState state, uint32_t ipv4)
{
    SnfNetManageState *net_state = &net_manage_state;
    SnfNetEvent event = {.interface = interface};
    SnfNetEventCB callback;
    uint32_t previous_ip;
    uint32_t callback_ip;

    if (mutexTake() != 0)
    {
        return;
    }
    previous_ip = net_state->ipv4[interface];
    /* 等待IP期间仍会发生连接/断连，允许WAIT_IP重复进入 */
    if ((net_state->state[interface] == state) && (previous_ip == ipv4)
        && (state != SNF_NET_STATE_WAIT_IP))
    {
        mutexGive();
        return;
    }
    net_state->state[interface] = state;
    net_state->ipv4[interface] = ipv4;
    callback = net_state->event_callback;
    callback_ip = net_state->ipv4[SNF_NET_IF_STA];
    mutexGive();

    if ((previous_ip != 0) && (ipv4 == 0))
    {
        event.id = SNF_NET_EVT_IP_LOST;
        event.data = &previous_ip;
        eventNotify(&event);
    }
    else if ((ipv4 != 0) && (previous_ip != ipv4))
    {
        event.id = SNF_NET_EVT_IP_READY;
        event.data = &ipv4;
        eventNotify(&event);
    }
    else
    {
        /* IP未变化, 仅更新网络准备阶段. */
    }

    if (callback != NULL)
    {
        state = snfNetGetInterfaceState(interface);
        if (state == SNF_NET_STATE_IDLE)
        {
            state = snfNetGetState();
        }
        callback(state, (state == SNF_NET_STATE_READY) ? &callback_ip : NULL);
    }
}

/**
 * @brief 响应链路变化管理IP可用性, 并原样转发无线事件.
 * @param [in] event - WIFI管理事件.
 * @param [in] event_data - WIFI事件数据.
 * @param [in] user_data - NET管理对象.
 */
static void wifiEventCallback(int event, const void *event_data, void *user_data)
{
    const SnfNetManageState *net_state = user_data;
    const SnfWifiStaDisconnectedEvent *disconnected = event_data;
    SnfNetEvent notification = {
        .id = SNF_NET_EVT_WIFI,
        .interface = SNF_NET_IF_STA,
        .wifi_event = event,
        .data = event_data,
    };
    uint32_t ipv4 = 0;
    int mode;
    int link_status;

    if (net_state->init == 0)
    {
        return;
    }

    switch (event)
    {
        case SNF_WIFI_EVT_STA_CONNECTING:
            stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_WAIT_IP, 0);
            break;
        case SNF_WIFI_EVT_STA_CONNECTED:
            /* IP事件可能先到, 不把已就绪的网络退回等待IP. */
            if (snfNetGetInterfaceState(SNF_NET_IF_STA) != SNF_NET_STATE_READY)
            {
                stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_WAIT_IP, 0);
            }
            break;
        case SNF_WIFI_EVT_STA_CONNECT_FAILED:
            link_status = snfWifiGetLinkStatus();
            if (link_status == SNF_WIFI_LINK_IDLE)
            {
                stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_IDLE, 0);
            }
            else if (link_status == SNF_WIFI_LINK_DISCONNECTED)
            {
                stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_WAIT_IP, 0);
            }
            else
            {
                /* 配置被拒绝但原链路仍连接时保留原IP. */
            }
            break;
        case SNF_WIFI_EVT_STA_DISCONNECTED:
            stateNotify(SNF_NET_IF_STA,
                        ((disconnected != NULL) && (disconnected->reason == SNF_WIFI_DISCONNECT_REASON_USER))
                        ? SNF_NET_STATE_IDLE : SNF_NET_STATE_WAIT_IP, 0);
            break;
        case SNF_WIFI_EVT_STA_DISCONNECTING:
        case SNF_WIFI_EVT_STA_STOPPED:
            stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_IDLE, 0);
            break;
        case SNF_WIFI_EVT_AP_STARTING:
            notification.interface = SNF_NET_IF_AP;
            if (snfNetGetInterfaceState(SNF_NET_IF_AP) != SNF_NET_STATE_AP_READY)
            {
                stateNotify(SNF_NET_IF_AP, SNF_NET_STATE_WAIT_IP, 0);
            }
            break;
        case SNF_WIFI_EVT_AP_STARTED:
        case SNF_WIFI_EVT_AP_START_FAILED:
            notification.interface = SNF_NET_IF_AP;
            mode = snfWifiGetMode();
            if ((mode == SNF_WIFI_MANAGE_MODE_AP) || (mode == SNF_WIFI_MANAGE_MODE_AP_STA))
            {
                if (snfNetAdapterGetIpv4(SNF_NET_ADAPTER_IF_AP, &ipv4) != 0)
                {
                    ipv4 = 0;
                }
                stateNotify(SNF_NET_IF_AP,
                            (ipv4 != 0) ? SNF_NET_STATE_READY : SNF_NET_STATE_WAIT_IP, ipv4);
            }
            else
            {
                stateNotify(SNF_NET_IF_AP, SNF_NET_STATE_IDLE, 0);
            }
            break;
        case SNF_WIFI_EVT_AP_STOPPED:
            notification.interface = SNF_NET_IF_AP;
            stateNotify(SNF_NET_IF_AP, SNF_NET_STATE_IDLE, 0);
            break;
        case SNF_WIFI_EVT_AP_STOP_FAILED:
            notification.interface = SNF_NET_IF_AP;
            break;
        case SNF_WIFI_EVT_MODE_CHANGED:
            notification.interface = SNF_NET_IF_NONE;
            mode = snfWifiGetMode();
            if ((mode == SNF_WIFI_MANAGE_MODE_IDLE) || (mode == SNF_WIFI_MANAGE_MODE_AP))
            {
                stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_IDLE, 0);
            }
            if ((mode == SNF_WIFI_MANAGE_MODE_IDLE) || (mode == SNF_WIFI_MANAGE_MODE_STA))
            {
                stateNotify(SNF_NET_IF_AP, SNF_NET_STATE_IDLE, 0);
            }
            break;
        case SNF_WIFI_EVT_SCAN_DONE:
        case SNF_WIFI_EVT_SCAN_FAILED:
            notification.interface = SNF_NET_IF_NONE;
            break;
        default:
            break;
    }

    eventNotify(&notification);
}

/**
 * @brief 接收STA的IP事件, 通过实际链路校验迟到的IP通知.
 * @param [in] event - 网络适配事件.
 * @param [in] event_data - 取得的IPv4地址或NULL.
 */
static void adapterEventCallback(SnfNetAdapterEvt event, const void *event_data)
{
    SnfWifiLinkInfo link_info = {0};

    if ((net_manage_state.init == 0) || (snfWifiStaGetLinkInfo(&link_info) != 0))
    {
        return;
    }
    if ((event == SNF_NET_ADAPTER_EVT_GOT_IP) && (event_data != NULL)
        && (*(const uint32_t *)event_data != 0))
    {
        stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_READY, *(const uint32_t *)event_data);
    }
    else if (event == SNF_NET_ADAPTER_EVT_LOST_IP)
    {
        stateNotify(SNF_NET_IF_STA, SNF_NET_STATE_WAIT_IP, 0);
    }
    else
    {
        /* 忽略不完整的IP事件. */
    }
}

/**
 * @brief 选择旧地址查询接口, 共存时优先STA.
 *
 * @return 当前默认接口.
 */
static SnfNetInterface currentInterface(void)
{
    int mode = snfWifiGetMode();

    if ((mode == SNF_WIFI_MANAGE_MODE_IDLE) || (mode == SNF_WIFI_MANAGE_MODE_AP))
    {
        if (snfNetGetInterfaceState(SNF_NET_IF_AP) != SNF_NET_STATE_IDLE)
        {
            return SNF_NET_IF_AP;
        }
    }

    return SNF_NET_IF_STA;
}

/**
 * @brief 将公开接口标识转换为适配层标识.
 *
 * @param [in] interface - 网络接口.
 * @return 对应适配接口, 参数无效时返回-1.
 */
static int adapterInterface(SnfNetInterface interface)
{
    if (interface == SNF_NET_IF_STA)
    {
        return SNF_NET_ADAPTER_IF_STA;
    }
    if (interface == SNF_NET_IF_AP)
    {
        return SNF_NET_ADAPTER_IF_AP;
    }

    return -1;
}

int snfNetStaConnect(const SnfWifiStaConfig *config)
{
    const SnfNetManageState *net_state = &net_manage_state;

    if ((net_state->init == 0) || (config == NULL))
    {
        return -1;
    }

    return snfWifiStaConnect(config);
}

int snfNetStaDisconnect(void)
{
    if (net_manage_state.init == 0)
    {
        return -1;
    }

    return snfWifiStaDisconnect();
}

int snfNetStaStop(void)
{
    if (net_manage_state.init == 0)
    {
        return -1;
    }

    return snfWifiStaStop();
}

int snfNetApStart(const SnfWifiApConfig *config)
{
    const SnfNetManageState *net_state = &net_manage_state;

    if ((net_state->init == 0) || (config == NULL))
    {
        return -1;
    }

    return snfWifiApStart(config);
}

int snfNetApStop(void)
{
    if (net_manage_state.init == 0)
    {
        return -1;
    }

    return snfWifiApStop();
}

int snfNetSetIdle(void)
{
    if (net_manage_state.init == 0)
    {
        return -1;
    }

    return snfWifiSetIdle();
}

int snfNetRegisterEventCallback(SnfNetEventCB callback)
{
    SnfNetManageState *net_state = &net_manage_state;

    if (callback == NULL)
    {
        return -1;
    }
    if (net_state->mutex != NULL)
    {
        if (mutexTake() != 0)
        {
            return -1;
        }
        net_state->event_callback = callback;
        mutexGive();

        return 0;
    }

    net_state->event_callback = callback;

    return 0;
}

int snfNetScan(void)
{
    if (net_manage_state.init == 0)
    {
        return -1;
    }

    return snfWifiScan();
}

int snfNetGetState(void)
{
    int state = snfNetGetInterfaceState(SNF_NET_IF_STA);

    return (state != SNF_NET_STATE_IDLE) ? state : snfNetGetInterfaceState(SNF_NET_IF_AP);
}

int snfNetGetInterfaceState(SnfNetInterface interface)
{
    SnfNetManageState *net_state = &net_manage_state;
    SnfNetState state;
    int link_status;
    int mode = snfWifiGetMode();

    if ((interface != SNF_NET_IF_STA) && (interface != SNF_NET_IF_AP))
    {
        return -1;
    }
    if (mutexTake() != 0)
    {
        return SNF_NET_STATE_IDLE;
    }
    state = net_state->state[interface];
    mutexGive();

    if (interface == SNF_NET_IF_AP)
    {
        if (state == SNF_NET_STATE_WAIT_IP)
        {
            return SNF_NET_STATE_AP_STARTING;
        }
        if ((mode == SNF_WIFI_MANAGE_MODE_AP) || (mode == SNF_WIFI_MANAGE_MODE_AP_STA))
        {
            return (state == SNF_NET_STATE_READY) ? SNF_NET_STATE_AP_READY : state;
        }
        return SNF_NET_STATE_IDLE;
    }
    if ((mode == SNF_WIFI_MANAGE_MODE_IDLE) || (mode == SNF_WIFI_MANAGE_MODE_AP))
    {
        return SNF_NET_STATE_IDLE;
    }
    if (state == SNF_NET_STATE_WAIT_IP)
    {
        link_status = snfWifiGetLinkStatus();
        if (link_status == SNF_WIFI_LINK_CONNECTING)
        {
            return SNF_NET_STATE_CONNECTING;
        }
        if (link_status == SNF_WIFI_LINK_DISCONNECTED)
        {
            return SNF_NET_STATE_RECONNECT_WAIT;
        }
    }

    return state;
}

int snfNetRegisterNotifyCallback(SnfNetNotifyCB callback, void *user_data)
{
    SnfNetManageState *net_state = &net_manage_state;

    if (callback == NULL)
    {
        return -1;
    }

    if ((net_state->mutex != NULL)
        && (mutexTake() != 0))
    {
        return -1;
    }

    net_state->notify_callback = callback;
    net_state->notify_user_data = user_data;
    if (net_state->mutex != NULL)
    {
        mutexGive();
    }

    return 0;
}

int snfNetGetWifiMode(void)
{
    return (net_manage_state.init != 0) ? snfWifiGetMode() : SNF_WIFI_MANAGE_MODE_IDLE;
}

int snfNetGetWifiLinkStatus(void)
{
    return (net_manage_state.init != 0) ? snfWifiGetLinkStatus() : SNF_WIFI_LINK_IDLE;
}

int snfNetStaGetLinkInfo(SnfWifiLinkInfo *info)
{
    return (net_manage_state.init != 0) ? snfWifiStaGetLinkInfo(info) : -1;
}

int snfNetStaGetRssi(int *rssi)
{
    return (net_manage_state.init != 0) ? snfWifiStaGetRssi(rssi) : -1;
}

int snfNetScanStatus(void)
{
    return (net_manage_state.init != 0) ? snfWifiScanStatus() : 0;
}

int snfNetScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count, uint16_t *result_count)
{
    return (net_manage_state.init != 0) ? snfWifiScanGetResults(results, max_count, result_count) : -1;
}

int snfNetGetIpv4(uint32_t *ip)
{
    return snfNetGetInterfaceIpv4(currentInterface(), ip);
}

int snfNetGetIpv6(uint8_t *ip)
{
    return snfNetGetInterfaceIpv6(currentInterface(), ip);
}

int snfNetGetMac(uint8_t *mac)
{
    return snfNetGetInterfaceMac(currentInterface(), mac);
}

int snfNetGetInterfaceIpv4(SnfNetInterface interface, uint32_t *ip)
{
    const SnfNetManageState *net_state = &net_manage_state;
    int adapter_interface = adapterInterface(interface);

    if ((net_state->init == 0) || (ip == NULL) || (adapter_interface < 0))
    {
        return -1;
    }

    return snfNetAdapterGetIpv4((SnfNetAdapterIf)adapter_interface, ip);
}

int snfNetGetInterfaceIpv6(SnfNetInterface interface, uint8_t *ip)
{
    const SnfNetManageState *net_state = &net_manage_state;
    int adapter_interface = adapterInterface(interface);

    if ((net_state->init == 0) || (ip == NULL) || (adapter_interface < 0))
    {
        return -1;
    }

    return snfNetAdapterGetIpv6((SnfNetAdapterIf)adapter_interface, ip);
}

int snfNetGetInterfaceMac(SnfNetInterface interface, uint8_t *mac)
{
    const SnfNetManageState *net_state = &net_manage_state;
    int adapter_interface = adapterInterface(interface);

    if ((net_state->init == 0) || (mac == NULL) || (adapter_interface < 0))
    {
        return -1;
    }

    return snfNetAdapterGetMac((SnfNetAdapterIf)adapter_interface, mac);
}

int snfNetInit(void)
{
    SnfNetManageState *net_state = &net_manage_state;
    int ret;

    if (net_state->init != 0)
    {
        return 0;
    }

    if (net_state->mutex == NULL)
    {
        net_state->mutex = xSemaphoreCreateMutex();
        if (net_state->mutex == NULL)
        {
            return -1;
        }
    }

    ret = snfNetAdapterInit();
    if (ret != 0)
    {
        return -1;
    }

    ret = snfNetAdapterRegisterEventCallback(adapterEventCallback);
    if (ret != 0)
    {
        return -2;
    }

    ret = snfWifiInit();
    if (ret != 0)
    {
        return -3;
    }

    ret = snfNetAdapterSetIpv4Config(SNF_NET_ADAPTER_IF_AP, &ap_ip_config);
    if (ret != 0)
    {
        return -4;
    }

    ret = snfWifiRegisterEventCallback(wifiEventCallback, net_state);
    if (ret != 0)
    {
        return -4;
    }

    net_state->init = 1;
    LOG_I(tag, "network manager initialized");

    return 0;
}
