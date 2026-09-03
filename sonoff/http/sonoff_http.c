/**
 * @file    sonoff_http.c
 * @brief   HTTP服务端
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>

#include <FreeRTOS.h>
#include <lwip/sockets.h>
#include <task.h>

#include "sonoff_html.h"
#include "sonoff_http_cgi.h"
#include "sonoff_http.h"
#include "sonoff_log.h"
#include "sonoff_ota_http.h"
#include "sonoff_task_def.h"

/** @brief HTTP服务日志标签. */
static const char *tag = "SNF-HTTP";

/** @brief HTTP服务监听端口. */
#define SNF_HTTP_SERVER_PORT (80U)

/** @brief HTTP服务监听队列长度. */
#define SNF_HTTP_SERVER_BACKLOG (1U)

/** @brief HTTP监听失败后的重试延时. */
#define SNF_HTTP_ACCEPT_RETRY_DELAY_MS (100U)

/** @brief HTTP请求缓冲区大小. */
#define SNF_HTTP_REQUEST_BUFFER_SIZE (512U)

/** @brief HTTP CGI响应缓冲区大小. */
#define SNF_HTTP_CGI_RESPONSE_BUFFER_SIZE (128U)

/**
 * @brief HTTP服务运行状态.
 */
typedef struct
{
    TaskHandle_t task_handle; /* HTTP服务任务句柄 */
    int started;              /* HTTP服务启动状态 */
} SnfHttpState;

/** @brief HTTP服务运行状态实例. */
static SnfHttpState http_state_data = {
    .task_handle = NULL,
    .started = 0,
};

/**
 * @brief 接收HTTP连接并返回网页内容.
 *
 * @param [in] arg - 任务参数.
 */
static void snfHttpServerTask(void *arg)
{
    SnfHttpState *http_state = &http_state_data;
    struct sockaddr_in server_address = {0};
    const char http_header[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n";
    const char cgi_header[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n";
    char request[SNF_HTTP_REQUEST_BUFFER_SIZE] = {0};
    char cgi_response[SNF_HTTP_CGI_RESPONSE_BUFFER_SIZE] = {0};
    int listen_socket = -1;
    int client_socket;
    int request_length;
    int cgi_response_length;
    int server_ready = 0;

    (void)arg;

    listen_socket = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket < 0)
    {
        LOG_E(tag, "create socket failed");
    }
    else
    {
        server_address.sin_family = AF_INET;
        server_address.sin_port = PP_HTONS(SNF_HTTP_SERVER_PORT);
        server_address.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

        if (lwip_bind(listen_socket,
                      (const struct sockaddr *)&server_address,
                      sizeof(server_address)) != 0)
        {
            LOG_E(tag, "bind port %u failed", SNF_HTTP_SERVER_PORT);
            lwip_close(listen_socket);
            listen_socket = -1;
        }
        else if (lwip_listen(listen_socket, SNF_HTTP_SERVER_BACKLOG) != 0)
        {
            LOG_E(tag, "listen failed");
            lwip_close(listen_socket);
            listen_socket = -1;
        }
        else
        {
            server_ready = 1;
            LOG_I(tag, "http server listening on port %u", SNF_HTTP_SERVER_PORT);
        }
    }

    if (server_ready != 0)
    {
        while (1)
        {
            client_socket = lwip_accept(listen_socket, NULL, NULL);
            if (client_socket >= 0)
            {
                cgi_response_length = 0;
                request_length = (int)lwip_recv(client_socket,
                                                request,
                                                sizeof(request) - 1U,
                                                0);
                if (request_length > 0)
                {
                    request[request_length] = '\0';
                    if (snfOtaHttpIsUploadRequest(request,
                                                   (size_t)request_length) != 0)
                    {
                        cgi_response_length = snfOtaHttpHandleUpload(client_socket,
                                                                      request,
                                                                      (size_t)request_length,
                                                                      cgi_response,
                                                                      sizeof(cgi_response));
                    }
                    else
                    {
                        cgi_response_length = snfHttpCgiHandleRequest(request,
                                                                      cgi_response,
                                                                      sizeof(cgi_response));
                    }
                }

                if (cgi_response_length > 0)
                {
                    if (lwip_send(client_socket,
                                  cgi_header,
                                  sizeof(cgi_header) - 1U,
                                  0) < 0)
                    {
                        LOG_E(tag, "send CGI header failed");
                    }
                    else if (lwip_send(client_socket,
                                       cgi_response,
                                       (size_t)cgi_response_length,
                                       0) < 0)
                    {
                        LOG_E(tag, "send CGI response failed");
                    }
                }
                else if (lwip_send(client_socket,
                                   http_header,
                                   sizeof(http_header) - 1U,
                                   0) < 0)
                {
                    LOG_E(tag, "send HTTP header failed");
                }
                else if (lwip_send(client_socket,
                                   sonoff_html_index,
                                   sizeof(sonoff_html_index) - 1U,
                                   0) < 0)
                {
                    LOG_E(tag, "send HTTP page failed");
                }

                lwip_close(client_socket);
            }
            else
            {
                LOG_E(tag, "accept HTTP connection failed");
                vTaskDelay(pdMS_TO_TICKS(SNF_HTTP_ACCEPT_RETRY_DELAY_MS));
            }
        }
    }

    http_state->started = 0;
    http_state->task_handle = NULL;
    vTaskDelete(NULL);
}

int snfHttpServerStart(void)
{
    static const SnfHttpCgiRoute cgi_routes[] = {
        {"/cgi/toggle", snfHttpCgiToggle},
        {"/cgi/toggle_status", snfHttpCgiGetToggleStatus},
        {"/cgi/time", snfHttpCgiGetTime},
        {"/cgi/ota_progress",
         snfOtaHttpGetProgress},
    };
    SnfHttpState *http_state = &http_state_data;
    int ret;

    if (http_state->started != 0)
    {
        return 0;
    }

    ret = snfHttpCgiRegister(cgi_routes,
                             sizeof(cgi_routes) / sizeof(cgi_routes[0]));
    if (ret != 0)
    {
        LOG_E(tag, "HTTP CGI register failed, ret=%d", ret);
        return -1;
    }

    http_state->started = 1;
    if (xTaskCreate(snfHttpServerTask,
                    SONOFF_HTTP_TASK_NAME,
                    SONOFF_HTTP_TASK_STACKSIZE,
                    NULL,
                    SONOFF_HTTP_TASK_PRIO,
                    &http_state->task_handle) != pdPASS)
    {
        http_state->started = 0;
        http_state->task_handle = NULL;
        LOG_E(tag, "http server task init failed");
        return -2;
    }

    LOG_I(tag, "http server start");

    return 0;
}
