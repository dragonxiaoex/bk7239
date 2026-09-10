/**
 * @file    sonoff_net_adapter.c
 * @brief   SDK的网络管理适配器模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026 深圳松诺技术有限公司
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <common/bk_err.h>
#include <components/event.h>
#include <components/netif.h>
#include <components/netif_types.h>
#include <components/system.h>
#include <lwip/inet.h>

#if defined(CONFIG_IPV6) && CONFIG_IPV6
#include <lwip/ip6_addr.h>
#include <lwip/netif.h>
#include "net.h"
#endif

#include "sonoff_net_adapter.h"

typedef struct {
    int initialized;                       /* 适配器初始化状态 */
    SnfNetAdapterEventCB event_callback;   /* 适配器事件回调 */
} SnfNetAdapterState;

static SnfNetAdapterState net_adapter_state = {
    .initialized = 0,
    .event_callback = NULL,
};

static bk_err_t snfNetAdapterSdkEventCallback(void *arg,
                                               event_module_t event_module,
                                               int event_id,
                                               void *event_data)
{
    SnfNetAdapterState *adp_state = &net_adapter_state;
    netif_event_got_ip4_t *got_ip = (netif_event_got_ip4_t *)event_data;
    uint32_t ip = 0;

    (void)arg;

    if ((event_module != EVENT_MOD_NETIF) || (adp_state->event_callback == NULL))
    {
        return BK_OK;
    }

    if ((event_id == EVENT_NETIF_GOT_IP4) && (got_ip != NULL)
        && (got_ip->netif_if == NETIF_IF_STA))
    {
        ip = inet_addr(got_ip->ip);
        adp_state->event_callback(SNF_NET_ADAPTER_EVT_GOT_IP, &ip);
    }
    else if ((event_id == EVENT_NETIF_DHCP_TIMEOUT) && (got_ip != NULL)
             && (got_ip->netif_if == NETIF_IF_STA))
    {
        adp_state->event_callback(SNF_NET_ADAPTER_EVT_LOST_IP, NULL);
    }

    return BK_OK;
}

int snfNetAdapterRegisterEventCallback(SnfNetAdapterEventCB callback)
{
    SnfNetAdapterState *adp_state = &net_adapter_state;

    if (callback == NULL)
    {
        return -1;
    }

    adp_state->event_callback = callback;

    return 0;
}

int snfNetAdapterInit(void)
{
    SnfNetAdapterState *adp_state = &net_adapter_state;

    if (adp_state->initialized != 0)
    {
        return 0;
    }

    if (bk_event_register_cb(EVENT_MOD_NETIF, EVENT_ID_ALL,
                             snfNetAdapterSdkEventCallback, NULL) != BK_OK)
    {
        return -1;
    }

    adp_state->initialized = 1;

    return 0;
}

static netif_if_t snfNetAdapterGetSdkInterface(SnfNetAdapterIf interface)
{
    if (interface == SNF_NET_ADAPTER_IF_STA)
    {
        return NETIF_IF_STA;
    }

    if (interface == SNF_NET_ADAPTER_IF_AP)
    {
        return NETIF_IF_AP;
    }

    return NETIF_IF_INVALID;
}

int snfNetAdapterGetIpv4(SnfNetAdapterIf interface, uint32_t *ip)
{
    netif_ip4_config_t config = {0};
    netif_if_t sdk_interface;

    if (ip == NULL)
    {
        return -1;
    }

    sdk_interface = snfNetAdapterGetSdkInterface(interface);
    if (sdk_interface == NETIF_IF_INVALID)
    {
        return -1;
    }

    if (bk_netif_get_ip4_config(sdk_interface, &config) != BK_OK)
    {
        return -1;
    }

    *ip = inet_addr(config.ip);
    if (*ip == 0U)
    {
        return -1;
    }

    return 0;
}

int snfNetAdapterGetIpv6(SnfNetAdapterIf interface, uint8_t *ip)
{
#if defined(CONFIG_IPV6) && CONFIG_IPV6
    struct netif *netif;
    netif_if_t sdk_interface;
    int index;

    if (ip == NULL)
    {
        return -1;
    }

    sdk_interface = snfNetAdapterGetSdkInterface(interface);
    if (sdk_interface == NETIF_IF_INVALID)
    {
        return -1;
    }

    if (sdk_interface == NETIF_IF_STA)
    {
        netif = (struct netif *)net_get_sta_handle();
    }
    else
    {
        netif = (struct netif *)net_get_uap_handle();
    }

    if (netif == NULL)
    {
        return -1;
    }

    for (index = 0; index < LWIP_IPV6_NUM_ADDRESSES; index++)
    {
        if (ip6_addr_isvalid(netif_ip6_addr_state(netif, index)))
        {
            memcpy(ip, netif_ip6_addr(netif, index)->addr, 16U);

            return 0;
        }
    }

    return -2;
#else
    (void)interface;
    (void)ip;

    return -1;
#endif
}

int snfNetAdapterGetMac(SnfNetAdapterIf interface, uint8_t *mac)
{
    mac_type_t mac_type;

    if (mac == NULL)
    {
        return -1;
    }

    if (interface == SNF_NET_ADAPTER_IF_STA)
    {
        mac_type = MAC_TYPE_STA;
    }
    else if (interface == SNF_NET_ADAPTER_IF_AP)
    {
        mac_type = MAC_TYPE_AP;
    }
    else
    {
        return -1;
    }

    if (bk_get_mac(mac, mac_type) != BK_OK)
    {
        return -1;
    }

    return 0;
}
