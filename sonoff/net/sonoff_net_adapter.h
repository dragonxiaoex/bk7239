/**
 * @file    sonoff_net_adapter.h
 * @brief   SDK的网络管理适配器模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026 深圳松诺技术有限公司
 */
#ifndef __SONOFF_NET_ADAPTER_H__
#define __SONOFF_NET_ADAPTER_H__

#include <stdint.h>

typedef enum {
    SNF_NET_ADAPTER_EVT_GOT_IP = 0,
    SNF_NET_ADAPTER_EVT_LOST_IP,
} SnfNetAdapterEvt;

typedef enum {
    SNF_NET_ADAPTER_IF_STA = 0,
    SNF_NET_ADAPTER_IF_AP,
} SnfNetAdapterIf;

typedef void (*SnfNetAdapterEventCB)(SnfNetAdapterEvt event, const void *event_data);

/**
 * @brief 初始化网络适配器.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfNetAdapterInit(void);

/**
 * @brief 注册网络适配器事件回调.
 *
 * @param [in] callback - 适配器事件回调.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetAdapterRegisterEventCallback(SnfNetAdapterEventCB callback);

/**
 * @brief 获取指定网络接口的IPv4地址.
 *
 * @param [in] interface - 网络接口.
 * @param [out] ip - IPv4地址, 网络字节序.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetAdapterGetIpv4(SnfNetAdapterIf interface, uint32_t *ip);

/**
 * @brief 获取指定网络接口的IPv6地址.
 *
 * @param [in] interface - 网络接口.
 * @param [out] ip - 16字节IPv6地址.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetAdapterGetIpv6(SnfNetAdapterIf interface, uint8_t *ip);

/**
 * @brief 获取指定网络接口的MAC地址.
 *
 * @param [in] interface - 网络接口.
 * @param [out] mac - 6字节MAC地址.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetAdapterGetMac(SnfNetAdapterIf interface, uint8_t *mac);

#endif /* __SONOFF_NET_ADAPTER_H__ */
