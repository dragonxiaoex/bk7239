/**
 * @file    sonoff_rpc_client.c
 * @brief   RPC 本地客户端实现
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "cJSON.h"

#include "sonoff_log.h"
#include "sonoff_rpc.h"
#include "sonoff_rpc_client.h"

static const char *tag = "SNF-RPC-C";

#define RPC_CLIENT_NAME_MAX_LEN 64

typedef struct
{
    bool in_use;
    uint32_t request_id;
    bool is_sync;

    TimeOut_t timeout_state;    /* FreeRTOS 超时记录，包含 tick 回绕信息。 */
    TickType_t timeout_ticks;   /* 剩余等待 tick，每次超时检查会更新，不是绝对截止时间。 */

    SemaphoreHandle_t sync_sem;
    cJSON *sync_result;
    cJSON *sync_error;

    SnfRpcClientResponseHandler async_handler;
    void *async_user_ctx;
} SnfRpcClientPending;

typedef struct
{
    char client_name[RPC_CLIENT_NAME_MAX_LEN];
    char server_name[RPC_CLIENT_NAME_MAX_LEN];
    char notify_remote_src[RPC_CLIENT_NAME_MAX_LEN];
    uint32_t default_timeout_ms;

    SnfRpcChannelHandle channel_handle;
    SnfRpcClientState state;

    uint32_t next_request_id;

    uint16_t max_pending;
    SnfRpcClientPending *pending_list;

    SnfRpcClientNotifyHandler notify_handler;
    void *notify_user_ctx;

    SemaphoreHandle_t mutex;
} SnfRpcClient;

/**
 * @brief 查找指定 ID 的等待项，调用方须持有客户端互斥锁.
 */
static SnfRpcClientPending *rpcClientFindPendingNoLock(SnfRpcClient *client, uint32_t request_id)
{
    if (!client || request_id == 0)
    {
        return NULL;
    }

    for (uint16_t i = 0; i < client->max_pending; i++)
    {
        SnfRpcClientPending *pending = &client->pending_list[i];
        if (pending->in_use && pending->request_id == request_id)
        {
            return pending;
        }
    }

    return NULL;
}

/**
 * @brief 清理等待项的信号量和 JSON 副本，调用方须保证独占访问.
 */
static void rpcClientClearPendingNoLock(SnfRpcClientPending *pending)
{
    if (!pending)
    {
        return;
    }

    if (pending->sync_sem)
    {
        vSemaphoreDelete(pending->sync_sem);
        pending->sync_sem = NULL;
    }

    if (pending->sync_result)
    {
        cJSON_Delete(pending->sync_result);
        pending->sync_result = NULL;
    }

    if (pending->sync_error)
    {
        cJSON_Delete(pending->sync_error);
        pending->sync_error = NULL;
    }

    memset(pending, 0, sizeof(*pending));
}

/**
 * @brief 从 JSON 数字或数字字符串读取请求 ID.
 */
static int32_t rpcClientParseId(const cJSON *id_node, uint32_t *out_id)
{
    if (!id_node || !out_id)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (JSON_IS_NUMBER(id_node))
    {
        double num = id_node->valuedouble;
        if (num < 0)
        {
            return SNF_RPC_ERR_INVALID_ARG;
        }
        *out_id = (uint32_t)num;
        return SNF_RPC_OK;
    }

    if (JSON_IS_STRING(id_node))
    {
        const char *id_str = id_node->valuestring;
        if (!id_str || id_str[0] == '\0')
        {
            return SNF_RPC_ERR_INVALID_ARG;
        }

        char *end = NULL;
        unsigned long val = strtoul(id_str, &end, 10);
        if (end == id_str || (end && *end != '\0'))
        {
            return SNF_RPC_ERR_INVALID_ARG;
        }

        *out_id = (uint32_t)val;
        return SNF_RPC_OK;
    }

    return SNF_RPC_ERR_INVALID_ARG;
}

/**
 * @brief 解析下行报文，唤醒同步等待者或将 JSON 副本交给回调.
 */
static int32_t rpcClientDispatchInbound(SnfRpcClient *client, const char *message, uint32_t msg_len)
{
    cJSON *root = cJSON_Parse(message);
    if (!JSON_IS_OBJECT(root))
    {
        if (root)
        {
            cJSON_Delete(root);
        }
        return SNF_RPC_ERR_PARSE_ERROR;
    }

    cJSON *id_node = cJSON_GetObjectItem(root, "id");
    cJSON *method_node = cJSON_GetObjectItem(root, "method");
    cJSON *result_node = cJSON_GetObjectItem(root, "result");
    cJSON *error_node = cJSON_GetObjectItem(root, "error");

    if (id_node && (result_node || error_node))
    {
        uint32_t request_id = 0;
        if (rpcClientParseId(id_node, &request_id) != SNF_RPC_OK)
        {
            cJSON_Delete(root);
            return SNF_RPC_OK;
        }

        SnfRpcClientResponseHandler async_handler = NULL;
        void *async_ctx = NULL;
        cJSON *async_result = NULL;
        cJSON *async_error = NULL;
        int32_t callback_status = SNF_RPC_OK;

        xSemaphoreTake(client->mutex, portMAX_DELAY);
        SnfRpcClientPending *pending = rpcClientFindPendingNoLock(client, request_id);
        if (!pending)
        {
            xSemaphoreGive(client->mutex);
            cJSON_Delete(root);
            return SNF_RPC_OK;
        }

        if (pending->is_sync)
        {
            /* root 将在分发结束时删除，先保留独立副本，再唤醒同步调用方取走结果。 */
            if (result_node)
            {
                pending->sync_result = cJSON_Duplicate(result_node, true);
            }
            if (error_node)
            {
                pending->sync_error = cJSON_Duplicate(error_node, true);
            }
            xSemaphoreGive(pending->sync_sem);
        }
        else
        {
            if (result_node)
            {
                async_result = cJSON_Duplicate(result_node, true);
            }
            if (error_node)
            {
                async_error = cJSON_Duplicate(error_node, true);
            }
            if ((result_node && !async_result) || (error_node && !async_error))
            {
                callback_status = SNF_RPC_ERR_NO_MEMORY;
            }

            async_handler = pending->async_handler;
            async_ctx = pending->async_user_ctx;
            rpcClientClearPendingNoLock(pending);
        }
        xSemaphoreGive(client->mutex);

        /* 等待槽已清理且互斥锁已释放，回调可以提交新请求；结果副本的所有权交给回调。 */
        if (async_handler)
        {
            async_handler(callback_status, request_id, async_result, async_error, async_ctx);
        }
        else
        {
            if (async_result)
            {
                cJSON_Delete(async_result);
            }
            if (async_error)
            {
                cJSON_Delete(async_error);
            }
        }

        cJSON_Delete(root);
        return SNF_RPC_OK;
    }

    if (JSON_IS_STRING(method_node))
    {
        const char *method = method_node->valuestring;
        cJSON *params = cJSON_GetObjectItem(root, "params");

        SnfRpcClientNotifyHandler notify_handler = NULL;
        void *notify_user_ctx = NULL;
        cJSON *params_copy = NULL;

        xSemaphoreTake(client->mutex, portMAX_DELAY);
        notify_handler = client->notify_handler;
        notify_user_ctx = client->notify_user_ctx;
        xSemaphoreGive(client->mutex);

        if (notify_handler)
        {
            /* params 副本交给回调释放；method 仍指向 root，只在本次回调期间有效。 */
            if (params)
            {
                params_copy = cJSON_Duplicate(params, true);
            }
            notify_handler(method, params_copy, notify_user_ctx);
        }

        cJSON_Delete(root);
        return SNF_RPC_OK;
    }

    cJSON_Delete(root);

    return SNF_RPC_OK;
}

/**
 * @brief 作为本地通道发送回调，将响应或通知交给客户端分发.
 */
static int32_t rpcClientChannelSend(void *handle, const char *message, uint32_t msg_len)
{
    if (!handle || !message || msg_len == 0)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)handle;

    return rpcClientDispatchInbound(client, message, msg_len);
}

/**
 * @brief 复制参数并序列化请求，成功时由调用方释放输出字符串.
 */
static int32_t rpcClientBuildRequest(SnfRpcClient *client,
                                     uint32_t request_id,
                                     const char *method,
                                     cJSON *params,
                                     char **out_json)
{
    if (!client || !method || !out_json)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    *out_json = NULL;

    cJSON *request = cJSON_CreateObject();
    if (!request)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    cJSON_AddNumberToObject(request, "id", (double)request_id);
    cJSON_AddStringToObject(request, "src", client->client_name);
    cJSON_AddStringToObject(request, "dst", client->server_name);
    cJSON_AddStringToObject(request, "method", method);

    if (params)
    {
        cJSON *params_copy = cJSON_Duplicate(params, true);
        if (!params_copy)
        {
            cJSON_Delete(request);
            return SNF_RPC_ERR_NO_MEMORY;
        }
        cJSON_AddItemToObject(request, "params", params_copy);
    }

    char *json = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    if (!json)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    *out_json = json;

    return SNF_RPC_OK;
}

/**
 * @brief 占用空闲等待槽位，调用方须持有客户端互斥锁.
 */
static SnfRpcClientPending *rpcClientAcquirePending(SnfRpcClient *client, uint32_t request_id, bool is_sync)
{
    if (!client)
    {
        return NULL;
    }

    for (uint16_t i = 0; i < client->max_pending; i++)
    {
        if (!client->pending_list[i].in_use)
        {
            SnfRpcClientPending *pending = &client->pending_list[i];
            memset(pending, 0, sizeof(*pending));
            pending->in_use = true;
            pending->request_id = request_id;
            pending->is_sync = is_sync;
            return pending;
        }
    }

    return NULL;
}

int32_t snfRpcClientCreate(const SnfRpcClientConfig *config, SnfRpcClientHandle *out_client)
{
    if (!config || !out_client || !config->client_name || !config->server_name)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    size_t client_len = strlen(config->client_name);
    size_t server_len = strlen(config->server_name);
    if (client_len == 0 || server_len == 0 || client_len >= RPC_CLIENT_NAME_MAX_LEN
            || server_len >= RPC_CLIENT_NAME_MAX_LEN)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)calloc(1, sizeof(SnfRpcClient));
    if (!client)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    client->mutex = xSemaphoreCreateMutex();
    if (!client->mutex)
    {
        free(client);
        return SNF_RPC_ERR_NO_MEMORY;
    }

    uint16_t max_pending = config->max_pending;
    if (max_pending == 0)
    {
        max_pending = CONFIG_SNF_RPC_CLIENT_MAX_PENDING;
    }

    client->pending_list = (SnfRpcClientPending *)calloc(max_pending, sizeof(SnfRpcClientPending));
    if (!client->pending_list)
    {
        vSemaphoreDelete(client->mutex);
        free(client);
        return SNF_RPC_ERR_NO_MEMORY;
    }

    strncpy(client->client_name, config->client_name, sizeof(client->client_name) - 1);
    client->client_name[sizeof(client->client_name) - 1] = '\0';

    strncpy(client->server_name, config->server_name, sizeof(client->server_name) - 1);
    client->server_name[sizeof(client->server_name) - 1] = '\0';

    if (config->notify_remote_src && config->notify_remote_src[0] != '\0')
    {
        strncpy(client->notify_remote_src, config->notify_remote_src, sizeof(client->notify_remote_src) - 1);
        client->notify_remote_src[sizeof(client->notify_remote_src) - 1] = '\0';
    }
    else
    {
        strncpy(client->notify_remote_src, client->client_name, sizeof(client->notify_remote_src) - 1);
        client->notify_remote_src[sizeof(client->notify_remote_src) - 1] = '\0';
    }

    client->default_timeout_ms = config->default_timeout_ms ? config->default_timeout_ms :
                                 CONFIG_SNF_RPC_CLIENT_DEFAULT_TIMEOUT_MS;
    client->max_pending = max_pending;
    client->state = SNF_RPC_CLIENT_DISCONNECTED;
    client->next_request_id = 1;

    *out_client = (SnfRpcClientHandle)client;

    return SNF_RPC_OK;
}

int32_t snfRpcClientDestroy(SnfRpcClientHandle client_handle)
{
    if (!client_handle)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;

    snfRpcClientClose(client_handle);

    if (client->pending_list)
    {
        for (uint16_t i = 0; i < client->max_pending; i++)
        {
            rpcClientClearPendingNoLock(&client->pending_list[i]);
        }
        free(client->pending_list);
        client->pending_list = NULL;
    }

    if (client->mutex)
    {
        vSemaphoreDelete(client->mutex);
        client->mutex = NULL;
    }

    free(client);

    return SNF_RPC_OK;
}

int32_t snfRpcClientConnect(SnfRpcClientHandle client_handle)
{
    if (!client_handle)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    if (client->state == SNF_RPC_CLIENT_CONNECTED)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_OK;
    }
    xSemaphoreGive(client->mutex);

    char channel_name[RPC_CLIENT_NAME_MAX_LEN + 24];
    snprintf(channel_name, sizeof(channel_name), "rpc_client_%s_%p", client->client_name, (void *)client);

    SnfRpcChannelConfig cfg =
    {
        .channel_name = channel_name,
        .transport_type = SNF_RPC_TRANSPORT_LOCAL,
        .conn_mode = SNF_RPC_CONN_PERSISTENT,
        .channel_handle = client,
        .send_cb = rpcClientChannelSend,
        .auth_supported = false,
    };

    SnfRpcChannelHandle channel_handle = NULL;
    int32_t ret = snfRpcChannelRegister(&cfg, &channel_handle);
    if (ret != SNF_RPC_OK)
    {
        return ret;
    }

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    client->channel_handle = channel_handle;
    client->state = SNF_RPC_CLIENT_CONNECTED;
    xSemaphoreGive(client->mutex);

    ret = snfRpcChannelSetNotifyRemoteSrc(channel_handle,
                                          client->notify_remote_src[0] ? client->notify_remote_src : client->client_name);
    if (ret != SNF_RPC_OK)
    {
        xSemaphoreTake(client->mutex, portMAX_DELAY);
        client->state = SNF_RPC_CLIENT_DISCONNECTED;
        client->channel_handle = NULL;
        xSemaphoreGive(client->mutex);
        snfRpcChannelUnregister(channel_handle);
        return ret;
    }

    LOG_I(tag, "rpc client connected: %s", client->client_name);

    return SNF_RPC_OK;
}

int32_t snfRpcClientClose(SnfRpcClientHandle client_handle)
{
    if (!client_handle)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;
    SnfRpcChannelHandle channel_to_close = NULL;

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    if (client->state != SNF_RPC_CLIENT_CONNECTED)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_OK;
    }

    client->state = SNF_RPC_CLIENT_DISCONNECTED;
    channel_to_close = client->channel_handle;
    client->channel_handle = NULL;

    for (uint16_t i = 0; i < client->max_pending; i++)
    {
        SnfRpcClientPending *pending = &client->pending_list[i];
        if (!pending->in_use)
        {
            continue;
        }

        if (pending->is_sync)
        {
            xSemaphoreGive(pending->sync_sem);
        }
        else if (pending->async_handler)
        {
            SnfRpcClientResponseHandler async_handler = pending->async_handler;
            uint32_t req_id = pending->request_id;
            void *async_ctx = pending->async_user_ctx;
            rpcClientClearPendingNoLock(pending);
            xSemaphoreGive(client->mutex);
            async_handler(SNF_RPC_ERR_INVALID_STATE, req_id, NULL, NULL, async_ctx);
            xSemaphoreTake(client->mutex, portMAX_DELAY);
        }
    }

    xSemaphoreGive(client->mutex);

    if (channel_to_close)
    {
        return snfRpcChannelUnregister(channel_to_close);
    }

    return SNF_RPC_OK;
}

SnfRpcClientState snfRpcClientGetState(SnfRpcClientHandle client_handle)
{
    if (!client_handle)
    {
        return SNF_RPC_CLIENT_DISCONNECTED;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    SnfRpcClientState state = client->state;
    xSemaphoreGive(client->mutex);

    return state;
}

int32_t snfRpcClientSetNotifyHandler(SnfRpcClientHandle client_handle,
                                     SnfRpcClientNotifyHandler handler,
                                     void *user_ctx)
{
    if (!client_handle)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    client->notify_handler = handler;
    client->notify_user_ctx = user_ctx;
    xSemaphoreGive(client->mutex);

    return SNF_RPC_OK;
}

int32_t snfRpcClientSetNotifyRemoteSrc(SnfRpcClientHandle client_handle,
                                       const char *notify_remote_src)
{
    if (!client_handle || !notify_remote_src || notify_remote_src[0] == '\0')
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;
    SnfRpcChannelHandle channel_handle = NULL;

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    strncpy(client->notify_remote_src, notify_remote_src, sizeof(client->notify_remote_src) - 1);
    client->notify_remote_src[sizeof(client->notify_remote_src) - 1] = '\0';
    channel_handle = client->channel_handle;
    xSemaphoreGive(client->mutex);

    if (channel_handle != NULL)
    {
        return snfRpcChannelSetNotifyRemoteSrc(channel_handle, client->notify_remote_src);
    }

    return SNF_RPC_OK;
}

int32_t snfRpcClientCallAsync(SnfRpcClientHandle client_handle,
                              const char *method,
                              cJSON *params,
                              uint32_t timeout_ms,
                              SnfRpcClientResponseHandler response_handler,
                              void *user_ctx,
                              uint32_t *out_request_id)
{
    if (!client_handle || !method || !response_handler)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;

    if (timeout_ms == 0)
    {
        timeout_ms = client->default_timeout_ms;
    }

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    if (client->state != SNF_RPC_CLIENT_CONNECTED || !client->channel_handle)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_ERR_INVALID_STATE;
    }

    uint32_t request_id = client->next_request_id++;
    if (client->next_request_id == 0)
    {
        client->next_request_id = 1;
    }

    SnfRpcClientPending *pending = rpcClientAcquirePending(client, request_id, false);
    if (!pending)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_ERR_QUEUE_FULL;
    }

    /* 先登记等待项再投递请求，避免快速响应到达时尚无关联记录；异步超时从此处开始计时。 */
    pending->timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    vTaskSetTimeOutState(&pending->timeout_state);
    pending->async_handler = response_handler;
    pending->async_user_ctx = user_ctx;

    SnfRpcChannelHandle channel_handle = client->channel_handle;
    xSemaphoreGive(client->mutex);

    char *request_json = NULL;
    int32_t ret = rpcClientBuildRequest(client, request_id, method, params, &request_json);
    if (ret != SNF_RPC_OK)
    {
        xSemaphoreTake(client->mutex, portMAX_DELAY);
        SnfRpcClientPending *failed_pending = rpcClientFindPendingNoLock(client, request_id);
        if (failed_pending)
        {
            rpcClientClearPendingNoLock(failed_pending);
        }
        xSemaphoreGive(client->mutex);
        return ret;
    }

    ret = snfRpcChannelHandleRequest(channel_handle, request_json);
    free(request_json);

    if (ret != SNF_RPC_OK)
    {
        xSemaphoreTake(client->mutex, portMAX_DELAY);
        SnfRpcClientPending *failed_pending = rpcClientFindPendingNoLock(client, request_id);
        if (failed_pending)
        {
            rpcClientClearPendingNoLock(failed_pending);
        }
        xSemaphoreGive(client->mutex);
        return ret;
    }

    if (out_request_id)
    {
        *out_request_id = request_id;
    }

    return SNF_RPC_OK;
}

int32_t snfRpcClientCallSync(SnfRpcClientHandle client_handle,
                             const char *method,
                             cJSON *params,
                             uint32_t timeout_ms,
                             cJSON **out_result,
                             cJSON **out_error)
{
    if (!client_handle || !method)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (out_result)
    {
        *out_result = NULL;
    }
    if (out_error)
    {
        *out_error = NULL;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;
    if (timeout_ms == 0)
    {
        timeout_ms = client->default_timeout_ms;
    }

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    if (client->state != SNF_RPC_CLIENT_CONNECTED || !client->channel_handle)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_ERR_INVALID_STATE;
    }

    uint32_t request_id = client->next_request_id++;
    if (client->next_request_id == 0)
    {
        client->next_request_id = 1;
    }

    SnfRpcClientPending *pending = rpcClientAcquirePending(client, request_id, true);
    if (!pending)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_ERR_QUEUE_FULL;
    }

    pending->timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    vTaskSetTimeOutState(&pending->timeout_state);

    /* 新建二值信号量初始为空，只有响应、取消或关闭等事件才能唤醒此次等待。 */
    pending->sync_sem = xSemaphoreCreateBinary();
    if (!pending->sync_sem)
    {
        rpcClientClearPendingNoLock(pending);
        xSemaphoreGive(client->mutex);
        return SNF_RPC_ERR_NO_MEMORY;
    }

    SnfRpcChannelHandle channel_handle = client->channel_handle;
    xSemaphoreGive(client->mutex);

    char *request_json = NULL;
    int32_t ret = rpcClientBuildRequest(client, request_id, method, params, &request_json);
    if (ret != SNF_RPC_OK)
    {
        xSemaphoreTake(client->mutex, portMAX_DELAY);
        SnfRpcClientPending *failed_pending = rpcClientFindPendingNoLock(client, request_id);
        if (failed_pending)
        {
            rpcClientClearPendingNoLock(failed_pending);
        }
        xSemaphoreGive(client->mutex);
        return ret;
    }

    ret = snfRpcChannelHandleRequest(channel_handle, request_json);
    free(request_json);

    if (ret != SNF_RPC_OK)
    {
        xSemaphoreTake(client->mutex, portMAX_DELAY);
        SnfRpcClientPending *failed_pending = rpcClientFindPendingNoLock(client, request_id);
        if (failed_pending)
        {
            rpcClientClearPendingNoLock(failed_pending);
        }
        xSemaphoreGive(client->mutex);
        return ret;
    }

    /* 等待期间不能持有 client->mutex，否则响应分发无法加锁保存结果并发送信号。 */
    BaseType_t wait_result = xSemaphoreTake(pending->sync_sem, pdMS_TO_TICKS(timeout_ms));

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    SnfRpcClientPending *done = rpcClientFindPendingNoLock(client, request_id);
    if (!done)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_ERR_TIMEOUT;
    }

    cJSON *result = done->sync_result;
    cJSON *error = done->sync_error;

    /* 先取走 JSON 所有权并置空，再清理等待项，避免清理函数删除将交给调用方的结果。 */
    done->sync_result = NULL;
    done->sync_error = NULL;
    rpcClientClearPendingNoLock(done);
    xSemaphoreGive(client->mutex);

    if (wait_result != pdPASS)
    {
        if (result)
        {
            cJSON_Delete(result);
        }
        if (error)
        {
            cJSON_Delete(error);
        }
        return SNF_RPC_ERR_TIMEOUT;
    }

    if (!result && !error)
    {
        return SNF_RPC_ERR_INVALID_STATE;
    }

    if (out_result)
    {
        *out_result = result;
    }
    else if (result)
    {
        cJSON_Delete(result);
    }

    if (out_error)
    {
        *out_error = error;
    }
    else if (error)
    {
        cJSON_Delete(error);
    }

    return SNF_RPC_OK;
}

int32_t snfRpcClientCancelPending(SnfRpcClientHandle client_handle, uint32_t request_id)
{
    if (!client_handle || request_id == 0)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;

    SnfRpcClientResponseHandler async_handler = NULL;
    void *async_ctx = NULL;
    bool is_sync = false;

    xSemaphoreTake(client->mutex, portMAX_DELAY);
    SnfRpcClientPending *pending = rpcClientFindPendingNoLock(client, request_id);
    if (!pending)
    {
        xSemaphoreGive(client->mutex);
        return SNF_RPC_ERR_NOT_FOUND;
    }

    is_sync = pending->is_sync;
    if (is_sync)
    {
        xSemaphoreGive(pending->sync_sem);
    }
    else
    {
        async_handler = pending->async_handler;
        async_ctx = pending->async_user_ctx;
        rpcClientClearPendingNoLock(pending);
    }

    xSemaphoreGive(client->mutex);

    if (async_handler)
    {
        async_handler(SNF_RPC_ERR_TIMEOUT, request_id, NULL, NULL, async_ctx);
    }

    return SNF_RPC_OK;
}

int32_t snfRpcClientPollTimeouts(SnfRpcClientHandle client_handle)
{
    if (!client_handle)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcClient *client = (SnfRpcClient *)client_handle;
    uint32_t timeout_count = 0;

    for (uint16_t i = 0; i < client->max_pending; i++)
    {
        SnfRpcClientResponseHandler async_handler = NULL;
        void *async_ctx = NULL;
        uint32_t request_id = 0;
        bool is_timeout = false;

        xSemaphoreTake(client->mutex, portMAX_DELAY);
        SnfRpcClientPending *pending = &client->pending_list[i];
        /* FreeRTOS 同时更新计时记录和剩余 tick；轮询时不能重新设置初始超时时长。 */
        if (pending->in_use && xTaskCheckForTimeOut(&pending->timeout_state, &pending->timeout_ticks) != pdFALSE)
        {
            request_id = pending->request_id;
            if (pending->is_sync)
            {
                xSemaphoreGive(pending->sync_sem);
            }
            else
            {
                async_handler = pending->async_handler;
                async_ctx = pending->async_user_ctx;
                rpcClientClearPendingNoLock(pending);
            }
            is_timeout = true;
        }
        xSemaphoreGive(client->mutex);

        if (is_timeout)
        {
            timeout_count++;
            if (async_handler)
            {
                async_handler(SNF_RPC_ERR_TIMEOUT, request_id, NULL, NULL, async_ctx);
            }
        }
    }

    return (int32_t)timeout_count;
}
