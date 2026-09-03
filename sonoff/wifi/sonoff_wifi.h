/**
 * @file    sonoff_wifi.h
 * @brief   WIFI状态管理模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-25
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_WIFI_H__
#define __SONOFF_WIFI_H__

#include <stdint.h>

#include "sonoff_wifi_type.h"

/**
 * @brief WIFI管理任务外部事件.
 */
typedef enum {
    SNF_WIFI_EVT_STA_CONNECTING = 0,
    SNF_WIFI_EVT_STA_CONNECTED,
    SNF_WIFI_EVT_STA_CONNECT_FAILED,
    SNF_WIFI_EVT_STA_DISCONNECTED,

    SNF_WIFI_EVT_AP_STARTED,
    SNF_WIFI_EVT_AP_START_FAILED,
    SNF_WIFI_EVT_AP_STOPPED,

    SNF_WIFI_EVT_SCAN_DONE,
    SNF_WIFI_EVT_SCAN_FAILED,
} SnfWifiExternalEventId;

/**
 * @brief WIFI管理任务工作模式.
 */
typedef enum {
    SNF_WIFI_MANAGE_MODE_IDLE = 0,
    SNF_WIFI_MANAGE_MODE_STA,
    SNF_WIFI_MANAGE_MODE_AP,
} SnfWifiManageMode;

/**
 * @brief WIFI链路状态.
 */
typedef enum {
    SNF_WIFI_LINK_IDLE,
    SNF_WIFI_LINK_CONNECTING,
    SNF_WIFI_LINK_CONNECTED,
    SNF_WIFI_LINK_DISCONNECTING,
    SNF_WIFI_LINK_DISCONNECTED,
} SnfWifiLinkStatus;

/**
 * @brief 获取当前WIFI工作模式.
 *
 * @return 当前WIFI工作模式.
 */
int snfWifiGetMode(void);

/**
 * @brief 异步请求连接STA.
 *
 * @param [in] config - STA配置参数.
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiStaConnect(const SnfWifiStaConfig *config);

/**
 * @brief 异步请求断开STA.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiStaDisconnect(void);

/**
 * @brief 获取当前STA连接信息.
 *
 * @param [out] info - STA连接信息.
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiStaGetLinkInfo(SnfWifiLinkInfo *info);

/**
 * @brief 获取当前STA信号强度.
 *
 * @param [out] rssi - RSSI值, 单位为dBm.
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiStaGetRssi(int *rssi);

/**
 * @brief 异步请求启动AP.
 *
 * @param [in] config - AP配置参数.
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiApStart(const SnfWifiApConfig *config);

/**
 * @brief 异步请求停止AP并进入IDLE模式.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiApStop(void);

/**
 * @brief 异步请求启动WIFI扫描.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiScan(void);

/**
 * @brief 获取当前扫描状态.
 *
 * @return 0表示未扫描, 1表示扫描中.
 */
int snfWifiScanStatus(void);

/**
 * @brief 获取最近一次扫描结果.
 *
 * @param [out] results - 扫描结果数组.
 * @param [in] max_count - results可容纳的最大结果数.
 * @param [out] result_count - 实际写入的结果数.
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count, uint16_t *result_count);

/**
 * @brief 获取当前WIFI链路状态.
 *
 * @return 当前WIFI链路状态.
 */
int snfWifiGetLinkStatus(void);

/**
 * @brief 初始化WIFI状态管理任务.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiInit(void);

/**
 * @brief 注册应用事件回调
 *
 * @param [in] callback - WIFI管理模块事件回调.
 * @param [in] user_data - 传递给回调函数的用户数据.
 * @return WIFI管理模块状态码.
 */
int snfWifiRegisterEventCallback(SnfWifiEventCB callback, void *user_data);

#endif /* __SONOFF_WIFI_H__ */
