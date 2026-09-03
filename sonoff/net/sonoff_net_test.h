/**
 * @file    sonoff_net_test.h
 * @brief   网络吞吐压力测试模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-28
 *
 * @copyright Copyright (c) 2026 深圳松诺技术有限公司
 *
 */

#ifndef SONOFF_NET_TEST_H_
#define SONOFF_NET_TEST_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SNR_NET_TEST_THREAD_TEST_ENABLE           0

/* 方向配置：1表示BK7239N发送，0表示BK7239N接收。 */
#ifndef SNF_NET_TEST_WIFI_SEND_ENABLE
#define SNF_NET_TEST_WIFI_SEND_ENABLE             1
#endif

#ifndef SNF_NET_TEST_THREAD_SEND_ENABLE
#define SNF_NET_TEST_THREAD_SEND_ENABLE           1
#endif

/* 通用测试配置。测试计时从连接建立或收到首个UDP数据包后开始。 */
#ifndef SNF_NET_TEST_DURATION_SECONDS
#define SNF_NET_TEST_DURATION_SECONDS             600U
#endif

#ifndef SNF_NET_TEST_PROGRESS_INTERVAL_SECONDS
#define SNF_NET_TEST_PROGRESS_INTERVAL_SECONDS    10U
#endif

/* Wi-Fi测试配置。对端需使用iperf2，iperf3协议不兼容。 */
#ifndef SNF_NET_TEST_WIFI_PEER_IPV4
#define SNF_NET_TEST_WIFI_PEER_IPV4               "192.168.50.165"
#endif

#ifndef SNF_NET_TEST_WIFI_TCP_PORT
#define SNF_NET_TEST_WIFI_TCP_PORT                5001U
#endif

#ifndef SNF_NET_TEST_WIFI_UDP_PORT
#define SNF_NET_TEST_WIFI_UDP_PORT                5001U
#endif

#ifndef SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE
#define SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE        8192U
#endif

#ifndef SNF_NET_TEST_WIFI_UDP_PAYLOAD_SIZE
#define SNF_NET_TEST_WIFI_UDP_PAYLOAD_SIZE        1470U
#endif

#ifndef SNF_NET_TEST_WIFI_UDP_TARGET_BITRATE
#define SNF_NET_TEST_WIFI_UDP_TARGET_BITRATE      30000000U
#endif

/* Thread测试配置。对端UDP程序需实现iperf2 UDP头及结束回报帧。 */
#ifndef SNF_NET_TEST_THREAD_PEER_IPV6
#define SNF_NET_TEST_THREAD_PEER_IPV6             "fd18:c2cd:8d60:a5d4:3cfa:bbef:c1dc:22d"
#endif

#ifndef SNF_NET_TEST_THREAD_UDP_PORT
#define SNF_NET_TEST_THREAD_UDP_PORT              5002U
#endif

#ifndef SNF_NET_TEST_THREAD_UDP_PAYLOAD_SIZE
#define SNF_NET_TEST_THREAD_UDP_PAYLOAD_SIZE      64U
#endif

#ifndef SNF_NET_TEST_THREAD_UDP_TARGET_BITRATE
#define SNF_NET_TEST_THREAD_UDP_TARGET_BITRATE    100000U
#endif

/**
 * @brief 阻塞获取Thread测试互斥锁.
 *
 * @return true 获取成功.
 * @return false 互斥锁未初始化或获取失败.
 */
bool snfNetTestThreadLock(void);

/**
 * @brief 非阻塞获取Thread测试互斥锁.
 *
 * @return true 获取成功.
 * @return false 互斥锁未初始化或当前不可用.
 */
bool snfNetTestThreadTryLock(void);

/**
 * @brief 释放Thread测试互斥锁.
 *
 * @return true 释放成功.
 * @return false 互斥锁未初始化或释放失败.
 */
bool snfNetTestThreadUnlock(void);

/**
 * @brief 初始化网络测试模块资源.
 *
 * @return 0 成功.
 * @return -1 失败.
 */
int snfNetTestInit(void);

/**
 * @brief 启动一次10分钟Thread UDP测试.
 *
 * @return 0 启动成功或测试已经运行.
 * @return -1 初始化或任务创建失败.
 */
int snfNetTestThreadInit(void);

/**
 * @brief 启动一次10分钟Wi-Fi TCP测试.
 *
 * @return 0 启动成功或测试已经运行.
 * @return -1 任务创建失败.
 */
int snfNetTestWifiTcpInit(void);

/**
 * @brief 启动一次10分钟Wi-Fi UDP测试.
 *
 * @return 0 启动成功或测试已经运行.
 * @return -1 任务创建失败.
 */
int snfNetTestWifiUdpInit(void);

#ifdef __cplusplus
}
#endif

#endif /* SONOFF_NET_TEST_H_ */
