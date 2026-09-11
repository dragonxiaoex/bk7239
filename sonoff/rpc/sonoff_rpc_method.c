/**
 * @file    sonoff_rpc_method.c
 * @brief   RPC 方法注册和管理实现
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <string.h>
#include <stdlib.h>

#include "sonoff_log.h"
#include "sonoff_rpc_internal.h"

static const char *tag = "SNF-RPC-METHOD";

int32_t snfRpcMethodRegister(const char *method_name, SnfRpcMethodHandler handler, void *user_ctx)
{
    LOG_I(tag, "snfRpcMethodRegister: method_name=%s", method_name ? method_name : "NULL");

    if (!method_name || !handler)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (strlen(method_name) >= RPC_MAX_METHOD_NAME_LEN)
    {
        LOG_W(tag, "snfRpcMethodRegister: Method name too long");
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    /* 分配方法节点（在检查重复之前，避免在持锁期间分配内存）*/
    SnfRpcMethodNode *method = malloc(sizeof(SnfRpcMethodNode));
    if (!method)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    /* 初始化方法 */
    memset(method, 0, sizeof(SnfRpcMethodNode));
    strncpy(method->method_name, method_name, RPC_MAX_METHOD_NAME_LEN - 1);
    method->method_name[RPC_MAX_METHOD_NAME_LEN - 1] = '\0';

    method->handler = handler;
    method->user_ctx = user_ctx;

    /* 重复检查和链表插入必须处于同一次加锁范围，避免并发注册同名方法。 */
    xSemaphoreTake(bus->method_mutex, portMAX_DELAY);

    /* 当前已持有非递归互斥锁，查找时使用 NoLock 版本，避免重复加锁。 */
    if (snfRpcMethodFindNoLock(method_name))
    {
        xSemaphoreGive(bus->method_mutex);
        free(method);
        LOG_W(tag, "snfRpcMethodRegister: Method %s already registered", method_name);
        return SNF_RPC_ERR_INVALID_REQUEST;
    }

    /* 添加到方法列表 */
    method->next = bus->method_list;
    bus->method_list = method;
    xSemaphoreGive(bus->method_mutex);

    LOG_I(tag, "Method %s registered", method_name);

    return SNF_RPC_OK;
}

int32_t snfRpcMethodUnregister(const char *method_name)
{
    LOG_I(tag, "snfRpcMethodUnregister: method_name=%s", method_name ? method_name : "NULL");

    if (!method_name)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->method_mutex, portMAX_DELAY);

    SnfRpcMethodNode *method = bus->method_list;
    SnfRpcMethodNode *prev = NULL;

    while (method)
    {
        if (strcmp(method->method_name, method_name) == 0)
        {
            if (prev)
            {
                prev->next = method->next;
            }
            else
            {
                bus->method_list = method->next;
            }

            xSemaphoreGive(bus->method_mutex);
            free(method);

            LOG_I(tag, "Method %s unregistered", method_name);
            return SNF_RPC_OK;
        }

        prev = method;
        method = method->next;
    }

    xSemaphoreGive(bus->method_mutex);

    return SNF_RPC_ERR_NOT_FOUND;
}

int32_t snfRpcMethodEnumerate(const char *filter, SnfRpcMethodEnumCb cb, void *ctx)
{
    if (!cb)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    size_t filter_len = (filter != NULL) ? strlen(filter) : 0;

    xSemaphoreTake(bus->method_mutex, portMAX_DELAY);

    for (SnfRpcMethodNode *method = bus->method_list; method != NULL; method = method->next)
    {
        if (filter_len > 0 && strncmp(method->method_name, filter, filter_len) != 0)
        {
            continue;
        }

        cb(method->method_name, ctx);
    }

    xSemaphoreGive(bus->method_mutex);

    return SNF_RPC_OK;
}
