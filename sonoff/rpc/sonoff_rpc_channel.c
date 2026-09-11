/**
 * @file    sonoff_rpc_channel.c
 * @brief   RPC 通道管理实现
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

#include "sonoff_log.h"
#include "sonoff_rpc_internal.h"

static const char *tag = "SNF-RPC-CH";

int32_t snfRpcChannelNotify(SnfRpcChannelHandle channel_handle, const char *method, cJSON *params)
{
    LOG_I(tag, "snfRpcChannelNotify: method=%s", method ? method : "NULL");

    if (!channel_handle || !method)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    SnfRpcChannelNode *channel = snfRpcChannelFind(channel_handle);
    if (!channel)
    {
        return SNF_RPC_ERR_NOT_FOUND;
    }

    const char *dst = NULL;

    if (channel->notify_remote_src_len > 0)
    {
        dst = channel->notify_remote_src;
    }
    else if (channel->remote_src_len > 0)
    {
        dst = channel->remote_src;
    }

    if (dst == NULL)
    {
        LOG_W(tag, "snfRpcChannelNotify: No notify/remote dst cached on channel");
        return SNF_RPC_ERR_INVALID_STATE;
    }

    /* 构建通知 JSON */
    cJSON *notification = cJSON_CreateObject();
    if (!notification)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(notification, "src", bus->src_name);
    cJSON_AddStringToObject(notification, "dst", dst);
    cJSON_AddStringToObject(notification, "method", method);

    /* 复制 params 而不是直接添加（避免所有权混乱）*/
    if (params)
    {
        cJSON *params_copy = cJSON_Duplicate(params, true);
        if (params_copy)
        {
            cJSON_AddItemToObject(notification, "params", params_copy);
        }
        else
        {
            LOG_W(tag, "snfRpcChannelNotify: Failed to duplicate params");
            /* 继续发送没有params的通知 */
        }
    }

    int32_t ret = snfRpcSendJsonMessage(channel, notification);
    cJSON_Delete(notification);

    return ret;
}

int32_t snfRpcBroadcastNotify(const char *method, cJSON *params)
{
    LOG_I(tag, "snfRpcBroadcastNotify: method=%s", method ? method : "NULL");

    if (!method)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    /* 临时缓存：存储需要发送的通道信息 */
    typedef struct
    {
        char channel_name[RPC_MAX_CHANNEL_NAME_LEN];
        char dst[RPC_MAX_SRC_LEN];
        void *channel_handle;
        SnfRpcChannelSend send_cb;
    } BroadcastTarget;

    BroadcastTarget *targets = NULL;
    int target_count = 0;
    int32_t ret = SNF_RPC_OK;

    /* 锁内复制目标信息，锁外发送，避免耗时的传输回调阻塞通道管理。 */
    xSemaphoreTake(bus->channel_mutex, portMAX_DELAY);

    SnfRpcChannelNode *channel = bus->channel_list;
    int channel_count = 0;

    /* 先计数 */
    while (channel)
    {
        if ((channel->notify_remote_src_len > 0 || channel->remote_src_len > 0) &&
                channel->conn_mode == SNF_RPC_CONN_PERSISTENT)
        {
            channel_count++;
        }
        channel = channel->next;
    }

    if (channel_count > 0)
    {
        /* 分配缓存 */
        targets = malloc(channel_count * sizeof(BroadcastTarget));
        if (targets)
        {
            /* 再次遍历，复制持久化通道信息 */
            channel = bus->channel_list;
            while (channel)
            {
                if ((channel->notify_remote_src_len > 0 || channel->remote_src_len > 0) &&
                        channel->conn_mode == SNF_RPC_CONN_PERSISTENT)
                {
                    strncpy(targets[target_count].channel_name, channel->channel_name, RPC_MAX_CHANNEL_NAME_LEN - 1);
                    targets[target_count].channel_name[RPC_MAX_CHANNEL_NAME_LEN - 1] = '\0';

                    if (channel->notify_remote_src_len > 0)
                    {
                        strncpy(targets[target_count].dst, channel->notify_remote_src, RPC_MAX_SRC_LEN - 1);
                    }
                    else
                    {
                        strncpy(targets[target_count].dst, channel->remote_src, RPC_MAX_SRC_LEN - 1);
                    }
                    targets[target_count].dst[RPC_MAX_SRC_LEN - 1] = '\0';

                    /* 只借用传输层句柄，快照不会延长外部上下文的生命周期。 */
                    targets[target_count].channel_handle = channel->channel_handle;
                    targets[target_count].send_cb = channel->send_cb;

                    target_count++;
                }
                channel = channel->next;
            }
        }
    }

    xSemaphoreGive(bus->channel_mutex);

    /* 检查是否分配内存失败 */
    if (channel_count > 0 && !targets)
    {
        LOG_E(tag, "snfRpcBroadcastNotify: Failed to allocate targets buffer");
        return SNF_RPC_ERR_NO_MEMORY;
    }

    /* 解锁后，为每个目标通道构建并发送通知 */
    int sent_count = 0;

    for (int i = 0; i < target_count; i++)
    {
        /* 构建通知 JSON */
        cJSON *notification = cJSON_CreateObject();
        if (!notification)
        {
            LOG_W(tag, "snfRpcBroadcastNotify: Failed to create notification JSON for channel %s",
                  targets[i].channel_name);
            continue;  /* 继续处理其他通道 */
        }

        cJSON_AddStringToObject(notification, "src", bus->src_name);
        cJSON_AddStringToObject(notification, "dst", targets[i].dst);
        cJSON_AddStringToObject(notification, "method", method);

        /* 复制 params 而不是直接添加（避免所有权混乱）*/
        if (params)
        {
            cJSON *params_copy = cJSON_Duplicate(params, true);
            if (params_copy)
            {
                cJSON_AddItemToObject(notification, "params", params_copy);
            }
            else
            {
                LOG_W(tag, "snfRpcBroadcastNotify: Failed to duplicate params for channel %s",
                      targets[i].channel_name);
                /* 继续发送没有params的通知 */
            }
        }

        /* 将 JSON 序列化为字符串 */
        char *json_str = cJSON_PrintUnformatted(notification);
        if (json_str)
        {
            size_t json_len = strlen(json_str);
            int32_t send_ret = targets[i].send_cb(targets[i].channel_handle, json_str, (uint32_t)json_len);
            if (send_ret == SNF_RPC_OK)
            {
                sent_count++;
                LOG_I(tag, "snfRpcBroadcastNotify: Message sent to channel %s",
                      targets[i].channel_name);
            }
            else
            {
                LOG_W(tag, "snfRpcBroadcastNotify: Send to channel %s failed, ret=%d",
                      targets[i].channel_name, send_ret);
            }

            /* cJSON_PrintUnformatted 返回的字符串需要被释放 */
            free(json_str);
        }
        else
        {
            LOG_W(tag, "snfRpcBroadcastNotify: Failed to serialize JSON for channel %s",
                  targets[i].channel_name);
        }

        cJSON_Delete(notification);
    }

    /* 确保在所有路径都释放 targets */
    if (targets)
    {
        free(targets);
    }

    if (sent_count == 0 && target_count > 0)
    {
        return SNF_RPC_ERR_INVALID_STATE;
    }

    return ret;
}

int32_t snfRpcNotifyByChannelName(const char *channel_name,
                                  const char *method,
                                  cJSON *params)
{
    LOG_I(tag, "snfRpcNotifyByChannelName: channel_name=%s, method=%s",
          channel_name ? channel_name : "NULL", method ? method : "NULL");

    if (!channel_name || !method)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->channel_mutex, portMAX_DELAY);

    SnfRpcChannelNode *channel = bus->channel_list;
    SnfRpcChannelNode *target = NULL;
    while (channel)
    {
        if (strcmp(channel->channel_name, channel_name) == 0)
        {
            target = channel;
            break;
        }
        channel = channel->next;
    }

    xSemaphoreGive(bus->channel_mutex);

    if (!target)
    {
        LOG_W(tag, "snfRpcNotifyByChannelName: channel %s not found", channel_name);
        return SNF_RPC_ERR_NOT_FOUND;
    }

    const char *dst = NULL;
    if (target->notify_remote_src_len > 0)
    {
        dst = target->notify_remote_src;
    }
    else if (target->remote_src_len > 0)
    {
        dst = target->remote_src;
    }

    if (dst == NULL)
    {
        LOG_W(tag, "snfRpcNotifyByChannelName: No notify/remote dst cached on channel %s", channel_name);
        return SNF_RPC_ERR_INVALID_STATE;
    }

    cJSON *notification = cJSON_CreateObject();
    if (!notification)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(notification, "src", bus->src_name);
    cJSON_AddStringToObject(notification, "dst", dst);
    cJSON_AddStringToObject(notification, "method", method);

    if (params)
    {
        cJSON *params_copy = cJSON_Duplicate(params, true);
        if (params_copy)
        {
            cJSON_AddItemToObject(notification, "params", params_copy);
        }
    }

    int32_t ret = snfRpcSendJsonMessage(target, notification);
    cJSON_Delete(notification);

    return ret;
}
