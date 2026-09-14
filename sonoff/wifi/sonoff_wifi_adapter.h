/**
 * @file    sonoff_wifi_adapter.h
 * @brief   SDK的WIFI驱动适配模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-24
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_WIFI_ADAPTER_H__
#define __SONOFF_WIFI_ADAPTER_H__

#include <stdint.h>

#include "sonoff_wifi_type.h"

/**
 * @brief WIFI适配模块状态码.
 */
typedef enum {
    SNF_WIFI_ADAPTER_OK = 0,              /**< 成功. */
    SNF_WIFI_ADAPTER_ERR_INVALID_PARAM,   /**< 参数无效. */
    SNF_WIFI_ADAPTER_ERR_NOT_INIT,        /**< 模块未初始化. */
    SNF_WIFI_ADAPTER_ERR_BUSY,            /**< 操作忙. */
    SNF_WIFI_ADAPTER_ERR_TIMEOUT,         /**< 操作超时. */
    SNF_WIFI_ADAPTER_ERR_NOT_SUPPORTED,   /**< 功能不支持. */
    SNF_WIFI_ADAPTER_ERR_INTERNAL,        /**< 内部错误. */
} SnfWifiAdapterErr;

/**
 * @brief WIFI适配模块事件.
 */
typedef enum {
    SNF_WIFI_ADP_EVT_CONNECTED = 0,           /**< STA已连接. */
    SNF_WIFI_ADP_EVT_DISCONNECTED,            /**< STA已断开. */
    SNF_WIFI_ADP_EVT_SCAN_DONE,               /**< 扫描完成. */
    SNF_WIFI_ADP_EVT_AP_STARTED,              /**< AP已启动. */
    SNF_WIFI_ADP_EVT_AP_STOPPED,              /**< AP已停止. */
    SNF_WIFI_ADP_EVT_AP_CLIENT_CONNECTED,     /**< AP客户端已接入. */
    SNF_WIFI_ADP_EVT_AP_CLIENT_DISCONNECTED,  /**< AP客户端已断开. */
} SnfWifiAdapterEvt;

/**
 * @brief 初始化WIFI适配模块.
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterInit(void);

/**
 * @brief 反初始化WIFI适配模块.
 *
 * 当前SDK不支持完整反初始化, 返回NOT_SUPPORTED且保留资源.
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterDeinit(void);

/**
 * @brief 启动STA接口, 不发起连接, 不操作AP.
 *
 * 配置和启停由WIFI管理任务串行调用, 适配器不决定工作模式.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaStart(void);

/**
 * @brief 停止STA接口, 不操作AP.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaStop(void);

/**
 * @brief 启动已配置的AP接口, 不操作STA.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterApStart(void);

/**
 * @brief 停止AP接口, 不操作STA.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterApStop(void);

/**
 * @brief 查询SDK的STA接口启动标志.
 * @return 1表示已启动, 0表示未启动或未初始化.
 */
int snfWifiAdapterStaIsStarted(void);

/**
 * @brief 查询SDK的AP接口启动标志.
 * @return 1表示已启动, 0表示未启动或未初始化.
 */
int snfWifiAdapterApIsStarted(void);

/**
 * @brief 配置STA连接参数.
 *
 * @param [in] config - STA配置参数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaSetConfig(const SnfWifiStaConfig *config);

/**
 * @brief 获取STA连接参数.
 *
 * @param [out] config - STA配置参数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaGetConfig(SnfWifiStaConfig *config);

/**
 * @brief 使用已配置参数连接STA.
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaConnect(void);

/**
 * @brief 断开STA与当前AP的连接.
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaDisconnect(void);

/**
 * @brief 获取当前STA连接信息.
 *
 * STA未连接时返回SNF_WIFI_ADAPTER_ERR_NOT_INIT.
 *
 * @param [out] info - STA连接信息.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaGetLinkInfo(SnfWifiLinkInfo *info);

/**
 * @brief 获取当前STA信号强度.
 *
 * @param [out] rssi - RSSI值, 单位为dBm.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaGetRssi(int *rssi);

/**
 * @brief 启动异步WIFI扫描.
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterScan(void);

/**
 * @brief 获取最近一次WIFI扫描结果.
 *
 * @param [out] results - 扫描结果数组.
 * @param [in] max_count - results可容纳的最大结果数.
 * @param [out] result_count - 实际写入results的结果数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count, uint16_t *result_count);

/**
 * @brief 配置软件AP.
 *
 * @param [in] config - AP配置参数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterApSetConfig(const SnfWifiApConfig *config);

/**
 * @brief 注册应用事件回调.
 *
 * @param [in] callback - WIFI适配模块事件回调.
 * @param [in] user_data - 传递给回调函数的用户数据.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterRegisterEventCallback(SnfWifiEventCB callback, void *user_data);

#endif /* #ifndef __SONOFF_WIFI_ADAPTER_H__ */
