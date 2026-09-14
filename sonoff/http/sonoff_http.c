/**
 * @file    sonoff_http.c
 * @brief   HTTP RPC与WebSocket连接管理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-14
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <FreeRTOS.h>
#include <lwip/sockets.h>
#include <semphr.h>
#include <task.h>

#include "sonoff_http.h"
#include "sonoff_http_internal.h"
#include "sonoff_log.h"
#include "sonoff_rpc.h"
#include "sonoff_task_def.h"
#include "sonoff_websocket.h"

static const char *tag = "SNF-HTTP";

/** @brief 监听和连接资源配置. */
#define HTTP_SERVER_PORT             80       /* HTTP和WebSocket共用端口 */
#define HTTP_CONNECTION_COUNT        4        /* 同时在线连接数 */
#define HTTP_CONNECTION_STACK        2048     /* 连接任务栈元素数，本平台每元素4字节 */
#define HTTP_RECEIVE_TIMEOUT_MS      10000    /* HTTP请求接收超时 */
#define HTTP_SEND_TIMEOUT_MS         5000     /* 单次发送超时 */
#define HTTP_RPC_TIMEOUT_MS          10000    /* 等待RPC响应超时 */
#define HTTP_ACCEPT_RETRY_MS         100      /* accept失败重试间隔 */

/** @brief 固定连接槽，锁与信号量在服务期间保持有效. */
typedef struct
{
    uintptr_t id;                    /**< 非零标识本次连接，回调通过该标识查找 */
    int socket;                      /**< 当前套接字 */
    SemaphoreHandle_t send_mutex;    /**< 串行化帧、HTTP响应和关闭 */
    SemaphoreHandle_t response_sem;  /**< 唤醒HTTP响应等待任务 */
    SnfRpcChannelHandle rpc;          /**< 当前RPC通道，归连接任务管理 */
    bool websocket;                  /**< 已完成WebSocket升级 */
    bool notify_ready;               /**< 已收到携带有效src的请求 */
    bool response_done;              /**< HTTP响应已经尝试发送 */
    bool closing;                    /**< 禁止后续帧发送 */
} SnfHttpConnection;

/** @brief 服务运行状态，连接槽的分配和身份查找由mutex保护. */
typedef struct
{
    TaskHandle_t task;
    SemaphoreHandle_t mutex;
    SnfHttpConnection connections[HTTP_CONNECTION_COUNT];
    uintptr_t next_id;
    int listen_socket;
} SnfHttpState;

static SnfHttpState http_state =
{
    .listen_socket = -1,
};

/** @brief 返回HTTP状态短语. */
static const char *httpStatusText(int status)
{
    switch (status)
    {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 411:
        return "Length Required";
    case 413:
        return "Content Too Large";
    case 417:
        return "Expectation Failed";
    case 426:
        return "Upgrade Required";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    default:
        return "Error";
    }
}

/** @brief 发送带准确长度的HTTP短连接响应；调用者负责发送互斥. */
static int httpSendResponse(int socket, int status, const char *extra, const char *body, size_t length)
{
    char header[192];
    int count = snprintf(header, sizeof(header),
                         "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
                         "Content-Length: %zu\r\nConnection: close\r\n",
                         status, httpStatusText(status), length);

    if ((count <= 0) || ((size_t)count >= sizeof(header)))
    {
        return -1;
    }
    if (snfHttpSendAll(socket, header, (size_t)count) != 0)
    {
        return -1;
    }
    if ((extra != NULL) && (snfHttpSendAll(socket, extra, strlen(extra)) != 0))
    {
        return -1;
    }
    if (snfHttpSendAll(socket, "\r\n", 2) != 0)
    {
        return -1;
    }

    return snfHttpSendAll(socket, body, length);
}

/** @brief 找到仍有效的连接并锁住发送，防止迟到回调使用复用后的socket. */
static SnfHttpConnection *httpLockConnection(uintptr_t id)
{
    SnfHttpState *state = &http_state;
    SnfHttpConnection *connection = NULL;

    xSemaphoreTake(state->mutex, portMAX_DELAY);
    for (uint32_t i = 0; i < HTTP_CONNECTION_COUNT; i++)
    {
        if ((id != 0) && (state->connections[i].id == id))
        {
            connection = &state->connections[i];
            xSemaphoreTake(connection->send_mutex, portMAX_DELAY);
            break;
        }
    }
    xSemaphoreGive(state->mutex);

    return connection;
}

/** @brief RPC借用的字符串在回调内发完，不保留指针；HTTP屏蔽所有通知. */
static int32_t httpRpcSend(void *handle, const char *message, uint32_t length)
{
    SnfHttpConnection *connection;
    cJSON *root = cJSON_Parse(message);
    bool notification;
    int ret;

    if (root == NULL)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }
    notification = (cJSON_GetObjectItem(root, "method") != NULL) && (cJSON_GetObjectItem(root, "id") == NULL);
    cJSON_Delete(root);
    connection = httpLockConnection((uintptr_t)handle);
    if (connection == NULL)
    {
        return SNF_RPC_ERR_INVALID_STATE;
    }
    if (connection->closing || (notification && (!connection->websocket || !connection->notify_ready))
            || (!connection->websocket && connection->response_done))
    {
        xSemaphoreGive(connection->send_mutex);
        return SNF_RPC_ERR_NOT_SUPPORTED;
    }
    if (connection->websocket)
    {
        ret = snfWebsocketSendFrame(connection->socket, SNF_WS_TEXT, message, length);
    }
    else
    {
        ret = httpSendResponse(connection->socket, 200, NULL, message, length);
        connection->response_done = true;
        xSemaphoreGive(connection->response_sem);
    }
    if (ret != 0)
    {
        connection->closing = true;
        lwip_shutdown(connection->socket, SHUT_RDWR);
    }
    xSemaphoreGive(connection->send_mutex);

    return ret == 0 ? SNF_RPC_OK : SNF_RPC_ERR_INTERNAL;
}

/** @brief 在连接任务中注册通道，初始化和业务方法由应用提供. */
static int httpRegisterChannel(SnfHttpConnection *connection, bool websocket)
{
    char name[32];
    SnfRpcChannelConfig config = {0};

    snprintf(name, sizeof(name), "%s-%" PRIuPTR, websocket ? "ws" : "http", connection->id);
    config.channel_name = name;
    config.transport_type = websocket ? SNF_RPC_TRANSPORT_WEBSOCKET : SNF_RPC_TRANSPORT_HTTP;
    config.conn_mode = websocket ? SNF_RPC_CONN_PERSISTENT : SNF_RPC_CONN_TEMPORARY;
    config.channel_handle = (void *)connection->id;
    config.send_cb = httpRpcSend;
    /* HTTP已经在入口校验Digest；WebSocket继续由RPC校验消息中的auth。 */
    config.auth_supported = websocket;

    return snfRpcChannelRegister(&config, &connection->rpc);
}

/** @brief WebSocket控制帧与RPC通知共用同一个发送锁. */
static int httpWebsocketSend(void *ctx, uint8_t opcode, const void *data, size_t length)
{
    SnfHttpConnection *connection = ctx;
    int ret = -1;

    xSemaphoreTake(connection->send_mutex, portMAX_DELAY);
    if (!connection->closing)
    {
        ret = snfWebsocketSendFrame(connection->socket, opcode, data, length);
        if ((ret != 0) || (opcode == SNF_WS_CLOSE))
        {
            connection->closing = true;
        }
    }
    xSemaphoreGive(connection->send_mutex);

    return ret;
}

/** @brief 文本消息交给RPC；首次有效src请求之前禁止发送通知. */
static int httpWebsocketReceive(void *ctx, const char *message)
{
    SnfHttpConnection *connection = ctx;
    cJSON *root = cJSON_ParseWithOpts(message, NULL, true);
    bool ready = false;
    int ret;

    if (JSON_IS_OBJECT(root))
    {
        cJSON *src = cJSON_GetObjectItem(root, "src");
        cJSON *method = cJSON_GetObjectItem(root, "method");
        ready = JSON_IS_STRING(src) && (src->valuestring != NULL) && (src->valuestring[0] != '\0')
                && (strlen(src->valuestring) < 64) && JSON_IS_STRING(method);
    }
    cJSON_Delete(root);
    if (ready)
    {
        xSemaphoreTake(connection->send_mutex, portMAX_DELAY);
        connection->notify_ready = true;
        xSemaphoreGive(connection->send_mutex);
    }
    ret = snfRpcChannelHandleRequest(connection->rpc, message);
    if (ret != SNF_RPC_OK)
    {
        /* RPC可能已同步发送繁忙响应，避免再生成第二份响应。 */
        LOG_W(tag, "WebSocket RPC submission failed, ret=%d", ret);
    }

    return ret == SNF_RPC_ERR_NOT_INITED ? -1 : 0;
}

/** @brief 校验升级请求并完成握手，保留预读的帧数据. */
static void httpHandleWebsocket(SnfHttpConnection *connection, SnfHttpRequest *request)
{
    char accept[SNF_WS_ACCEPT_SIZE];
    char response[192];
    struct timeval timeout = {0};
    int length;

    if ((strcmp(request->version, "HTTP/1.1") != 0)
            || !snfHttpHeaderHasToken(request->connection, "Upgrade")
            || !snfHttpHeaderHasToken(request->upgrade, "websocket") || (request->content_length != 0))
    {
        httpSendResponse(connection->socket, 400, NULL, NULL, 0);
        return;
    }
    if ((request->websocket_version == NULL) || (strcmp(request->websocket_version, "13") != 0))
    {
        httpSendResponse(connection->socket, 426, "Sec-WebSocket-Version: 13\r\n", NULL, 0);
        return;
    }
    if (snfWebsocketBuildAccept(request->websocket_key, accept) != 0)
    {
        httpSendResponse(connection->socket, 400, NULL, NULL, 0);
        return;
    }
    if (httpRegisterChannel(connection, true) != SNF_RPC_OK)
    {
        httpSendResponse(connection->socket, 503, NULL, NULL, 0);
        return;
    }
    length = snprintf(response, sizeof(response),
                      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if ((length <= 0) || ((size_t)length >= sizeof(response)))
    {
        return;
    }
    if (snfHttpSendAll(connection->socket, response, (size_t)length) != 0)
    {
        return;
    }
    /* 空闲长连接不套用HTTP收包超时，断开由读失败、Close或发送失败触发。 */
    if (lwip_setsockopt(connection->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
    {
        return;
    }
    xSemaphoreTake(connection->send_mutex, portMAX_DELAY);
    connection->websocket = true;
    xSemaphoreGive(connection->send_mutex);
}

/** @brief 短连接接收单条带数字id的调用，不接受通知或批量调用. */
static void httpHandleRpc(SnfHttpConnection *connection, SnfHttpReader *reader, SnfHttpRequest *request)
{
    char challenge[SNF_HTTP_AUTH_HEADER_SIZE];
    char *body;
    cJSON *root;
    cJSON *id;
    int ret;
    bool valid;

    /* 先认证再收正文，支持curl --digest的空正文探测以及Expect: 100-continue。 */
    ret = snfHttpAuthCheck(request, challenge);
    if (ret != 0)
    {
        httpSendResponse(connection->socket, ret, ret == 401 ? challenge : NULL, NULL, 0);
        return;
    }
    if (!request->has_content_length)
    {
        httpSendResponse(connection->socket, 411, NULL, NULL, 0);
        return;
    }
    if (request->content_length == 0)
    {
        httpSendResponse(connection->socket, 400, NULL, NULL, 0);
        return;
    }
    if (request->expect != NULL)
    {
        if (strcasecmp(request->expect, "100-continue") != 0)
        {
            httpSendResponse(connection->socket, 417, NULL, NULL, 0);
            return;
        }
        if (snfHttpSendAll(connection->socket, "HTTP/1.1 100 Continue\r\n\r\n", 25) != 0)
        {
            return;
        }
    }
    body = malloc(request->content_length + 1);
    if (body == NULL)
    {
        httpSendResponse(connection->socket, 500, NULL, NULL, 0);
        return;
    }
    ret = snfHttpReadExact(reader, body, request->content_length);
    if (ret != 0)
    {
        free(body);
        httpSendResponse(connection->socket, 408, NULL, NULL, 0);
        return;
    }
    body[request->content_length] = '\0';
    root = cJSON_ParseWithOpts(body, NULL, true);
    id = JSON_IS_OBJECT(root) ? cJSON_GetObjectItem(root, "id") : NULL;
    valid = JSON_IS_OBJECT(root) && JSON_IS_NUMBER(id) && (memchr(body, 0, request->content_length) == NULL);
    cJSON_Delete(root);
    if (!valid)
    {
        free(body);
        httpSendResponse(connection->socket, 400, NULL, NULL, 0);
        return;
    }
    ret = httpRegisterChannel(connection, false);
    if (ret != SNF_RPC_OK)
    {
        free(body);
        httpSendResponse(connection->socket, 503, NULL, NULL, 0);
        return;
    }
    ret = snfRpcChannelHandleRequest(connection->rpc, body);
    free(body);
    if (ret == SNF_RPC_OK)
    {
        xSemaphoreTake(connection->response_sem, pdMS_TO_TICKS(HTTP_RPC_TIMEOUT_MS));
    }
    xSemaphoreTake(connection->send_mutex, portMAX_DELAY);
    if (!connection->response_done)
    {
        httpSendResponse(connection->socket, ret == SNF_RPC_OK ? 504 : 503, NULL, NULL, 0);
        connection->response_done = true;
    }
    xSemaphoreGive(connection->send_mutex);
}

/** @brief 关闭并使连接标识失效，迟到RPC回调不会写入下一条连接. */
static void httpCloseConnection(SnfHttpConnection *connection)
{
    SnfHttpState *state = &http_state;
    SnfRpcChannelHandle rpc = connection->rpc;

    xSemaphoreTake(state->mutex, portMAX_DELAY);
    xSemaphoreTake(connection->send_mutex, portMAX_DELAY);
    connection->closing = true;
    lwip_shutdown(connection->socket, SHUT_RDWR);
    lwip_close(connection->socket);
    connection->socket = -1;
    connection->id = 0;
    xSemaphoreGive(connection->send_mutex);
    xSemaphoreGive(state->mutex);
    if (rpc != NULL)
    {
        snfRpcChannelUnregister(rpc);
    }
}

/** @brief 每条连接独立收包，WebSocket在线时监听任务仍可接收新连接. */
static void httpConnectionTask(void *arg)
{
    static const SnfWebsocketCallbacks callbacks =
    {
        .send_frame = httpWebsocketSend,
        .receive_text = httpWebsocketReceive,
    };

    SnfHttpConnection *connection = arg;
    SnfHttpReader reader = {.socket = connection->socket};
    SnfHttpRequest request;
    char *header = malloc(SNF_HTTP_HEADER_MAX_LENGTH + 1);
    int ret;

    if (header != NULL)
    {
        ret = snfHttpReadRequest(&reader, header, &request);
        if (ret != 0)
        {
            if (ret > 0)
            {
                httpSendResponse(connection->socket, ret, NULL, NULL, 0);
            }
        }
        else if (strcmp(request.uri, "/rpc") != 0)
        {
            httpSendResponse(connection->socket, 404, NULL, NULL, 0);
        }
        else if (strcmp(request.method, "POST") == 0)
        {
            httpHandleRpc(connection, &reader, &request);
        }
        else if (strcmp(request.method, "GET") == 0)
        {
            httpHandleWebsocket(connection, &request);
        }
        else
        {
            httpSendResponse(connection->socket, 405, "Allow: POST, GET\r\n", NULL, 0);
        }
        free(header);
        if (connection->websocket)
        {
            /* 升级后归还HTTP头缓冲，输入流保留预读帧字节。 */
            snfWebsocketRun(&reader, &callbacks, connection);
        }
    }
    else
    {
        httpSendResponse(connection->socket, 500, NULL, NULL, 0);
    }
    httpCloseConnection(connection);
    vTaskDelete(NULL);
}

/** @brief 为新socket分配连接槽；回调使用递增标识而不是socket编号. */
static SnfHttpConnection *httpAcquireConnection(int socket)
{
    SnfHttpState *state = &http_state;
    SnfHttpConnection *connection = NULL;

    xSemaphoreTake(state->mutex, portMAX_DELAY);
    for (uint32_t i = 0; i < HTTP_CONNECTION_COUNT; i++)
    {
        if (state->connections[i].id == 0)
        {
            connection = &state->connections[i];
            state->next_id++;
            if (state->next_id == 0)
            {
                state->next_id++;
            }
            connection->id = state->next_id;
            connection->socket = socket;
            connection->rpc = NULL;
            connection->websocket = false;
            connection->notify_ready = false;
            connection->response_done = false;
            connection->closing = false;
            xSemaphoreTake(connection->response_sem, 0);
            break;
        }
    }
    xSemaphoreGive(state->mutex);

    return connection;
}

/** @brief 监听任务只接收连接并创建连接任务. */
static void httpServerTask(void *arg)
{
    SnfHttpState *state = &http_state;

    while (true)
    {
        int socket = lwip_accept(state->listen_socket, NULL, NULL);
        if (socket >= 0)
        {
            struct timeval receive_timeout = {.tv_sec = HTTP_RECEIVE_TIMEOUT_MS / 1000};
            struct timeval send_timeout = {.tv_sec = HTTP_SEND_TIMEOUT_MS / 1000};
            SnfHttpConnection *connection;
            int ret = lwip_setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
            if (ret == 0)
            {
                ret = lwip_setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
            }
            connection = ret == 0 ? httpAcquireConnection(socket) : NULL;
            if (connection == NULL)
            {
                httpSendResponse(socket, 503, NULL, NULL, 0);
                lwip_close(socket);
            }
            else if (xTaskCreate(httpConnectionTask, "snf_http_conn", HTTP_CONNECTION_STACK,
                                 connection, SONOFF_HTTP_TASK_PRIO, NULL) != pdPASS)
            {
                httpCloseConnection(connection);
            }
        }
        else if (errno != EINTR)
        {
            LOG_W(tag, "accept failed, errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(HTTP_ACCEPT_RETRY_MS));
        }
    }
}

int snfHttpServerStart(void)
{
    SnfHttpState *state = &http_state;
    struct sockaddr_in address = {0};
    int reuse_address = 1;

    if (state->task != NULL)
    {
        return 0;
    }
    state->mutex = xSemaphoreCreateMutex();
    if (state->mutex == NULL)
    {
        return -1;
    }
    for (uint32_t i = 0; i < HTTP_CONNECTION_COUNT; i++)
    {
        state->connections[i].send_mutex = xSemaphoreCreateMutex();
        state->connections[i].response_sem = xSemaphoreCreateBinary();
        if ((state->connections[i].send_mutex == NULL) || (state->connections[i].response_sem == NULL))
        {
            goto cleanup;
        }
    }
    state->listen_socket = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (state->listen_socket < 0)
    {
        goto cleanup;
    }
    lwip_setsockopt(state->listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));
    address.sin_family = AF_INET;
    address.sin_port = PP_HTONS(HTTP_SERVER_PORT);
    address.sin_addr.s_addr = PP_HTONL(INADDR_ANY);
    if (lwip_bind(state->listen_socket, (const struct sockaddr *)&address, sizeof(address)) != 0)
    {
        goto cleanup;
    }
    if (lwip_listen(state->listen_socket, HTTP_CONNECTION_COUNT) != 0)
    {
        goto cleanup;
    }
    if (xTaskCreate(httpServerTask, SONOFF_HTTP_TASK_NAME, SONOFF_HTTP_TASK_STACKSIZE,
                    NULL, SONOFF_HTTP_TASK_PRIO, &state->task) != pdPASS)
    {
        goto cleanup;
    }
    LOG_I(tag, "RPC HTTP/WebSocket listening on port %d", HTTP_SERVER_PORT);

    return 0;

cleanup:
    if (state->listen_socket >= 0)
    {
        lwip_close(state->listen_socket);
        state->listen_socket = -1;
    }
    for (uint32_t i = 0; i < HTTP_CONNECTION_COUNT; i++)
    {
        if (state->connections[i].send_mutex != NULL)
        {
            vSemaphoreDelete(state->connections[i].send_mutex);
            state->connections[i].send_mutex = NULL;
        }
        if (state->connections[i].response_sem != NULL)
        {
            vSemaphoreDelete(state->connections[i].response_sem);
            state->connections[i].response_sem = NULL;
        }
    }
    vSemaphoreDelete(state->mutex);
    state->mutex = NULL;
    state->task = NULL;
    LOG_E(tag, "HTTP server start failed");

    return -1;
}
