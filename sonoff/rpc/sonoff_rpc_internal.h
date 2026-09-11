/**
 * @file    sonoff_rpc_internal.h
 * @brief   RPC 模块内部结构体和接口
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_RPC_INTERNAL_H__
#define __SONOFF_RPC_INTERNAL_H__

#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

#include "sonoff_rpc.h"

#define RPC_MAX_CHANNEL_NAME_LEN    32    /**< 通道名称最大长度 */
#define RPC_MAX_METHOD_NAME_LEN     64    /**< 方法名称最大长度 */
#define RPC_MAX_SRC_LEN             64    /**< src/dst 字段最大长度 */
#define RPC_MAX_USERNAME_LEN        32    /**< 用户名最大长度 */
#define RPC_MAX_PASSWORD_LEN        64    /**< 密码最大长度 */
#define RPC_MAX_AUTH_NONCE_LEN      32    /**< 认证 nonce 最大长度 */
#define RPC_MAX_REALM_LEN           64    /**< 认证 realm 最大长度 */
#define RPC_CURRENT_REQUEST_SLOT_COUNT 4  /**< 当前请求上下文槽位数量 */

/* 请求双层缓冲池配置 */
#define RPC_SMALL_REQ_SIZE          512  /**< 小请求缓冲区大小（常驻内存） */
#define RPC_SMALL_POOL_SIZE         3    /**< 小缓冲池数量 */

#define RPC_LARGE_REQ_SIZE          4096  /**< 大请求缓冲区大小（按需分配） */

#define RPC_RSP_MAX_JSON_LEN        2048  /**< 响应 JSON 最大长度（不含终止符） */

/* 方法标志位 */
#define RPC_METHOD_FLAG_EXEMPT_AUTH   (1 << 0)  /**< 方法豁免认证 */
#define RPC_METHOD_FLAG_INTERNAL      (1 << 1)  /**< 方法仅限本地/内部调用 */

/**
 * @brief RPC 内部通道结构体
 */
typedef struct SnfRpcChannelNode
{
    struct SnfRpcChannelNode *next;  /**< 指向下一个通道 */

    char channel_name[RPC_MAX_CHANNEL_NAME_LEN];  /**< 通道标识符 */
    SnfRpcTransportType transport_type;           /**< 传输协议类型 */
    SnfRpcConnectionMode conn_mode;               /**< 连接模式 */

    void *channel_handle;       /**< 不透明的通道上下文 */
    SnfRpcChannelSend send_cb;  /**< 发送消息回调 */

    bool auth_supported;                      /**< 此通道是否支持认证 */
    char remote_src[RPC_MAX_SRC_LEN];         /**< 缓存的远程客户端源标识符 */
    char notify_remote_src[RPC_MAX_SRC_LEN];  /**< 通知默认目标源标识符 */
    uint16_t ref_count;                       /**< 正在使用该通道的引用计数 */
    bool pending_free;                        /**< 已注销且等待引用归零后释放 */
    uint32_t remote_src_len;                  /**< 缓存的 remote_src 长度 */
    uint32_t notify_remote_src_len;           /**< 缓存的 notify_remote_src 长度 */
    bool allow_internal_methods;              /**< 此通道是否允许调用 INTERNAL 方法，与传输类型无关。 */
} SnfRpcChannelNode;

/**
 * @brief RPC 内部方法结构体
 */
typedef struct SnfRpcMethodNode
{
    struct SnfRpcMethodNode *next;  /**< 指向下一个方法 */

    char method_name[RPC_MAX_METHOD_NAME_LEN];  /**< 方法名称 */
    SnfRpcMethodHandler handler;                /**< 处理器回调函数 */
    void *user_ctx;                             /**< 用户定义的上下文 */
    uint32_t flags;                             /**< 方法属性标志（见 RPC_METHOD_FLAG_*） */
} SnfRpcMethodNode;

/**
 * @brief 内部请求消息结构体
 */
typedef struct
{
    SnfRpcChannelNode *channel;  /**< 指向源通道的指针 */
    char *json_data;             /**< JSON 请求字符串 */
    uint32_t json_len;           /**< JSON 长度 */
    uint16_t pool_index;         /**< 请求缓冲池索引 */
} SnfRpcRequestMsg;

typedef struct
{
    bool in_use;
    uint16_t depth;
    TaskHandle_t task;
    SnfRpcRequestContext ctx;
} SnfRpcCurrentRequestSlot;

/**
 * @brief 内部认证上下文
 */
typedef struct
{
    bool auth_enabled;                          /**< 全局认证开关 */
    char realm[RPC_MAX_REALM_LEN];              /**< 认证 realm */
    char username[RPC_MAX_USERNAME_LEN];        /**< 用户名 */
    char password[RPC_MAX_PASSWORD_LEN];        /**< 原始密码（用于计算HA1） */
    bool password_valid;                        /**< 是否持有可用于重新计算HA1的原始密码 */
    char ha1[SNF_RPC_AUTH_SHA256_HEX_LEN + 1];  /**< 预计算或外部提供的HA1 */
    bool ha1_valid;                             /**< 是否已配置可用的HA1 */

    /* 最近一次通过的 nonce（简单防重放） */
    char last_nonce[RPC_MAX_AUTH_NONCE_LEN + 1];

    bool last_nonce_valid;
    uint32_t last_nc;  /**< 最近一次通过的 nc（与 last_nonce 绑定） */
    bool last_nc_valid;

    SemaphoreHandle_t auth_mutex;  /**< 认证操作互斥锁 */
} SnfRpcAuthCtx;

/**
 * @brief 全局 RPC 总线结构体（单实例）
 */
typedef struct
{
    bool initialized;  /**< 总线是否已初始化 */

    char src_name[RPC_MAX_SRC_LEN];  /**< RPC 总线源名称 */

    /* 通道管理 */
    SnfRpcChannelNode *channel_list;  /**< 通道链表 */
    SemaphoreHandle_t channel_mutex;  /**< 通道操作互斥锁 */

    /* 方法管理 */
    SnfRpcMethodNode *method_list;   /**< 已注册方法链表 */
    SemaphoreHandle_t method_mutex;  /**< 方法操作互斥锁 */

    /* 请求队列 */
    QueueHandle_t request_queue;  /**< 传入请求队列 */

    /* 请求双层缓冲池设计 - 小池常驻，大池按需 */

    /* 小请求缓冲池（常驻内存，3×512B = 1.5KB） */
    SemaphoreHandle_t req_pool_mutex;
    SemaphoreHandle_t req_pool_sem;
    char small_req_pool[RPC_SMALL_POOL_SIZE][RPC_SMALL_REQ_SIZE + 1];
    bool small_req_pool_in_use[RPC_SMALL_POOL_SIZE];

    /* 大请求缓冲区（按需分配，4KB） */
    char *large_req_buffer;
    bool large_req_allocated;
    bool large_req_in_use;
    SemaphoreHandle_t large_req_mutex;

    /* 工作线程 */
    TaskHandle_t worker_thread;                 /**< 工作线程句柄 */
    volatile bool should_stop;                  /**< 停止工作线程的信号 */
    volatile bool worker_running;               /**< 工作线程是否仍在运行 */
    SemaphoreHandle_t worker_probe_mutex;       /**< worker 探针槽位互斥锁 */
    bool worker_probe_pending;                  /**< worker 探针是否等待执行 */
    SnfRpcWorkerProbeCallback worker_probe_cb;  /**< worker 探针回调 */
    void *worker_probe_ctx;                     /**< worker 探针上下文 */

    /* 认证 */
    SnfRpcAuthCtx auth_ctx;  /**< 认证上下文 */

    /* 统计 */
    SemaphoreHandle_t stats_mutex;
    SnfRpcStats stats;

    /* 限流（令牌桶） */
    bool rate_limit_enabled;
    uint32_t rate_limit_rps;
    uint32_t rate_limit_burst;
    uint32_t rate_limit_tokens;
    TickType_t rate_limit_last_tick;

    /* 熔断器 */
    bool cb_enabled;
    uint32_t cb_threshold;
    uint32_t cb_window_ms;
    uint32_t cb_cooldown_ms;
    uint32_t cb_err_count;
    TickType_t cb_window_start_tick;
    TickType_t cb_open_start_tick;
    bool cb_open;

    /* 当前请求上下文 */
    SemaphoreHandle_t current_request_mutex;
    SnfRpcCurrentRequestSlot current_request_slots[RPC_CURRENT_REQUEST_SLOT_COUNT];
} SnfRpcBus;

/**
 * @brief   获取全局 RPC 总线实例
 * @return  指向全局 RPC 总线结构体的指针
 */
SnfRpcBus *snfRpcBusGetInstance(void);

/**
 * @brief   按句柄查找通道（加锁版本）
 * @param [in]  handle        通道句柄
 * @return  通道节点指针，未找到时返回 NULL
 */
SnfRpcChannelNode *snfRpcChannelFind(SnfRpcChannelHandle handle);

/**
 * @brief   按句柄查找通道（不加锁，内部使用）
 * @warning 调用者必须持有 channel_mutex
 * @param [in]  handle        通道句柄
 * @return  通道节点指针，未找到时返回 NULL
 */
SnfRpcChannelNode *snfRpcChannelFindNoLock(SnfRpcChannelHandle handle);

/**
 * @brief   按名称查找方法（加锁版本）
 * @param [in]  method_name   要查找的方法名称
 * @return  方法节点指针，未找到时返回 NULL
 */
SnfRpcMethodNode *snfRpcMethodFind(const char *method_name);

/**
 * @brief   按名称查找方法（不加锁，内部使用）
 * @warning 调用者必须持有 method_mutex
 * @param [in]  method_name   要查找的方法名称
 * @return  方法节点指针，未找到时返回 NULL
 */
SnfRpcMethodNode *snfRpcMethodFindNoLock(const char *method_name);

/**
 * @brief   处理传入的 RPC 请求
 * @param [in]  channel    指向源通道的指针
 * @param [in]  json_str   请求 JSON 字符串
 * @param [in]  json_len   请求 JSON 长度
 */
void snfRpcProcessRequest(SnfRpcChannelNode *channel, const char *json_str, uint32_t json_len);

/**
 * @brief   验证和认证请求
 * @param [in]  request_obj   JSON 请求对象
 * @param [in]  channel       指向通道的指针
 * @return  验证并认证成功返回 0，失败返回负数错误码
 */
int32_t snfRpcValidateAndAuth(cJSON *request_obj, SnfRpcChannelNode *channel);

/**
 * @brief   检查方法是否豁免认证
 * @param [in]  method_name   方法名称
 * @return  豁免返回 true，否则返回 false
 */
bool snfRpcIsMethodExempt(const char *method_name);

/**
 * @brief   通过通道发送 JSON 消息
 * @param [in]  channel    指向通道的指针
 * @param [in]  json_str   消息 JSON 字符串
 * @param [in]  json_len   消息 JSON 长度
 * @return  成功返回 0，失败返回负数错误码
 */
int32_t snfRpcChannelSendMessage(SnfRpcChannelNode *channel, const char *json_str, uint32_t json_len);
int32_t snfRpcSendJsonMessage(SnfRpcChannelNode *channel, const cJSON *message);
void snfRpcStatsInc(uint32_t *counter);
int32_t snfRpcCurrentRequestPush(SnfRpcChannelNode *channel);
void snfRpcCurrentRequestPop(void);

/**
 * @brief   创建 JSON-RPC 响应对象
 * @param [in]  channel        请求通道，可为 NULL.
 * @param [in]  id_node        请求 ID 节点，可为 NULL；仅借用，所有权不转移.
 * @param [in]  force_null_id  无 ID 节点时是否添加 null ID.
 * @param [in]  result         独立的结果节点，与 error 恰好一个非 NULL.
 * @param [in]  error          独立的错误节点，与 result 恰好一个非 NULL.
 * @return  新分配的 JSON 响应对象，失败时返回 NULL.
 * @note    成功时返回对象接管 result/error，调用方只需在使用完毕后调用 cJSON_Delete() 释放响应.
 * @note    返回 NULL 时 result/error 仍由调用方负责释放，函数内部只清理自己创建的对象.
 * @note    result/error 必须是未挂入其他 JSON 树的独立节点，不得传入借用节点.
 */
cJSON *snfRpcCreateResponse(SnfRpcChannelNode *channel, cJSON *id_node, bool force_null_id, cJSON *result,
                            cJSON *error);

/**
 * @brief   创建 JSON-RPC 错误对象
 * @param [in]  code        错误码
 * @param [in]  message     错误消息字符串
 * @return  新分配的 JSON 错误对象，错误时返回 NULL。调用者使用完毕后需调用 cJSON_Delete()。
 */
cJSON *snfRpcCreateErrorObject(int32_t code, const char *message);

#endif /* __SONOFF_RPC_INTERNAL_H__ */
