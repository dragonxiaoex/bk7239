/**
 * @file    sonoff_rpc_core.c
 * @brief   RPC 模块核心实现 - 主总线逻辑和工作线程
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "cJSON.h"

#include "sonoff_log.h"
#include "sonoff_task_def.h"
#include "sonoff_rpc.h"
#include "sonoff_rpc_internal.h"

static const char *tag = "SNF-RPC-CORE";

#define RPC_SLOW_REQUEST_MS                200
#define RPC_WORKER_QUEUE_TIMEOUT_MS        1000
#define RPC_WORKER_STOP_WAIT_MS            2000
#define RPC_WORKER_STOP_POLL_INTERVAL_MS   10

/* 全局 RPC 总线实例 */
static SnfRpcBus rpc_bus = {0};

/**
 * @brief 查找当前任务的请求上下文槽位，允许时返回空槽位.
 */
static SnfRpcCurrentRequestSlot *rpcCurrentRequestFindSlot(TaskHandle_t task, bool allow_empty)
{
    SnfRpcBus *bus = &rpc_bus;

    SnfRpcCurrentRequestSlot *empty_slot = NULL;

    for (uint32_t i = 0; i < RPC_CURRENT_REQUEST_SLOT_COUNT; ++i)
    {
        if (bus->current_request_slots[i].in_use)
        {
            if (bus->current_request_slots[i].task == task)
            {
                return &bus->current_request_slots[i];
            }
            continue;
        }

        if ((empty_slot == NULL) && allow_empty)
        {
            empty_slot = &bus->current_request_slots[i];
        }
    }

    return empty_slot;
}

/**
 * @brief 从双层缓冲池获取请求缓冲区
 * @param required_size 需要的缓冲区大小
 * @param out_index 输出缓冲区索引（0xFF=大缓冲区）
 * @param out_buf 输出缓冲区指针
 * @return SNF_RPC_OK 成功，其他为错误码
 */
static int rpcReqPoolAcquire(uint32_t required_size, uint16_t *out_index, char **out_buf)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!out_index || !out_buf)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    /* 计数信号量表示空闲小缓冲区数量，互斥锁保护具体槽位；无空闲槽时立即拒绝请求。 */
    if (required_size <= RPC_SMALL_REQ_SIZE)
    {
        if (!bus->req_pool_mutex || !bus->req_pool_sem)
        {
            return SNF_RPC_ERR_INTERNAL;
        }

        if (xSemaphoreTake(bus->req_pool_sem, 0) != pdPASS)
        {
            return SNF_RPC_ERR_SERVER_BUSY;
        }

        xSemaphoreTake(bus->req_pool_mutex, portMAX_DELAY);
        for (uint16_t i = 0; i < RPC_SMALL_POOL_SIZE; i++)
        {
            if (!bus->small_req_pool_in_use[i])
            {
                bus->small_req_pool_in_use[i] = true;
                *out_index = i;
                *out_buf = bus->small_req_pool[i];
                xSemaphoreGive(bus->req_pool_mutex);
                return SNF_RPC_OK;
            }
        }
        xSemaphoreGive(bus->req_pool_mutex);
        xSemaphoreGive(bus->req_pool_sem);
        return SNF_RPC_ERR_SERVER_BUSY;
    }

    /* 慢速路径：大缓冲区（按需分配） */
    if (required_size > RPC_LARGE_REQ_SIZE)
    {
        LOG_E(tag, "Request size %u exceeds max %u", required_size, RPC_LARGE_REQ_SIZE);
        return SNF_RPC_ERR_INVALID_SIZE;
    }

    if (!bus->large_req_mutex)
    {
        return SNF_RPC_ERR_INTERNAL;
    }

    xSemaphoreTake(bus->large_req_mutex, portMAX_DELAY);

    if (bus->large_req_in_use)
    {
        xSemaphoreGive(bus->large_req_mutex);
        return SNF_RPC_ERR_SERVER_BUSY;
    }

    /* 大缓冲区首次使用时分配，后续请求复用；释放占用时不归还堆内存，直到总线反初始化。 */
    if (!bus->large_req_allocated)
    {
        bus->large_req_buffer = (char *)malloc(RPC_LARGE_REQ_SIZE + 1);
        if (!bus->large_req_buffer)
        {
            xSemaphoreGive(bus->large_req_mutex);
            LOG_E(tag, "Failed to allocate large request buffer");
            return SNF_RPC_ERR_NO_MEMORY;
        }
        bus->large_req_allocated = true;
        LOG_I(tag, "Allocated large request buffer (%u bytes)", RPC_LARGE_REQ_SIZE);
    }

    bus->large_req_in_use = true;
    *out_index = 0xFF;  /* 特殊标记：大缓冲区 */
    *out_buf = bus->large_req_buffer;
    xSemaphoreGive(bus->large_req_mutex);

    return SNF_RPC_OK;
}

/**
 * @brief 释放请求缓冲区
 * @param index 缓冲区索引（0xFF=大缓冲区）
 */
static void rpcReqPoolRelease(uint16_t index)
{
    SnfRpcBus *bus = &rpc_bus;

    if (index == 0xFF)
    {
        /* 只解除大缓冲区的占用，保留内存供下一条大请求复用。 */
        if (!bus->large_req_mutex)
        {
            return;
        }
        xSemaphoreTake(bus->large_req_mutex, portMAX_DELAY);
        bus->large_req_in_use = false;
        xSemaphoreGive(bus->large_req_mutex);
    }
    else
    {
        /* 释放小缓冲区 */
        if (index >= RPC_SMALL_POOL_SIZE || !bus->req_pool_mutex || !bus->req_pool_sem)
        {
            return;
        }

        xSemaphoreTake(bus->req_pool_mutex, portMAX_DELAY);
        bus->small_req_pool_in_use[index] = false;
        xSemaphoreGive(bus->req_pool_mutex);

        xSemaphoreGive(bus->req_pool_sem);
    }
}

/**
 * @brief 在统计互斥锁保护下清空统计数据.
 */
static void rpcStatsInit(void)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!bus->stats_mutex)
    {
        return;
    }
    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    memset(&bus->stats, 0, sizeof(bus->stats));
    xSemaphoreGive(bus->stats_mutex);
}

/**
 * @brief 更新正在处理的请求数及其峰值.
 */
static void rpcStatsUpdateInflight(int32_t delta)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!bus->stats_mutex)
    {
        return;
    }
    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    if (delta > 0)
    {
        bus->stats.inflight += (uint32_t)delta;
        if (bus->stats.inflight > bus->stats.inflight_max)
        {
            bus->stats.inflight_max = bus->stats.inflight;
        }
    }
    else if (delta < 0)
    {
        uint32_t d = (uint32_t)(-delta);
        if (bus->stats.inflight >= d)
        {
            bus->stats.inflight -= d;
        }
        else
        {
            bus->stats.inflight = 0;
        }
    }
    xSemaphoreGive(bus->stats_mutex);
}

/**
 * @brief 取出待执行探针并在工作任务内调用.
 */
static void rpcWorkerRunProbe(void)
{
    SnfRpcBus *bus = &rpc_bus;

    SnfRpcWorkerProbeCallback cb = NULL;
    void *ctx = NULL;

    if (bus->worker_probe_mutex == NULL)
    {
        return;
    }

    if (xSemaphoreTake(bus->worker_probe_mutex, 0) != pdPASS)
    {
        return;
    }

    if (bus->worker_probe_pending)
    {
        cb = bus->worker_probe_cb;
        ctx = bus->worker_probe_ctx;
        bus->worker_probe_cb = NULL;
        bus->worker_probe_ctx = NULL;
        bus->worker_probe_pending = false;
    }
    xSemaphoreGive(bus->worker_probe_mutex);

    if (cb != NULL)
    {
        cb(ctx);
    }
}

/**
 * @brief 按 tick 间隔补充令牌并判断是否允许请求.
 */
static bool rpcRateLimitAllow(void)
{
    SnfRpcBus *bus = &rpc_bus;

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed;
    uint64_t added_tokens;
    bool allowed = false;

    if (!bus->rate_limit_enabled)
    {
        return true;
    }

    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    /* 无符号 tick 差值兼容一次计数回绕；乘法先扩展到 64 位，避免补充令牌时溢出。 */
    elapsed = (TickType_t)(now - bus->rate_limit_last_tick);
    added_tokens = ((uint64_t)elapsed * bus->rate_limit_rps) / configTICK_RATE_HZ;
    if (added_tokens > 0)
    {
        if (added_tokens >= bus->rate_limit_burst - bus->rate_limit_tokens)
        {
            bus->rate_limit_tokens = bus->rate_limit_burst;
        }
        else
        {
            bus->rate_limit_tokens += (uint32_t)added_tokens;
        }
        /* 不足一个令牌时保留原计时起点，避免频繁请求让令牌一直无法补充。 */
        bus->rate_limit_last_tick = now;
    }

    if (bus->rate_limit_tokens > 0)
    {
        bus->rate_limit_tokens--;
        allowed = true;
    }
    xSemaphoreGive(bus->stats_mutex);

    return allowed;
}

/**
 * @brief 记录窗口内失败次数并按阈值开启熔断.
 */
static void rpcCircuitBreakerOnFail(void)
{
    SnfRpcBus *bus = &rpc_bus;

    TickType_t now = xTaskGetTickCount();

    if (!bus->cb_enabled)
    {
        return;
    }

    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    if (bus->cb_err_count == 0
            || (TickType_t)(now - bus->cb_window_start_tick) > pdMS_TO_TICKS(bus->cb_window_ms))
    {
        bus->cb_window_start_tick = now;
        bus->cb_err_count = 0;
    }

    bus->cb_err_count++;
    if (bus->cb_err_count >= bus->cb_threshold)
    {
        bus->cb_open = true;
        bus->cb_open_start_tick = now;
    }
    xSemaphoreGive(bus->stats_mutex);
}

/**
 * @brief 检查熔断状态，冷却期结束后恢复接收请求.
 */
static bool rpcCircuitBreakerAllow(void)
{
    SnfRpcBus *bus = &rpc_bus;

    TickType_t now = xTaskGetTickCount();
    bool open;

    if (!bus->cb_enabled)
    {
        return true;
    }

    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    open = bus->cb_open;
    if (open && (TickType_t)(now - bus->cb_open_start_tick) >= pdMS_TO_TICKS(bus->cb_cooldown_ms))
    {
        open = false;
        bus->cb_open = false;
        bus->cb_err_count = 0;
        bus->cb_window_start_tick = now;
    }
    xSemaphoreGive(bus->stats_mutex);

    return !open;
}

/**
 * @brief 为含有效 ID 的请求发送服务繁忙响应.
 */
static void rpcSendServerBusy(SnfRpcChannelNode *channel, const char *request_json)
{
    if (!channel || !request_json)
    {
        return;
    }

    cJSON *req_obj = cJSON_Parse(request_json);
    if (!req_obj)
    {
        return;
    }

    cJSON *id_node = cJSON_GetObjectItem(req_obj, "id");
    if (JSON_IS_NUMBER(id_node))
    {
        cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_SERVER_BUSY, "Server busy");
        cJSON *response = snfRpcCreateResponse(channel, id_node, false, NULL, error);
        if (response)
        {
            snfRpcSendJsonMessage(channel, response);
            cJSON_Delete(response);
        }
        else if (error)
        {
            cJSON_Delete(error);
        }
    }

    cJSON_Delete(req_obj);
}

/**
 * @brief   获取 RPC 统计信息
 *
 * @author  XieWeiMing (weiming.xie@itead.cc)
 * @date    2026-04-20
 */
static cJSON *rpcMethodGetStats(const char *method, cJSON *params, void *user_ctx)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!bus->stats_mutex)
    {
        return NULL;
    }

    cJSON *stats_obj = cJSON_CreateObject();
    if (!stats_obj)
    {
        return NULL;
    }

    /* 复制当前统计数据（加锁读取） */
    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    cJSON_AddNumberToObject(stats_obj, "req_total", (double)bus->stats.req_total);
    cJSON_AddNumberToObject(stats_obj, "req_accepted", (double)bus->stats.req_accepted);
    cJSON_AddNumberToObject(stats_obj, "req_dropped", (double)bus->stats.req_dropped);
    cJSON_AddNumberToObject(stats_obj, "queue_full", (double)bus->stats.queue_full);
    cJSON_AddNumberToObject(stats_obj, "pool_busy", (double)bus->stats.pool_busy);
    cJSON_AddNumberToObject(stats_obj, "invalid_request", (double)bus->stats.invalid_request);
    cJSON_AddNumberToObject(stats_obj, "invalid_params", (double)bus->stats.invalid_params);
    cJSON_AddNumberToObject(stats_obj, "auth_failed", (double)bus->stats.auth_failed);
    cJSON_AddNumberToObject(stats_obj, "method_not_found", (double)bus->stats.method_not_found);
    cJSON_AddNumberToObject(stats_obj, "rate_limited", (double)bus->stats.rate_limited);
    cJSON_AddNumberToObject(stats_obj, "circuit_open", (double)bus->stats.circuit_open);
    cJSON_AddNumberToObject(stats_obj, "slow_request", (double)bus->stats.slow_request);
    cJSON_AddNumberToObject(stats_obj, "inflight", (double)bus->stats.inflight);
    cJSON_AddNumberToObject(stats_obj, "inflight_max", (double)bus->stats.inflight_max);
    xSemaphoreGive(bus->stats_mutex);

    /* 包装为顶层 { "result": <stats_obj> } 以符合 handler 返回约定 */
    cJSON *wrapper = cJSON_CreateObject();
    if (!wrapper)
    {
        cJSON_Delete(stats_obj);
        return NULL;
    }

    cJSON_AddItemToObject(wrapper, "result", stats_obj);

    return wrapper;
}

/**
 * @brief 安全递减通道引用计数，引用归零且标记 pending_free 时真正释放内存
 *
 * 调用者无需额外加锁。函数内部使用 channel_mutex 保护 ref_count 和 pending_free 的检查。
 *
 * @param [in]  channel  需要释放引用的通道节点指针（可为 NULL）
 */
static void rpcChannelReleaseRef(SnfRpcChannelNode *channel)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!channel)
    {
        return;
    }

    bool should_free = false;

    xSemaphoreTake(bus->channel_mutex, portMAX_DELAY);
    if (channel->ref_count > 0)
    {
        channel->ref_count--;
    }
    if (channel->pending_free && channel->ref_count == 0)
    {
        should_free = true;
    }
    xSemaphoreGive(bus->channel_mutex);

    if (should_free)
    {
        free(channel);
    }
}

/**
 * @brief 处理队列请求，释放请求资源，退出时删除自身任务.
 */
static void rpcWorkerTask(void *arg)
{
    SnfRpcBus *bus = &rpc_bus;

    LOG_I(tag, "RPC worker thread started");
    bus->worker_running = true;

    while (!bus->should_stop)
    {
        SnfRpcRequestMsg req_msg;

        /* 等待请求消息 */
        rpcWorkerRunProbe();
        BaseType_t queue_ret = xQueueReceive(bus->request_queue, &req_msg, pdMS_TO_TICKS(RPC_WORKER_QUEUE_TIMEOUT_MS));
        if (queue_ret != pdPASS)
        {
            continue;  /* 等待超时，继续处理 */
        }

        /* 停机时取出的仍可能是普通请求，须归还缓冲区和通道引用；唤醒消息的 channel 为 NULL。 */
        if (bus->should_stop)
        {
            if (req_msg.channel != NULL)
            {
                rpcReqPoolRelease(req_msg.pool_index);
                rpcChannelReleaseRef(req_msg.channel);
            }
            break;
        }

        /*
         * 通道已注销 → 对端不在，消息无意义。
         * 跳过 snfRpcProcessRequest，直接释放资源，加速 channel 回收。
         */
        if (req_msg.channel->pending_free)
        {
            LOG_I(tag,
                  "Discarding msg for unregistered channel '%s'",
                  req_msg.channel->channel_name);
        }
        else
        {
            /* 处理请求 */
            TickType_t start_tick = xTaskGetTickCount();
            rpcStatsUpdateInflight(1);
            snfRpcProcessRequest(req_msg.channel, req_msg.json_data, req_msg.json_len);
            TickType_t elapsed_ticks = (TickType_t)(xTaskGetTickCount() - start_tick);
            if (elapsed_ticks > pdMS_TO_TICKS(RPC_SLOW_REQUEST_MS))
            {
                LOG_W(tag, "RPC request slow: %lu ms",
                      (unsigned long)(((uint64_t)elapsed_ticks * 1000) / configTICK_RATE_HZ));
                snfRpcStatsInc(&bus->stats.slow_request);
            }
            rpcStatsUpdateInflight(-1);
            rpcWorkerRunProbe();
        }

        /* 释放请求缓冲区（支持双层缓冲池，0xFF表示大缓冲区） */
        rpcReqPoolRelease(req_msg.pool_index);

        /*
         * 释放通道引用计数。若通道已在 snfRpcChannelUnregister 中标记 pending_free
         * 且 ref_count 降为 0，rpcChannelReleaseRef 内部将自动 free 通道内存。
         */
        rpcChannelReleaseRef(req_msg.channel);
    }

    LOG_I(tag, "RPC worker thread stopped");
    /* 原子清除句柄和运行标记，避免 deinit 重复删除；发布停止状态后不再访问总线资源。 */
    taskENTER_CRITICAL();
    bus->worker_thread = NULL;
    bus->worker_running = false;
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

int32_t snfRpcCurrentRequestPush(SnfRpcChannelNode *channel)
{
    SnfRpcBus *bus = &rpc_bus;

    TaskHandle_t task = NULL;
    SnfRpcCurrentRequestSlot *slot = NULL;

    if ((channel == NULL) || (bus->current_request_mutex == NULL))
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    task = xTaskGetCurrentTaskHandle();
    if (task == NULL)
    {
        return SNF_RPC_ERR_INTERNAL;
    }

    xSemaphoreTake(bus->current_request_mutex, portMAX_DELAY);
    slot = rpcCurrentRequestFindSlot(task, true);
    if (slot == NULL)
    {
        xSemaphoreGive(bus->current_request_mutex);
        return SNF_RPC_ERR_NO_MEMORY;
    }

    if (!slot->in_use)
    {
        slot->in_use = true;
        slot->depth = 0;
        slot->task = task;
    }

    slot->depth++;
    slot->ctx.transport_type = channel->transport_type;
    slot->ctx.conn_mode = channel->conn_mode;
    slot->ctx.channel_name = channel->channel_name;
    slot->ctx.channel_handle = channel->channel_handle;
    xSemaphoreGive(bus->current_request_mutex);

    return SNF_RPC_OK;
}

void snfRpcCurrentRequestPop(void)
{
    SnfRpcBus *bus = &rpc_bus;

    TaskHandle_t task = NULL;
    SnfRpcCurrentRequestSlot *slot = NULL;

    if (bus->current_request_mutex == NULL)
    {
        return;
    }

    task = xTaskGetCurrentTaskHandle();
    if (task == NULL)
    {
        return;
    }

    xSemaphoreTake(bus->current_request_mutex, portMAX_DELAY);
    slot = rpcCurrentRequestFindSlot(task, false);
    if (slot != NULL)
    {
        if (slot->depth > 0)
        {
            slot->depth--;
        }

        if (slot->depth == 0)
        {
            memset(slot, 0, sizeof(*slot));
        }
    }
    xSemaphoreGive(bus->current_request_mutex);
}

int32_t snfRpcGetCurrentRequestContext(SnfRpcRequestContext *ctx)
{
    SnfRpcBus *bus = &rpc_bus;

    TaskHandle_t task = NULL;
    SnfRpcCurrentRequestSlot *slot = NULL;

    if (ctx == NULL)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (bus->current_request_mutex == NULL)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    task = xTaskGetCurrentTaskHandle();
    if (task == NULL)
    {
        return SNF_RPC_ERR_INTERNAL;
    }

    xSemaphoreTake(bus->current_request_mutex, portMAX_DELAY);
    slot = rpcCurrentRequestFindSlot(task, false);
    if ((slot == NULL) || !slot->in_use)
    {
        xSemaphoreGive(bus->current_request_mutex);
        return SNF_RPC_ERR_NOT_FOUND;
    }

    *ctx = slot->ctx;
    xSemaphoreGive(bus->current_request_mutex);

    return SNF_RPC_OK;
}

void snfRpcStatsInc(uint32_t *counter)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!bus->stats_mutex || !counter)
    {
        return;
    }
    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    (*counter)++;
    xSemaphoreGive(bus->stats_mutex);
}

int32_t snfRpcInit(const char *src_name, uint32_t queue_size)
{
    SnfRpcBus *bus = &rpc_bus;

    LOG_I(tag, "snfRpcInit: src_name=%s, queue_size=%u", src_name, queue_size);

    if (!src_name || queue_size == 0)
    {
        LOG_E(tag, "snfRpcInit: Invalid parameters");
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (queue_size > RPC_SMALL_POOL_SIZE)
    {
        LOG_E(tag, "snfRpcInit: queue_size exceeds small pool size");
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (bus->initialized)
    {
        LOG_W(tag, "snfRpcInit: Already initialized");
        return SNF_RPC_OK;
    }

    /* 初始化总线结构体 */
    strncpy(bus->src_name, src_name, RPC_MAX_SRC_LEN - 1);
    bus->src_name[RPC_MAX_SRC_LEN - 1] = '\0';

    /* 使用 goto cleanup 模式进行资源管理 */
    int32_t ret = SNF_RPC_OK;

    /* 创建同步原语 */
    bus->channel_mutex = xSemaphoreCreateMutex();
    if (!bus->channel_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create channel_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    bus->method_mutex = xSemaphoreCreateMutex();
    if (!bus->method_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create method_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    bus->request_queue = xQueueCreate(queue_size, sizeof(SnfRpcRequestMsg));
    if (!bus->request_queue)
    {
        LOG_E(tag, "snfRpcInit: Failed to create request_queue");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    /* 初始化小请求池 */
    bus->req_pool_mutex = xSemaphoreCreateMutex();
    if (!bus->req_pool_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create req_pool_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    bus->req_pool_sem = xSemaphoreCreateCounting(RPC_SMALL_POOL_SIZE, RPC_SMALL_POOL_SIZE);
    if (!bus->req_pool_sem)
    {
        LOG_E(tag, "snfRpcInit: Failed to create req_pool_sem");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    for (uint16_t i = 0; i < RPC_SMALL_POOL_SIZE; i++)
    {
        bus->small_req_pool_in_use[i] = false;
        bus->small_req_pool[i][0] = '\0';
    }

    /* 初始化大请求缓冲区（按需分配） */
    bus->large_req_mutex = xSemaphoreCreateMutex();
    if (!bus->large_req_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create large_req_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }
    bus->large_req_buffer = NULL;
    bus->large_req_allocated = false;
    bus->large_req_in_use = false;

    bus->stats_mutex = xSemaphoreCreateMutex();
    if (!bus->stats_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create stats_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    rpcStatsInit();
    bus->rate_limit_enabled = false;
    bus->rate_limit_rps = 0;
    bus->rate_limit_burst = 0;
    bus->rate_limit_tokens = 0;
    bus->rate_limit_last_tick = 0;
    bus->cb_enabled = false;
    bus->cb_threshold = 0;
    bus->cb_window_ms = 0;
    bus->cb_cooldown_ms = 0;
    bus->cb_err_count = 0;
    bus->cb_window_start_tick = 0;
    bus->cb_open = false;
    bus->cb_open_start_tick = 0;

    /* 初始化认证上下文 */
    bus->auth_ctx.auth_enabled = false;
    bus->auth_ctx.realm[0] = '\0';
    bus->auth_ctx.username[0] = '\0';
    bus->auth_ctx.password[0] = '\0';
    bus->auth_ctx.last_nonce[0] = '\0';

    bus->auth_ctx.last_nonce_valid = false;
    bus->auth_ctx.last_nc = 0;
    bus->auth_ctx.last_nc_valid = false;
    bus->auth_ctx.auth_mutex = xSemaphoreCreateMutex();
    if (!bus->auth_ctx.auth_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create auth_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    bus->current_request_mutex = xSemaphoreCreateMutex();
    if (!bus->current_request_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create current_request_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    memset(bus->current_request_slots, 0, sizeof(bus->current_request_slots));

    bus->worker_probe_mutex = xSemaphoreCreateMutex();
    if (!bus->worker_probe_mutex)
    {
        LOG_E(tag, "snfRpcInit: Failed to create worker_probe_mutex");
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }
    bus->worker_probe_pending = false;
    bus->worker_probe_cb = NULL;
    bus->worker_probe_ctx = NULL;

    /* 与 OTA、HTTP 一致，栈配置直接使用 StackType_t 元素数，本平台每个元素为 4 字节。 */
    bus->should_stop = false;
    bus->worker_running = true;
    if (xTaskCreate(rpcWorkerTask, SONOFF_RPC_TASK_NAME,
                    SONOFF_RPC_TASK_STACKSIZE, NULL,
                    SONOFF_RPC_TASK_PRIO, &bus->worker_thread) != pdPASS)
    {
        LOG_E(tag, "snfRpcInit: Failed to create worker thread");
        bus->worker_running = false;
        ret = SNF_RPC_ERR_NO_MEMORY;
        goto cleanup;
    }

    bus->initialized = true;
    LOG_I(tag, "RPC bus initialized successfully");

    /* 注册内部管理方法：查询 RPC 运行统计 */
    if (snfRpcMethodRegister("Rpc.GetStats", rpcMethodGetStats, NULL) != SNF_RPC_OK)
    {
        LOG_W(tag, "snfRpcInit: failed to register Rpc.GetStats method");
    }

    return SNF_RPC_OK;

cleanup:
    /* 清理已创建的资源（按照创建的逆序） */
    if (bus->worker_thread)
    {
        bus->should_stop = true;
        vTaskDelete(bus->worker_thread);
        bus->worker_thread = NULL;
        bus->worker_running = false;
    }

    if (bus->worker_probe_mutex)
    {
        vSemaphoreDelete(bus->worker_probe_mutex);
        bus->worker_probe_mutex = NULL;
    }

    if (bus->current_request_mutex)
    {
        vSemaphoreDelete(bus->current_request_mutex);
        bus->current_request_mutex = NULL;
    }

    if (bus->auth_ctx.auth_mutex)
    {
        vSemaphoreDelete(bus->auth_ctx.auth_mutex);
        bus->auth_ctx.auth_mutex = NULL;
    }

    if (bus->stats_mutex)
    {
        vSemaphoreDelete(bus->stats_mutex);
        bus->stats_mutex = NULL;
    }

    if (bus->large_req_mutex)
    {
        if (bus->large_req_allocated && bus->large_req_buffer)
        {
            free(bus->large_req_buffer);
            bus->large_req_buffer = NULL;
        }
        vSemaphoreDelete(bus->large_req_mutex);
        bus->large_req_mutex = NULL;
    }

    if (bus->req_pool_sem)
    {
        vSemaphoreDelete(bus->req_pool_sem);
        bus->req_pool_sem = NULL;
    }

    if (bus->req_pool_mutex)
    {
        vSemaphoreDelete(bus->req_pool_mutex);
        bus->req_pool_mutex = NULL;
    }

    if (bus->request_queue)
    {
        vQueueDelete(bus->request_queue);
        bus->request_queue = NULL;
    }

    if (bus->method_mutex)
    {
        vSemaphoreDelete(bus->method_mutex);
        bus->method_mutex = NULL;
    }

    if (bus->channel_mutex)
    {
        vSemaphoreDelete(bus->channel_mutex);
        bus->channel_mutex = NULL;
    }

    return ret;
}

int32_t snfRpcDeinit(void)
{
    SnfRpcBus *bus = &rpc_bus;

    LOG_I(tag, "snfRpcDeinit");

    if (!bus->initialized)
    {
        return SNF_RPC_OK;
    }

    /* worker 不能等待自身退出，也不能在仍执行 handler 时销毁其依赖的总线资源。 */
    if (xTaskGetCurrentTaskHandle() == bus->worker_thread)
    {
        return SNF_RPC_ERR_INVALID_STATE;
    }

    /* 信号工作线程停止 */
    bus->should_stop = true;

    /* 空消息仅唤醒队列等待；真正的退出条件是 should_stop，队列已满时无需额外唤醒。 */
    if (bus->request_queue)
    {
        SnfRpcRequestMsg stop_msg = {0};
        stop_msg.pool_index = UINT16_MAX;
        (void)xQueueSend(bus->request_queue, &stop_msg, 0);
    }

    /* 等待 worker 自然退出，避免销毁其仍在使用的资源 */
    TickType_t wait_start = xTaskGetTickCount();
    while (bus->worker_running
            && (TickType_t)(xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(RPC_WORKER_STOP_WAIT_MS))
    {
        vTaskDelay(pdMS_TO_TICKS(RPC_WORKER_STOP_POLL_INTERVAL_MS));
    }

    /* 超时后按原有策略终止任务；临界区避免与任务自行退出重复删除。 */
    taskENTER_CRITICAL();
    if (bus->worker_thread != NULL)
    {
        vTaskDelete(bus->worker_thread);
        bus->worker_thread = NULL;
        bus->worker_running = false;
    }
    taskEXIT_CRITICAL();

    /* 队列销毁前释放请求引用，回收已注销但仍被排队请求持有的通道。 */
    SnfRpcRequestMsg pending_msg;
    while (xQueueReceive(bus->request_queue, &pending_msg, 0) == pdPASS)
    {
        if (pending_msg.channel != NULL)
        {
            rpcReqPoolRelease(pending_msg.pool_index);
            rpcChannelReleaseRef(pending_msg.channel);
        }
    }

    /* Clean up channels */
    SnfRpcChannelNode *channel = bus->channel_list;
    while (channel)
    {
        SnfRpcChannelNode *next = channel->next;
        free(channel);
        channel = next;
    }
    bus->channel_list = NULL;

    /* Clean up methods */
    SnfRpcMethodNode *method = bus->method_list;
    while (method)
    {
        SnfRpcMethodNode *next = method->next;
        free(method);
        method = next;
    }
    bus->method_list = NULL;

    /* Delete synchronization objects */
    if (bus->channel_mutex)
    {
        vSemaphoreDelete(bus->channel_mutex);
        bus->channel_mutex = NULL;
    }

    if (bus->method_mutex)
    {
        vSemaphoreDelete(bus->method_mutex);
        bus->method_mutex = NULL;
    }

    if (bus->request_queue)
    {
        vQueueDelete(bus->request_queue);
        bus->request_queue = NULL;
    }

    if (bus->req_pool_sem)
    {
        vSemaphoreDelete(bus->req_pool_sem);
        bus->req_pool_sem = NULL;
    }

    if (bus->req_pool_mutex)
    {
        vSemaphoreDelete(bus->req_pool_mutex);
        bus->req_pool_mutex = NULL;
    }

    /* 释放大请求缓冲区 */
    if (bus->large_req_mutex)
    {
        if (bus->large_req_allocated && bus->large_req_buffer)
        {
            free(bus->large_req_buffer);
            bus->large_req_buffer = NULL;
            LOG_I(tag, "Freed large request buffer");
        }
        vSemaphoreDelete(bus->large_req_mutex);
        bus->large_req_mutex = NULL;
    }

    if (bus->auth_ctx.auth_mutex)
    {
        vSemaphoreDelete(bus->auth_ctx.auth_mutex);
        bus->auth_ctx.auth_mutex = NULL;
    }

    if (bus->worker_probe_mutex)
    {
        vSemaphoreDelete(bus->worker_probe_mutex);
        bus->worker_probe_mutex = NULL;
        bus->worker_probe_pending = false;
        bus->worker_probe_cb = NULL;
        bus->worker_probe_ctx = NULL;
    }

    if (bus->current_request_mutex)
    {
        vSemaphoreDelete(bus->current_request_mutex);
        bus->current_request_mutex = NULL;
    }

    if (bus->stats_mutex)
    {
        vSemaphoreDelete(bus->stats_mutex);
        bus->stats_mutex = NULL;
    }

    bus->initialized = false;
    LOG_I(tag, "RPC bus deinitialized");

    return SNF_RPC_OK;
}

const char *snfRpcGetSrcName(void)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!bus->initialized)
    {
        return NULL;
    }

    return bus->src_name;
}

int32_t snfRpcWorkerProbeRegister(SnfRpcWorkerProbeCallback cb, void *ctx)
{
    SnfRpcBus *bus = &rpc_bus;

    int32_t ret = SNF_RPC_OK;

    if (cb == NULL)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }
    if (!bus->initialized || (bus->worker_probe_mutex == NULL))
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    if (xSemaphoreTake(bus->worker_probe_mutex, 0) != pdPASS)
    {
        return SNF_RPC_ERR_BUSY;
    }

    if (bus->worker_probe_pending)
    {
        ret = SNF_RPC_ERR_BUSY;
    }
    else
    {
        bus->worker_probe_cb = cb;
        bus->worker_probe_ctx = ctx;
        bus->worker_probe_pending = true;
    }
    xSemaphoreGive(bus->worker_probe_mutex);

    return ret;
}

int32_t snfRpcChannelRegister(const SnfRpcChannelConfig *config, SnfRpcChannelHandle *out_channel_handle)
{
    SnfRpcBus *bus = &rpc_bus;

    LOG_I(tag, "snfRpcChannelRegister: channel_name=%s", config ? config->channel_name : "NULL");

    if (!config || !out_channel_handle)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    /* 分配通道节点 */
    SnfRpcChannelNode *channel = malloc(sizeof(*channel));
    if (!channel)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    /* 初始化通道 */
    memset(channel, 0, sizeof(*channel));
    strncpy(channel->channel_name, config->channel_name, RPC_MAX_CHANNEL_NAME_LEN - 1);
    channel->channel_name[RPC_MAX_CHANNEL_NAME_LEN - 1] = '\0';

    channel->transport_type = config->transport_type;
    channel->conn_mode = config->conn_mode;
    channel->channel_handle = config->channel_handle;
    channel->send_cb = (SnfRpcChannelSend)config->send_cb;
    channel->auth_supported = config->auth_supported;

    /* 添加到通道列表 */
    xSemaphoreTake(bus->channel_mutex, portMAX_DELAY);
    channel->next = bus->channel_list;
    bus->channel_list = channel;
    xSemaphoreGive(bus->channel_mutex);

    *out_channel_handle = (SnfRpcChannelHandle)channel;
    LOG_I(tag, "Channel %s registered", config->channel_name);

    return SNF_RPC_OK;
}

int32_t snfRpcChannelUnregister(SnfRpcChannelHandle channel_handle)
{
    SnfRpcBus *bus = &rpc_bus;

    LOG_I(tag, "snfRpcChannelUnregister");

    if (!channel_handle)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->channel_mutex, portMAX_DELAY);

    SnfRpcChannelNode *channel = bus->channel_list;
    SnfRpcChannelNode *prev = NULL;

    while (channel)
    {
        if ((SnfRpcChannelHandle)channel == channel_handle)
        {
            if (prev)
            {
                prev->next = channel->next;
            }
            else
            {
                bus->channel_list = channel->next;
            }

            xSemaphoreGive(bus->channel_mutex);

            /*
             * 延迟释放：标记 pending_free，只有当队列中没有待处理的请求引用此通道
             * （ref_count == 0）时才真正释放内存。避免了 rpcWorkerTask 取出的
             * 请求消息中的 channel 指针成为悬垂指针。
             */
            channel->pending_free = true;
            if (channel->ref_count == 0)
            {
                free(channel);
            }
            else
            {
                LOG_I(tag,
                      "Channel '%s' pending free (ref_count=%u), deferred until workers release",
                      channel->channel_name, channel->ref_count);
            }

            LOG_I(tag, "Channel unregistered");
            return SNF_RPC_OK;
        }

        prev = channel;
        channel = channel->next;
    }

    xSemaphoreGive(bus->channel_mutex);

    return SNF_RPC_ERR_NOT_FOUND;
}

int32_t snfRpcChannelSetRemoteSrc(SnfRpcChannelHandle channel_handle, const char *remote_src)
{
    LOG_I(tag, "snfRpcChannelSetRemoteSrc: remote_src=%s", remote_src ? remote_src : "NULL");

    if (!channel_handle || !remote_src)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (!rpc_bus.initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    SnfRpcChannelNode *channel = snfRpcChannelFind(channel_handle);
    if (!channel)
    {
        return SNF_RPC_ERR_NOT_FOUND;
    }

    strncpy(channel->remote_src, remote_src, RPC_MAX_SRC_LEN - 1);
    channel->remote_src[RPC_MAX_SRC_LEN - 1] = '\0';
    channel->remote_src_len = strlen(channel->remote_src);

    return SNF_RPC_OK;
}

int32_t snfRpcChannelSetNotifyRemoteSrc(SnfRpcChannelHandle channel_handle, const char *notify_remote_src)
{
    LOG_I(tag, "snfRpcChannelSetNotifyRemoteSrc: notify_remote_src=%s", notify_remote_src ? notify_remote_src : "NULL");

    if (!channel_handle || !notify_remote_src)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (!rpc_bus.initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    SnfRpcChannelNode *channel = snfRpcChannelFind(channel_handle);
    if (!channel)
    {
        return SNF_RPC_ERR_NOT_FOUND;
    }

    strncpy(channel->notify_remote_src, notify_remote_src, RPC_MAX_SRC_LEN - 1);
    channel->notify_remote_src[RPC_MAX_SRC_LEN - 1] = '\0';
    channel->notify_remote_src_len = strlen(channel->notify_remote_src);

    return SNF_RPC_OK;
}

int32_t snfRpcChannelHandleRequest(SnfRpcChannelHandle channel_handle, const char *request_json)
{
    SnfRpcBus *bus = &rpc_bus;

    LOG_I(tag, "snfRpcChannelHandleRequest");

    if (!channel_handle || !request_json)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    /*
     * 原子查找 + 引用计数递增，防止 channel 在入队后被另一线程 free。
     * 必须在 channel_mutex 保护下完成 find + ref_count++ 原子操作。
     */
    xSemaphoreTake(bus->channel_mutex, portMAX_DELAY);
    SnfRpcChannelNode *channel = snfRpcChannelFindNoLock(channel_handle);
    if (!channel)
    {
        xSemaphoreGive(bus->channel_mutex);
        return SNF_RPC_ERR_NOT_FOUND;
    }
    channel->ref_count++;
    xSemaphoreGive(bus->channel_mutex);

    snfRpcStatsInc(&bus->stats.req_total);

    if (!rpcCircuitBreakerAllow())
    {
        snfRpcStatsInc(&bus->stats.circuit_open);
        snfRpcStatsInc(&bus->stats.req_dropped);
        rpcSendServerBusy(channel, request_json);
        rpcChannelReleaseRef(channel);
        return SNF_RPC_ERR_SERVER_BUSY;
    }

    if (!rpcRateLimitAllow())
    {
        snfRpcStatsInc(&bus->stats.rate_limited);
        snfRpcStatsInc(&bus->stats.req_dropped);
        rpcSendServerBusy(channel, request_json);
        rpcChannelReleaseRef(channel);
        return SNF_RPC_ERR_SERVER_BUSY;
    }

    /* 分配请求消息 */
    uint32_t json_len = strlen(request_json);
    if (json_len == 0 || json_len > RPC_LARGE_REQ_SIZE)
    {
        LOG_W(tag, "snfRpcChannelHandleRequest: Invalid JSON length=%"PRIu32, json_len);
        rpcChannelReleaseRef(channel);
        return SNF_RPC_ERR_INVALID_ARG;
    }

    LOG_I(tag, "[RPC_RECV][%s] %s",
          channel->channel_name,
          request_json);

    SnfRpcRequestMsg req_msg;

    req_msg.channel = channel;
    req_msg.json_len = json_len;

    char *pool_buf = NULL;
    uint16_t pool_index = 0;
    int pool_ret = rpcReqPoolAcquire(json_len, &pool_index, &pool_buf);
    if (pool_ret != SNF_RPC_OK)
    {
        snfRpcStatsInc(&bus->stats.pool_busy);
        snfRpcStatsInc(&bus->stats.req_dropped);
        rpcCircuitBreakerOnFail();
        rpcSendServerBusy(channel, request_json);
        rpcChannelReleaseRef(channel);
        return pool_ret;
    }

    /* 先复制报文，调用方返回后即可复用原接收缓冲区；FreeRTOS 队列只复制消息结构体。 */
    memcpy(pool_buf, request_json, json_len);
    pool_buf[json_len] = '\0';
    req_msg.json_data = pool_buf;
    req_msg.pool_index = pool_index;

    /* 入队成功后由 worker 归还缓冲区和通道引用；入队失败则由当前路径成对回收。 */
    if (xQueueSend(bus->request_queue, &req_msg, 0) != pdPASS)
    {
        rpcReqPoolRelease(req_msg.pool_index);
        LOG_E(tag, "snfRpcChannelHandleRequest: Queue full");

        snfRpcStatsInc(&bus->stats.queue_full);
        snfRpcStatsInc(&bus->stats.req_dropped);
        rpcCircuitBreakerOnFail();
        rpcSendServerBusy(channel, request_json);
        rpcChannelReleaseRef(channel);
        return SNF_RPC_ERR_QUEUE_FULL;
    }

    snfRpcStatsInc(&bus->stats.req_accepted);

    return SNF_RPC_OK;
}

SnfRpcBus *snfRpcBusGetInstance(void)
{
    return &rpc_bus;
}

SnfRpcChannelNode *snfRpcChannelFindNoLock(SnfRpcChannelHandle handle)
{
    if (!handle)
    {
        return NULL;
    }

    SnfRpcChannelNode *channel = rpc_bus.channel_list;
    while (channel)
    {
        if ((SnfRpcChannelHandle)channel == handle)
        {
            return channel;
        }
        channel = channel->next;
    }

    return NULL;
}

SnfRpcChannelNode *snfRpcChannelFind(SnfRpcChannelHandle handle)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!handle)
    {
        return NULL;
    }

    xSemaphoreTake(bus->channel_mutex, portMAX_DELAY);
    SnfRpcChannelNode *channel = snfRpcChannelFindNoLock(handle);
    xSemaphoreGive(bus->channel_mutex);

    return channel;
}

SnfRpcMethodNode *snfRpcMethodFindNoLock(const char *method_name)
{
    if (!method_name)
    {
        return NULL;
    }

    SnfRpcMethodNode *method = rpc_bus.method_list;
    while (method)
    {
        if (strcmp(method->method_name, method_name) == 0)
        {
            return method;
        }
        method = method->next;
    }

    return NULL;
}

SnfRpcMethodNode *snfRpcMethodFind(const char *method_name)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!method_name)
    {
        return NULL;
    }

    xSemaphoreTake(bus->method_mutex, portMAX_DELAY);
    SnfRpcMethodNode *method = snfRpcMethodFindNoLock(method_name);
    xSemaphoreGive(bus->method_mutex);

    return method;
}

int32_t snfRpcMethodSetFlags(const char *method_name, uint32_t flags, bool enable)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!method_name)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    xSemaphoreTake(bus->method_mutex, portMAX_DELAY);
    SnfRpcMethodNode *method = snfRpcMethodFindNoLock(method_name);
    if (!method)
    {
        xSemaphoreGive(bus->method_mutex);
        return SNF_RPC_ERR_METHOD_NOT_FOUND;
    }

    if (enable)
    {
        method->flags |= flags;
    }
    else
    {
        method->flags &= ~flags;
    }

    xSemaphoreGive(bus->method_mutex);

    return SNF_RPC_OK;
}

int32_t snfRpcMethodSetExemptAuth(const char *method, bool enable)
{
    return snfRpcMethodSetFlags(method, RPC_METHOD_FLAG_EXEMPT_AUTH, enable);
}

int32_t snfRpcMethodSetInternal(const char *method, bool enable)
{
    return snfRpcMethodSetFlags(method, RPC_METHOD_FLAG_INTERNAL, enable);
}

int32_t snfRpcChannelSendMessage(SnfRpcChannelNode *channel, const char *json_str, uint32_t json_len)
{
    if (!channel || !channel->send_cb || !json_str)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    LOG_I(tag, "[RPC_SEND][%s] %.*s",
          channel->channel_name,
          (int)json_len,
          json_str);

    return channel->send_cb(channel->channel_handle, json_str, json_len);
}

int32_t snfRpcSendJsonMessage(SnfRpcChannelNode *channel, const cJSON *message)
{
    if (!channel || !message)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    /* SDK 的打印接口未声明 const，序列化过程只读取节点。 */
    char *json_str = cJSON_PrintUnformatted((cJSON *)message);
    if (!json_str)
    {
        return SNF_RPC_ERR_NO_MEMORY;
    }

    /* 长度限制在序列化完成后检查，只约束发送报文，不能限制打印过程的内存峰值。 */
    size_t len = strlen(json_str);
    if (len > RPC_RSP_MAX_JSON_LEN)
    {
        LOG_E(tag, "Response JSON too large: %zu bytes", len);
        free(json_str);
        return SNF_RPC_ERR_INVALID_SIZE;
    }

    /* 回调只借用字符串；异步发送须在返回前复制，RPC 无论发送成败都会释放此缓冲区。 */
    int32_t ret = snfRpcChannelSendMessage(channel, json_str, (uint32_t)len);
    free(json_str);

    return ret;
}

int32_t snfRpcSetRateLimit(uint32_t rps, uint32_t burst)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    if (rps == 0 || burst == 0)
    {
        bus->rate_limit_enabled = false;
        bus->rate_limit_rps = 0;
        bus->rate_limit_burst = 0;
        bus->rate_limit_tokens = 0;
        bus->rate_limit_last_tick = 0;
    }
    else
    {
        bus->rate_limit_enabled = true;
        bus->rate_limit_rps = rps;
        bus->rate_limit_burst = burst;
        bus->rate_limit_tokens = burst;
        bus->rate_limit_last_tick = xTaskGetTickCount();
    }
    xSemaphoreGive(bus->stats_mutex);

    return SNF_RPC_OK;
}

int32_t snfRpcSetCircuitBreaker(bool enabled, uint32_t threshold, uint32_t window_ms, uint32_t cooldown_ms)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    bus->cb_enabled = enabled;
    bus->cb_threshold = threshold;
    bus->cb_window_ms = window_ms;
    bus->cb_cooldown_ms = cooldown_ms;
    bus->cb_err_count = 0;
    bus->cb_window_start_tick = 0;
    bus->cb_open = false;
    bus->cb_open_start_tick = 0;
    xSemaphoreGive(bus->stats_mutex);

    return SNF_RPC_OK;
}

int32_t snfRpcGetStats(SnfRpcStats *out_stats)
{
    SnfRpcBus *bus = &rpc_bus;

    if (!out_stats)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->stats_mutex, portMAX_DELAY);
    *out_stats = bus->stats;
    xSemaphoreGive(bus->stats_mutex);

    return SNF_RPC_OK;
}

int32_t snfRpcResetStats(void)
{
    if (!rpc_bus.initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    rpcStatsInit();

    return SNF_RPC_OK;
}

cJSON *snfRpcCreateErrorObject(int32_t code, const char *message)
{
    cJSON *error = cJSON_CreateObject();
    if (!error)
    {
        return NULL;
    }

    cJSON_AddNumberToObject(error, "code", (double)code);
    if (message)
    {
        cJSON_AddStringToObject(error, "message", message);
    }

    return error;
}

cJSON *snfRpcCreateResponse(SnfRpcChannelNode *channel, cJSON *id_node, bool force_null_id, cJSON *result, cJSON *error)
{
    /* 成功结果和错误互斥；在最后挂入节点之前，result/error 始终由调用方持有。 */
    if ((result == NULL) == (error == NULL))
    {
        return NULL;
    }

    cJSON *response = cJSON_CreateObject();
    if (response == NULL)
    {
        return NULL;
    }

    /* 不再在响应中包含 jsonrpc 字段（兼容省略场景） */

    /* 添加 src (总线名称) */
    cJSON_AddStringToObject(response, "src", rpc_bus.src_name);

    /* 添加 dst (请求方 src) */
    if (channel && channel->remote_src_len > 0)
    {
        cJSON_AddStringToObject(response, "dst", channel->remote_src);
    }
    else
    {
        /* dst 必须存在，缺失时置为空字符串 */
        cJSON_AddStringToObject(response, "dst", "");
    }

    /* 复制 ID 的值，避免响应引用请求树中的节点；请求树和响应树可以独立释放。 */
    if (id_node)
    {
        if (JSON_IS_NUMBER(id_node))
        {
            cJSON_AddNumberToObject(response, "id", id_node->valuedouble);
        }
        else if (JSON_IS_NULL(id_node))
        {
            cJSON_AddNullToObject(response, "id");
        }
    }
    else if (force_null_id)
    {
        cJSON_AddNullToObject(response, "id");
    }

    /* 最后挂入独立节点；常量键无需分配内存，接管后直接返回成功。 */
    if (result != NULL)
    {
        cJSON_AddItemToObjectCS(response, "result", result);
    }
    else
    {
        cJSON_AddItemToObjectCS(response, "error", error);
    }

    return response;
}
