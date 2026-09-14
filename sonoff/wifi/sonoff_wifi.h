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
 * @brief 获取当前WIFI工作模式.
 *
 * 模式表示接口启用组合, STA断连不退出STA模式; 不包含尚未执行的请求.
 *
 * @return 当前WIFI工作模式, 未初始化时返回IDLE.
 */
int snfWifiGetMode(void);

/**
 * @brief 异步请求连接STA.
 *
 * 保留AP, 配置在返回前复制; 请求按入队顺序执行.
 *
 * @param [in] config - STA配置参数.
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfWifiStaConnect(const SnfWifiStaConfig *config);

/**
 * @brief 异步请求断开STA.
 *
 * 保留STA接口和AP; 请求按入队顺序执行.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfWifiStaDisconnect(void);

/**
 * @brief 异步请求关闭STA接口, 保留AP.
 *
 * 请求按入队顺序执行, 成功关闭后链路状态为IDLE.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfWifiStaStop(void);

/**
 * @brief 通过适配器查询当前STA实际连接信息.
 *
 * SDK已建立链路时可查询, 不依赖连接事件是否已出队.
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
 * 保留STA及其链路状态, 配置在返回前复制; 请求按入队顺序执行.
 * AP已启动时按SDK规则重新配置AP, 共存信道由SDK协调.
 *
 * @param [in] config - AP配置参数.
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfWifiApStart(const SnfWifiApConfig *config);

/**
 * @brief 异步请求停止AP, 保留STA及其链路状态.
 *
 * 请求按入队顺序执行.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfWifiApStop(void);

/**
 * @brief 异步请求关闭STA和AP.
 *
 * 请求按入队顺序执行, 两侧分别尝试关闭; 部分失败时保留实际工作模式.
 * 不反初始化模块, 不清除配置和扫描结果.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfWifiSetIdle(void);

/**
 * @brief 异步请求启动WIFI扫描.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
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
 * @brief 注册应用事件回调
 *
 * 回调在WIFI管理任务中执行且不持有状态锁, 可查询状态或提交新请求.
 * event_data仅在回调期间有效: CONNECTED为SnfWifiLinkInfo,
 * DISCONNECTED为SnfWifiStaDisconnectedEvent, 失败事件为int类型的适配层错误码,
 * MODE_CHANGED为SnfWifiModeChangedEvent, 其他事件为NULL.
 * 接口返回成功只表示请求入队, 执行结果通过回调通知.
 * WIFI管理队列满时可能丢失通知, 调用方可查询当前状态.
 *
 * @param [in] callback - WIFI管理模块事件回调.
 * @param [in] user_data - 传递给回调函数的用户数据.
 * @return WIFI管理模块状态码.
 */
int snfWifiRegisterEventCallback(SnfWifiEventCB callback, void *user_data);

/**
 * @brief 初始化WIFI状态管理任务.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfWifiInit(void);

#endif /* __SONOFF_WIFI_H__ */
