/**
 * @file    sonoff_websocket.h
 * @brief   WebSocket服务端帧处理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-14
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_WEBSOCKET_H__
#define __SONOFF_WEBSOCKET_H__

#include <stddef.h>
#include <stdint.h>

#include "sonoff_http_internal.h"

/** @brief WebSocket控制码与握手结果长度. */
#define SNF_WS_TEXT           1      /* 文本消息 */
#define SNF_WS_CLOSE          8      /* 关闭连接 */
#define SNF_WS_PING           9      /* Ping */
#define SNF_WS_PONG            10    /* Pong */
#define SNF_WS_ACCEPT_SIZE    29     /* Accept字符串容量，含结束符 */

/** @brief 连接任务回调，载荷仅在回调期间有效. */
typedef struct
{
    int (*send_frame)(void *ctx, uint8_t opcode, const void *data, size_t length);  /**< 串行发送完整帧 */
    int (*receive_text)(void *ctx, const char *message);                          /**< 接收重组后的消息 */
} SnfWebsocketCallbacks;

/**
 * @brief 验证握手Key并计算Sec-WebSocket-Accept.
 * @param [in] key - 客户端Key.
 * @param [out] accept - 至少SNF_WS_ACCEPT_SIZE字节的缓冲区.
 * @return 0表示成功，负数表示无效Key或计算失败.
 */
int snfWebsocketBuildAccept(const char *key, char *accept);

/**
 * @brief 发送服务端未掩码的完整帧.
 * @param [in] socket - 已升级的套接字.
 * @param [in] opcode - 操作码.
 * @param [in] data - 帧载荷.
 * @param [in] length - 字节数.
 * @note 调用者负责串行化同一连接的所有发送.
 * @return 0表示成功，负数表示发送失败.
 */
int snfWebsocketSendFrame(int socket, uint8_t opcode, const void *data, size_t length);

/**
 * @brief 接收文本消息并处理分片、Ping/Pong和Close，阻塞到连接结束.
 * @param [in,out] reader - TCP输入流，包含握手时预读的帧字节.
 * @param [in] callbacks - 完整消息与发送回调.
 * @param [in] ctx - 回调上下文.
 * @note 不关闭套接字，不释放ctx，资源由连接任务统一回收.
 */
void snfWebsocketRun(SnfHttpReader *reader, const SnfWebsocketCallbacks *callbacks, void *ctx);

#endif /* __SONOFF_WEBSOCKET_H__ */
