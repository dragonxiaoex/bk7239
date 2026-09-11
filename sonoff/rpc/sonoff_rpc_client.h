/**
 * @file    sonoff_rpc_client.h
 * @brief   RPC 本地客户端接口
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_RPC_CLIENT_H__
#define __SONOFF_RPC_CLIENT_H__

#include <stdint.h>
#include <stdbool.h>

#include "sonoff_rpc.h"

#ifndef CONFIG_SNF_RPC_CLIENT_DEFAULT_TIMEOUT_MS
#define CONFIG_SNF_RPC_CLIENT_DEFAULT_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_SNF_RPC_CLIENT_MAX_PENDING
#define CONFIG_SNF_RPC_CLIENT_MAX_PENDING 8
#endif

typedef void *SnfRpcClientHandle;

typedef enum
{
    SNF_RPC_CLIENT_DISCONNECTED = 0,
    SNF_RPC_CLIENT_CONNECTED = 1,
} SnfRpcClientState;

typedef struct
{
    const char *client_name;        /**< 客户端 src，非空且小于 64 字节. */
    const char *server_name;        /**< 服务端 dst，非空且小于 64 字节. */
    const char *notify_remote_src;  /**< 通知目标，NULL 或空字符串时使用 client_name. */
    uint32_t default_timeout_ms;    /**< 默认超时毫秒数，0 时使用配置默认值. */
    uint16_t max_pending;           /**< 等待响应的请求槽位数，0 时使用配置默认值. */
} SnfRpcClientConfig;

/**
 * @brief 接收 RPC 通知.
 * @param [in] method - 借用的方法名，仅在回调期间有效.
 * @param [in] params - 参数副本，可能为 NULL；所有权交给回调，由回调负责释放.
 * @param [in] user_ctx - 注册时指定的用户上下文.
 */
typedef void (*SnfRpcClientNotifyHandler)(const char *method,
                                          cJSON *params,
                                          void *user_ctx);

/**
 * @brief 接收异步调用响应或超时、关闭事件.
 * @param [in] status - 本地处理状态；业务错误需另读 error 节点.
 * @param [in] request_id - 请求 ID.
 * @param [in] result - 结果副本，可能为 NULL；回调接管并负责释放.
 * @param [in] error - 错误副本，可能为 NULL；回调接管并负责释放.
 * @param [in] user_ctx - 调用时指定的用户上下文.
 */
typedef void (*SnfRpcClientResponseHandler)(int32_t status,
                                            uint32_t request_id,
                                            cJSON *result,
                                            cJSON *error,
                                            void *user_ctx);

/**
 * @brief 创建本地客户端，尚未连接 RPC 总线.
 * @param [in] config - 配置；字符串在创建期间复制.
 * @param [out] out_client - 成功时返回的客户端句柄.
 * @return SNF_RPC_OK 成功，其他为 RPC 错误码.
 */
int32_t snfRpcClientCreate(const SnfRpcClientConfig *config, SnfRpcClientHandle *out_client);

/**
 * @brief 关闭并销毁客户端；调用前需保证其他任务不再使用该句柄.
 * @param [in] client_handle - 客户端句柄.
 * @return SNF_RPC_OK 成功，其他为 RPC 错误码.
 */
int32_t snfRpcClientDestroy(SnfRpcClientHandle client_handle);

/**
 * @brief 向已初始化的总线注册本地持久通道.
 * @param [in] client_handle - 客户端句柄.
 * @return SNF_RPC_OK 成功，其他为 RPC 错误码.
 */
int32_t snfRpcClientConnect(SnfRpcClientHandle client_handle);

/**
 * @brief 注销本地通道，并通知等待中的调用客户端已关闭.
 * @param [in] client_handle - 客户端句柄.
 * @return SNF_RPC_OK 成功，其他为 RPC 错误码.
 */
int32_t snfRpcClientClose(SnfRpcClientHandle client_handle);

/**
 * @brief 查询客户端连接状态.
 * @param [in] client_handle - 客户端句柄.
 * @return 当前连接状态；NULL 句柄返回未连接.
 */
SnfRpcClientState snfRpcClientGetState(SnfRpcClientHandle client_handle);

/**
 * @brief 设置通知处理回调.
 * @param [in] client_handle - 客户端句柄.
 * @param [in] handler - 通知回调，NULL 表示取消接收.
 * @param [in] user_ctx - 回调上下文.
 * @return SNF_RPC_OK 成功，其他为 RPC 错误码.
 */
int32_t snfRpcClientSetNotifyHandler(SnfRpcClientHandle client_handle,
                                     SnfRpcClientNotifyHandler handler,
                                     void *user_ctx);

/**
 * @brief 设置本地通道的默认通知目标.
 * @param [in] client_handle - 客户端句柄.
 * @param [in] notify_remote_src - 非空的目标 src 字符串.
 * @return SNF_RPC_OK 成功，其他为 RPC 错误码.
 */
int32_t snfRpcClientSetNotifyRemoteSrc(SnfRpcClientHandle client_handle,
                                       const char *notify_remote_src);

/**
 * @brief 提交异步请求，后续通过回调返回结果.
 * @param [in] client_handle - 已连接的客户端句柄.
 * @param [in] method - 方法名.
 * @param [in] params - 借用的参数，内部深拷贝，调用方仍负责释放；可为 NULL.
 * @param [in] timeout_ms - 超时毫秒数，0 时使用客户端默认值.
 * @param [in] response_handler - 响应回调，不可为 NULL.
 * @param [in] user_ctx - 回调上下文.
 * @param [out] out_request_id - 成功时返回请求 ID，可为 NULL.
 * @return SNF_RPC_OK 表示请求已入队，其他为 RPC 错误码.
 * @note 需定期调用 snfRpcClientPollTimeouts() 驱动超时回调.
 */
int32_t snfRpcClientCallAsync(SnfRpcClientHandle client_handle,
                              const char *method,
                              cJSON *params,
                              uint32_t timeout_ms,
                              SnfRpcClientResponseHandler response_handler,
                              void *user_ctx,
                              uint32_t *out_request_id);

/**
 * @brief 提交请求并阻塞等待响应，不得在 RPC worker 内调用.
 * @param [in] client_handle - 已连接的客户端句柄.
 * @param [in] method - 方法名.
 * @param [in] params - 借用的参数，调用方仍负责释放；可为 NULL.
 * @param [in] timeout_ms - 等待响应的超时毫秒数，0 时使用客户端默认值.
 * @param [out] out_result - 结果副本，由调用方释放；传 NULL 时内部释放结果.
 * @param [out] out_error - 错误副本，由调用方释放；传 NULL 时内部释放错误.
 * @return SNF_RPC_OK 表示收到响应，业务错误需另读 out_error；其他为 RPC 错误码.
 */
int32_t snfRpcClientCallSync(SnfRpcClientHandle client_handle,
                             const char *method,
                             cJSON *params,
                             uint32_t timeout_ms,
                             cJSON **out_result,
                             cJSON **out_error);

/**
 * @brief 取消等待响应；已入队请求仍可能执行.
 * @param [in] client_handle - 客户端句柄.
 * @param [in] request_id - 待取消的请求 ID.
 * @return SNF_RPC_OK 成功，其他为 RPC 错误码.
 * @note 异步回调收到 SNF_RPC_ERR_TIMEOUT；同步等待被唤醒，无结果时返回 SNF_RPC_ERR_INVALID_STATE.
 */
int32_t snfRpcClientCancelPending(SnfRpcClientHandle client_handle, uint32_t request_id);

/**
 * @brief 检查等待项并在调用任务内执行超时回调.
 * @param [in] client_handle - 客户端句柄.
 * @return 本次处理的超时数量，参数无效时返回负数错误码.
 */
int32_t snfRpcClientPollTimeouts(SnfRpcClientHandle client_handle);

#endif /* __SONOFF_RPC_CLIENT_H__ */
