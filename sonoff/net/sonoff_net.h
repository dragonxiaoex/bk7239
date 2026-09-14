/**
 * @file    sonoff_net.h
 * @brief   网络管理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_NET_H__
#define __SONOFF_NET_H__

#include <stdint.h>

#include "sonoff_wifi_type.h"

/** @brief 网络接口. */
typedef enum
{
    SNF_NET_IF_STA = 0,
    SNF_NET_IF_AP,
    SNF_NET_IF_NONE,              /* 模式变化、扫描等不属于单个接口的事件 */
} SnfNetInterface;

/** @brief 单个接口的网络状态. */
typedef enum
{
    SNF_NET_STATE_IDLE = 0,        /* 接口未建立网络 */
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
 * STA与AP分别通知变化; 共存时AP_READY不代表STA断开.
 * 一侧回到IDLE时报告另一侧当前状态, 两侧均空闲才报告IDLE.
 * 可通过snfNetGetInterfaceState分别查询; 回调值不一定等于snfNetGetState.
 * READY的event_data为STA的IPv4地址快照, 网络字节序; 其他状态为NULL.
 * 回调在WIFI任务或SDK网络事件任务中执行, 不持有网络状态锁,
 * 不同任务的通知可能交错; 数据仅在回调期间有效, 不应执行阻塞操作.
 */
typedef void (*SnfNetEventCB)(SnfNetState state, const void *event_data);

/** @brief NET通知类别, 无线事件与IP可用性分别表达. */
typedef enum
{
    SNF_NET_EVT_WIFI = 0, /* 来自WIFI模块的事件 */
    SNF_NET_EVT_IP_READY,
    SNF_NET_EVT_IP_LOST,
} SnfNetEventId;

/**
 * @brief NET统一事件, 结构体及data仅在回调期间有效.
 *
 * WIFI类别的wifi_event为SnfWifiExternalEventId, data按无线事件解释:
 * MODE_CHANGED为SnfWifiModeChangedEvent, STA_CONNECTED为SnfWifiLinkInfo,
 * STA_DISCONNECTED为SnfWifiStaDisconnectedEvent, 失败为int错误码, 其他为NULL.
 * IP_READY/IP_LOST的data为该接口的新/旧IPv4地址, 网络字节序;
 * 这两类通知不使用wifi_event字段. 当前IP就绪通知以IPv4为准.
 */
typedef struct
{
    SnfNetEventId id;
    SnfNetInterface interface;
    SnfWifiExternalEventId wifi_event;
    const void *data;
} SnfNetEvent;

/** @brief 统一通知回调, 无锁调用; WIFI任务与SDK网络事件任务的通知可能交错. */
typedef void (*SnfNetNotifyCB)(const SnfNetEvent *event, void *user_data);

/**
 * @brief 注册统一通知回调, 与旧状态回调可以同时使用.
 *
 * 回调内可查询状态或提交异步请求, 不应阻塞; 模式和扫描事件的接口为NONE.
 * 允许在初始化前注册; WIFI队列满时无线通知可能丢失, 可通过查询接口校准.
 *
 * @param [in] callback - 回调函数.
 * @param [in] user_data - 回调用户数据.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetRegisterNotifyCallback(SnfNetNotifyCB callback, void *user_data);

/**
 * @brief 查询WIFI实际工作模式, NET不缓存无线状态.
 * @return SnfWifiManageMode, 未初始化时为IDLE.
 */
int snfNetGetWifiMode(void);

/**
 * @brief 查询STA链路状态, 与IP就绪状态分别判断.
 * @return SnfWifiLinkStatus, 未初始化时为IDLE.
 */
int snfNetGetWifiLinkStatus(void);

/**
 * @brief 查询STA实际连接信息.
 * @param [out] info - 当前连接信息.
 * @return 0表示成功, 负数表示失败或未连接.
 */
int snfNetStaGetLinkInfo(SnfWifiLinkInfo *info);

/**
 * @brief 查询STA信号强度.
 * @param [out] rssi - 信号强度, 单位dBm.
 * @return 0表示成功, 负数表示失败或未连接.
 */
int snfNetStaGetRssi(int *rssi);

/**
 * @brief 查询WIFI扫描状态.
 * @return 0表示未扫描, 1表示扫描中.
 */
int snfNetScanStatus(void);

/**
 * @brief 获取最近一次WIFI扫描结果.
 * @param [out] results - 结果数组.
 * @param [in] max_count - 数组容量.
 * @param [out] result_count - 实际写入的结果数.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count, uint16_t *result_count);

/**
 * @brief 异步请求连接STA.
 *
 * 保留AP; 配置在返回前复制, 请求按WIFI队列顺序执行.
 * 网络状态在请求实际执行后更新, 返回成功不表示连接完成.
 *
 * @param [in] config - STA配置参数.
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfNetStaConnect(const SnfWifiStaConfig *config);

/**
 * @brief 异步请求断开STA.
 *
 * 保留STA接口和AP; 请求按WIFI队列顺序执行, 成功执行后更新STA网络状态.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfNetStaDisconnect(void);

/**
 * @brief 异步请求关闭STA接口, 保留AP.
 *
 * 请求按WIFI队列顺序执行, 成功执行后更新STA网络状态.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfNetStaStop(void);

/**
 * @brief 异步请求启动AP.
 *
 * 保留STA网络和IP; 配置在返回前复制, 请求按WIFI队列顺序执行.
 * 网络状态在请求实际执行后更新, 返回成功不表示AP已经启动.
 *
 * @param [in] config - AP配置参数.
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfNetApStart(const SnfWifiApConfig *config);

/**
 * @brief 异步请求停止AP.
 *
 * 保留STA网络和IP; 请求按WIFI队列顺序执行, 成功执行后更新AP状态.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfNetApStop(void);

/**
 * @brief 异步请求关闭STA和AP.
 *
 * 请求按WIFI队列顺序执行; 一侧关闭失败也继续关闭另一侧, 状态反映实际结果.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfNetSetIdle(void);

/**
 * @brief 注册兼容旧应用的网络状态回调, 新应用可使用统一通知回调.
 *
 * @param [in] callback - 网络状态回调.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetRegisterEventCallback(SnfNetEventCB callback);

/**
 * @brief 异步请求扫描WiFi.
 *
 * @return 0表示请求已入队, 负数表示请求未接受.
 */
int snfNetScan(void);

/**
 * @brief 获取当前网络状态.
 *
 * STA网络状态非IDLE时返回STA状态, 否则返回AP状态.
 *
 * @return 当前网络状态.
 */
int snfNetGetState(void);

/**
 * @brief 获取指定接口的网络状态.
 *
 * @param [in] interface - STA或AP接口.
 * @return 接口状态, 未初始化时返回IDLE, 参数无效时返回负数.
 */
int snfNetGetInterfaceState(SnfNetInterface interface);

/**
 * @brief 获取当前网络接口的IPv4地址.
 *
 * 共存或STA接口启用时选择STA, 仅AP启用或启动中时选择AP.
 *
 * @param [out] ip - IPv4地址, 网络字节序.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetIpv4(uint32_t *ip);

/**
 * @brief 获取当前网络接口的IPv6地址.
 *
 * 默认接口选择与snfNetGetIpv4一致.
 *
 * @param [out] ip - 16字节IPv6地址.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetIpv6(uint8_t *ip);

/**
 * @brief 获取当前网络接口的MAC地址.
 *
 * 默认接口选择与snfNetGetIpv4一致.
 *
 * @param [out] mac - 6字节MAC地址.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetMac(uint8_t *mac);

/**
 * @brief 获取指定接口的IPv4地址.
 *
 * @param [in] interface - STA或AP接口.
 * @param [out] ip - IPv4地址, 网络字节序.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetInterfaceIpv4(SnfNetInterface interface, uint32_t *ip);

/**
 * @brief 获取指定接口的IPv6地址.
 *
 * @param [in] interface - STA或AP接口.
 * @param [out] ip - 至少16字节的IPv6地址缓冲区.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetInterfaceIpv6(SnfNetInterface interface, uint8_t *ip);

/**
 * @brief 获取指定接口的MAC地址.
 *
 * @param [in] interface - STA或AP接口.
 * @param [out] mac - 至少6字节的MAC地址缓冲区.
 * @return 0表示成功, 负数表示失败.
 */
int snfNetGetInterfaceMac(SnfNetInterface interface, uint8_t *mac);

/**
 * @brief 初始化网络管理模块.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfNetInit(void);

#endif /* __SONOFF_NET_H__ */
