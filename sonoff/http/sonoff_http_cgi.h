/**
 * @file    sonoff_http_cgi.h
 * @brief   HTTP网页事件处理.
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_HTTP_SONOFF_HTTP_CGI_H__
#define __SONOFF_HTTP_SONOFF_HTTP_CGI_H__

#include <stddef.h>

/**
 * @brief HTTP CGI事件回调.
 *
 * @param [in] query - 查询参数.
 * @param [in] query_length - 查询参数长度.
 * @param [out] response - CGI响应内容.
 * @param [in] response_size - CGI响应缓冲区大小.
 * @return 非负数表示响应内容长度, 负数表示失败.
 */
typedef int (*SnfHttpCgiCallback)(const char *query,
                                  size_t query_length,
                                  char *response,
                                  size_t response_size);

/**
 * @brief HTTP CGI路由.
 */
typedef struct
{
    const char *path;            /* CGI访问路径 */
    SnfHttpCgiCallback callback; /* CGI事件回调 */
} SnfHttpCgiRoute;

/**
 * @brief 注册HTTP CGI事件回调.
 *
 * @param [in] routes - CGI路由数组.
 * @param [in] route_count - CGI路由数量.
 * @return 0表示成功, 负数表示失败.
 */
int snfHttpCgiRegister(const SnfHttpCgiRoute *routes, size_t route_count);

/**
 * @brief 根据HTTP请求调用对应的CGI回调.
 *
 * request必须是以空字符结尾的HTTP请求文本.
 *
 * @param [in] request - HTTP请求文本.
 * @param [out] response - CGI响应内容.
 * @param [in] response_size - CGI响应缓冲区大小.
 * @return 非负数表示响应内容长度, 负数表示失败.
 */
int snfHttpCgiHandleRequest(const char *request,
                            char *response,
                            size_t response_size);

/**
 * @brief 处理GPIO翻转按钮事件.
 *
 * @param [in] query - 查询参数.
 * @param [in] query_length - 查询参数长度.
 * @param [out] response - CGI响应内容.
 * @param [in] response_size - CGI响应缓冲区大小.
 * @return 非负数表示响应内容长度, 负数表示失败.
 */
int snfHttpCgiToggle(const char *query,
                     size_t query_length,
                     char *response,
                     size_t response_size);

/**
 * @brief 获取GPIO输出状态.
 *
 * @param [in] query - 查询参数.
 * @param [in] query_length - 查询参数长度.
 * @param [out] response - CGI响应内容.
 * @param [in] response_size - CGI响应缓冲区大小.
 * @return 非负数表示响应内容长度, 负数表示失败.
 */
int snfHttpCgiGetToggleStatus(const char *query,
                              size_t query_length,
                              char *response,
                              size_t response_size);

/**
 * @brief 获取当前时间.
 *
 * @param [in] query - 查询参数.
 * @param [in] query_length - 查询参数长度.
 * @param [out] response - CGI响应内容.
 * @param [in] response_size - CGI响应缓冲区大小.
 * @return 非负数表示响应内容长度, 负数表示失败.
 */
int snfHttpCgiGetTime(const char *query,
                      size_t query_length,
                      char *response,
                      size_t response_size);

#endif /* __SONOFF_HTTP_SONOFF_HTTP_CGI_H__ */
