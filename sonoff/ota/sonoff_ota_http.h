/**
 * @file    sonoff_ota_http.h
 * @brief   OTA升级HTTP适配接口
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-27
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_OTA_SONOFF_OTA_HTTP_H__
#define __SONOFF_OTA_SONOFF_OTA_HTTP_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 判断HTTP请求是否为OTA镜像上传请求.
 *
 * @param [in] request - HTTP请求数据.
 * @param [in] request_size - HTTP请求数据长度.
 * @return 1表示是OTA上传请求, 0表示不是.
 */
int snfOtaHttpIsUploadRequest(const char *request, size_t request_size);

/**
 * @brief 接收HTTP上传的OTA镜像.
 *
 * 请求头和已接收的部分请求数据由request提供，后续数据从client_socket继续读取。
 * 函数返回前会等待OTA写入及镜像校验完成，并在校验成功后请求应用升级。
 *
 * @param [in] client_socket - HTTP客户端套接字.
 * @param [in] request - 已接收的HTTP请求数据.
 * @param [in] request_size - 已接收的HTTP请求数据长度.
 * @param [out] response - HTTP响应内容.
 * @param [in] response_size - HTTP响应缓冲区大小.
 * @return 非负数表示响应内容长度, 负数表示响应生成失败.
 */
int snfOtaHttpHandleUpload(int client_socket,
                           const char *request,
                           size_t request_size,
                           char *response,
                           size_t response_size);

/**
 * @brief 获取HTTP OTA升级进度.
 *
 * @param [in] query - 查询参数.
 * @param [in] query_length - 查询参数长度.
 * @param [out] response - CGI响应内容.
 * @param [in] response_size - CGI响应缓冲区大小.
 * @return 非负数表示响应内容长度, 负数表示失败.
 */
int snfOtaHttpGetProgress(const char *query,
                          size_t query_length,
                          char *response,
                          size_t response_size);

#ifdef __cplusplus
}
#endif

#endif /* __SONOFF_OTA_SONOFF_OTA_HTTP_H__ */
