/**
 * @file    sonoff_http_internal.h
 * @brief   HTTP与WebSocket内部接口
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-14
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_HTTP_INTERNAL_H__
#define __SONOFF_HTTP_INTERNAL_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 接收与认证缓冲区限制. */
#define SNF_HTTP_RPC_MAX_LENGTH      4096    /* RPC请求最大字节数，不含结束符 */
#define SNF_HTTP_HEADER_MAX_LENGTH   2048    /* HTTP请求头最大字节数 */
#define SNF_HTTP_AUTH_HEADER_SIZE    256     /* WWW-Authenticate响应头容量 */

/** @brief 保留预读数据的TCP输入流，归连接任务独占. */
typedef struct
{
    int socket;
    uint8_t buffer[512];
    size_t position;
    size_t length;
} SnfHttpReader;

/** @brief 请求头字段均借用原请求头缓冲区. */
typedef struct
{
    char *method;
    char *uri;
    char *version;
    char *host;
    char *authorization;
    char *connection;
    char *upgrade;
    char *websocket_key;
    char *websocket_version;
    char *expect;
    size_t content_length;
    bool has_content_length;
} SnfHttpRequest;

/**
 * @brief 完整发送数据，处理TCP短写.
 * @param [in] socket - 已连接套接字.
 * @param [in] data - 发送数据.
 * @param [in] length - 字节数.
 * @return 0表示成功，负数表示失败.
 */
int snfHttpSendAll(int socket, const void *data, size_t length);

/**
 * @brief 读取指定字节数，优先消费预读数据.
 * @param [in,out] reader - 输入流.
 * @param [out] data - 接收缓冲区.
 * @param [in] length - 读取字节数.
 * @return 0表示成功，负数表示断开或超时.
 */
int snfHttpReadExact(SnfHttpReader *reader, void *data, size_t length);

/**
 * @brief 接收并解析请求头，保留已经收到的正文或WebSocket帧.
 * @param [in,out] reader - 输入流.
 * @param [out] buffer - 至少SNF_HTTP_HEADER_MAX_LENGTH+1字节的缓冲区.
 * @param [out] request - 解析结果，借用buffer.
 * @return 0表示成功，正数为HTTP错误状态码，负数表示连接失败.
 */
int snfHttpReadRequest(SnfHttpReader *reader, char *buffer, SnfHttpRequest *request);

/**
 * @brief 检查逗号分隔的HTTP头字段是否包含指定标记，不区分大小写.
 * @param [in] value - 头字段值，可为NULL.
 * @param [in] token - 要查找的标记.
 * @return 是否包含完整标记.
 */
bool snfHttpHeaderHasToken(const char *value, const char *token);

/**
 * @brief 检查HTTP摘要认证，失败时生成质询响应头.
 * @param [in,out] request - 请求，授权头解析时会原地分割.
 * @param [out] challenge - 至少SNF_HTTP_AUTH_HEADER_SIZE字节的缓冲区.
 * @return 0表示放行，401表示需要认证，500表示内部失败.
 */
int snfHttpAuthCheck(SnfHttpRequest *request, char *challenge);

#endif /* __SONOFF_HTTP_INTERNAL_H__ */
