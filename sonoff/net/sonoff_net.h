/**
 * @file    sonoff_net.h
 * @brief   网络管理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026 深圳松诺技术有限公司
 */
#ifndef __SONOFF_NET_H__
#define __SONOFF_NET_H__

#include <stdint.h>

#include "sonoff_wifi_type.h"

typedef enum {
    SNF_NET_STATE_IDLE = 0,        /* 网络未启用 */
    SNF_NET_STATE_CONNECTING,      /* 正在建立网络 */
    SNF_NET_STATE_WAIT_IP,         /* WiFi已连接，等待IP */
    SNF_NET_STATE_READY,           /* IP网络可用 */
    SNF_NET_STATE_RECONNECT_WAIT,  /* 等待重新连接 */
    SNF_NET_STATE_AP_STARTING,     /* 正在启动AP */
    SNF_NET_STATE_AP_READY,        /* AP已启动 */
} SnfNetState;

/**
 * @brief 网络状态变化回调.
 *
 * READY状态的event_data指向网络管理模块保存的IPv4地址，地址为网络字节序，
 * 仅保证在回调执行期间有效；其他状态的event_data为NULL.
 */
typedef void (*SnfNetEventCB)(SnfNetState state, const void *event_data);

/**
 * @brief 异步请求连接STA.
 *
 * @param [in] config - STA配置参数.
 * @return 0表示请求成功, 负数表示失败.
 */
int snfNetStaConnect(const SnfWifiStaConfig *config);

/**
 * @brief 异步请求断开STA.
 *
 * @return 0表示请求成功, 负数表示失败.
 */
int snfNetStaDisconnect(void);

/**
 * @brief 异步请求启动AP.
 *
 * @param [in] config - AP配置参数.
 * @return 0表示请求成功, 负数表示失败.
 */
int snfNetApStart(const SnfWifiApConfig *config);

/**
 * @brief 异步请求停止AP.
 *
 * @return 0表示请求成功, 负数表示失败.
 */
int snfNetApStop(void);

/**
 * @brief 注册网络状态回调.
 *
 * @param [in] callback - 网络状态回调.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetRegisterEventCallback(SnfNetEventCB callback);

/**
 * @brief 异步请求扫描WiFi.
 *
 * @return 0表示请求成功, 负数表示失败.
 */
int snfNetScan(void);

/**
 * @brief 获取当前网络状态.
 *
 * @return 当前网络状态.
 */
int snfNetGetState(void);

/**
 * @brief 获取当前网络接口的IPv4地址.
 *
 * @param [out] ip - IPv4地址, 网络字节序.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetIpv4(uint32_t *ip);

/**
 * @brief 获取当前网络接口的IPv6地址.
 *
 * @param [out] ip - 16字节IPv6地址.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetIpv6(uint8_t *ip);

/**
 * @brief 获取当前网络接口的MAC地址.
 *
 * @param [out] mac - 6字节MAC地址.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetMac(uint8_t *mac);

/**
 * @brief 初始化网络管理模块.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfNetInit(void);

#endif /* __SONOFF_NET_H__ */
