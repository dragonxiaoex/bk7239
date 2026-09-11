/**
 * @file    sonoff_rpc.h
 * @brief   RPC（远程过程调用）模块 - 基于 JSON-RPC 2.0 的分布式通信框架
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_RPC_H__
#define __SONOFF_RPC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

/**
 * @brief JSON 节点类型判断，屏蔽引用和常量键标记
 * @note 参数应为节点指针变量，不得传入带副作用的表达式.
 */
#define JSON_TYPE_MASK          0xFF
#define JSON_IS_OBJECT(json)    (((json) != NULL) && (((uint32_t)((json)->type) & JSON_TYPE_MASK) == cJSON_Object))
#define JSON_IS_ARRAY(json)     (((json) != NULL) && (((uint32_t)((json)->type) & JSON_TYPE_MASK) == cJSON_Array))
#define JSON_IS_STRING(json)    (((json) != NULL) && (((uint32_t)((json)->type) & JSON_TYPE_MASK) == cJSON_String))
#define JSON_IS_NUMBER(json)    (((json) != NULL) && (((uint32_t)((json)->type) & JSON_TYPE_MASK) == cJSON_Number))
#define JSON_IS_NULL(json)      (((json) != NULL) && (((uint32_t)((json)->type) & JSON_TYPE_MASK) == cJSON_NULL))

/** @brief RPC接口返回码. */
typedef enum
{
    SNF_RPC_OK = 0,
    SNF_RPC_ERR_PARSE_ERROR = -0x3201,       /**< JSON 解析错误 */
    SNF_RPC_ERR_INVALID_REQUEST = -0x3202,   /**< RPC 请求格式无效 */
    SNF_RPC_ERR_METHOD_NOT_FOUND = -0x3203,  /**< 方法未找到 */
    SNF_RPC_ERR_INVALID_PARAMS = -0x3204,    /**< 方法参数无效 */
    SNF_RPC_ERR_INTERNAL = -0x3205,          /**< RPC 内部错误 */
    SNF_RPC_ERR_SERVER_BUSY = -0x3206,       /**< 服务器繁忙（队列满） */
    SNF_RPC_ERR_TIMEOUT = -0x3207,           /**< 请求超时 */
    SNF_RPC_ERR_DST_INVALID = -0x3208,       /**< 无效的目标地址 */
    SNF_RPC_ERR_ACCESS_DENIED = -0x3209,     /**< 访问被拒绝（认证失败） */
    SNF_RPC_ERR_QUEUE_FULL = -0x320A,        /**< 请求队列已满 */
    SNF_RPC_ERR_NOT_INITED = -0x320B,        /**< RPC 总线未初始化 */
    SNF_RPC_ERR_INVALID_STATE = -0x320C,     /**< 当前状态不允许执行此操作 */
    SNF_RPC_ERR_NO_MEMORY = -0x320D,         /**< 内存不足 */
    SNF_RPC_ERR_NOT_SUPPORTED = -0x320E,     /**< 不支持的操作 */
    SNF_RPC_ERR_INVALID_SIZE = -0x320F,      /**< 请求数据大小超过限制或不对 */
    SNF_RPC_ERR_CHECKSUM_FAILED = -0x3210,   /**< 数据校验失败（SHA256/CRC等） */
    SNF_RPC_ERR_DECRYPT_FAILED = -0x3211,    /**< 数据解密失败 */
    SNF_RPC_ERR_MODEL_FAILED = -0x3212,      /**< 设备型号不匹配 */
    SNF_RPC_ERR_VERSION_FAILED = -0x3213,    /**< 设备版本不匹配 */
    SNF_RPC_ERR_NO_PASSWORD = -0x3214,       /**< 未设置密码，访问被拒绝 */
    SNF_RPC_ERR_INVALID_ARG = -0x3215,       /**< 参数无效. */
    SNF_RPC_ERR_BUSY = -0x3216,              /**< 资源忙. */
    SNF_RPC_ERR_NOT_FOUND = -0x3217,         /**< 对象不存在. */
} SnfRpcStatus;

#define SNF_RPC_AUTH_SHA256_HEX_LEN      64
#define SNF_RPC_MAX_REALM_LEN            64
#define SNF_RPC_MAX_AUTH_NONCE_LEN       32

/**
 * @brief 传输层类型枚举
 */
typedef enum
{
    SNF_RPC_TRANSPORT_LOCAL,      /**< 本地 */
    SNF_RPC_TRANSPORT_SERIAL,     /**< 串口通信 */
    SNF_RPC_TRANSPORT_UDP,        /**< UDP 协议 */
    SNF_RPC_TRANSPORT_HTTP,       /**< HTTP 协议 */
    SNF_RPC_TRANSPORT_WEBSOCKET,  /**< WebSocket 协议 */
    SNF_RPC_TRANSPORT_MQTT,       /**< MQTT 协议 */
} SnfRpcTransportType;

/**
 * @brief 连接模式枚举
 */
typedef enum
{
    SNF_RPC_CONN_PERSISTENT,  /**< 持久连接（如 WebSocket、MQTT、持久 TCP） */
    SNF_RPC_CONN_TEMPORARY,   /**< 临时连接（如 HTTP 请求/响应） */
    SNF_RPC_CONN_STATELESS,   /**< 无状态连接（如 UDP、HTTP 带地址） */
} SnfRpcConnectionMode;

/**
 * @brief 通道配置结构体
 *
 * 描述用于 RPC 消息路由的通信通道。
 * 每个通道关联用于发送消息的回调函数。
 */
typedef struct
{
    const char *channel_name;            /**< 唯一的通道名称/标识符 */
    SnfRpcTransportType transport_type;  /**< 传输协议类型 */
    SnfRpcConnectionMode conn_mode;      /**< 连接持久化模式 */

    void *channel_handle;  /**< 传递给回调的不透明句柄（上下文） */

    /* 回调函数 - 函数指针 */
    int32_t (*send_cb)(void *handle, const char *message, uint32_t msg_len);  /**< 发送回调函数 */

    bool auth_supported;  /**< 此通道是否支持认证 */
} SnfRpcChannelConfig;

/**
 * @brief 认证挑战信息
 */
typedef struct
{
    char realm[SNF_RPC_MAX_REALM_LEN];
    char nonce[SNF_RPC_MAX_AUTH_NONCE_LEN + 1];
    uint32_t nc;
} SnfRpcAuthChallenge;

/**
 * @brief RPC 运行统计信息
 */
typedef struct
{
    uint32_t req_total;         /**< 收到的请求总数（含拒绝） */
    uint32_t req_accepted;      /**< 成功入队的请求数 */
    uint32_t req_dropped;       /**< 被拒绝的请求数 */
    uint32_t queue_full;        /**< 队列满次数 */
    uint32_t pool_busy;         /**< 请求缓冲池耗尽次数 */
    uint32_t invalid_request;   /**< 请求格式无效次数 */
    uint32_t invalid_params;    /**< 参数无效次数 */
    uint32_t auth_failed;       /**< 认证失败次数 */
    uint32_t method_not_found;  /**< 方法不存在次数 */
    uint32_t rate_limited;      /**< 限流拒绝次数 */
    uint32_t circuit_open;      /**< 熔断拒绝次数 */
    uint32_t slow_request;      /**< 慢请求次数 */
    uint32_t inflight;          /**< 当前处理中请求数 */
    uint32_t inflight_max;      /**< 处理中请求峰值 */
} SnfRpcStats;

/**
 * @brief RPC 通道句柄
 */
typedef void *SnfRpcChannelHandle;

/**
 * @brief 当前 RPC 请求上下文
 *
 * @note  仅在 RPC 方法处理器执行期间有效。
 */
typedef struct
{
    SnfRpcTransportType transport_type;  /**< 当前请求的传输类型 */
    SnfRpcConnectionMode conn_mode;      /**< 当前请求的连接模式 */
    const char *channel_name;            /**< 当前请求所属通道名 */
    void *channel_handle;                /**< 当前请求所属通道句柄 */
} SnfRpcRequestContext;

/**
 * @brief RPC 方法处理器回调函数类型
 *
 * 当外部 RPC 请求调用已注册的方法时调用此函数
 *
 * @param [in]  method   方法名称
 * @param [in]  params   方法参数（JSON 对象/数组），无参数时为 NULL
 * @param [in]  user_ctx 注册时传入的用户自定义上下文
 *
 * @return     独立的 JSON 响应对象，顶层必须包含 result 或 error 之一（二者互斥）；无法构造响应时返回 NULL.
 * @note       返回对象的所有权转移给 RPC，由 RPC 负责释放；处理器不得再使用或释放该对象.
 * @note       不得直接返回 params 或其他 JSON 树中的节点；需要复用时先深拷贝为独立对象.
 */
typedef cJSON *(*SnfRpcMethodHandler)(const char *method,
                                      cJSON *params,
                                      void *user_ctx);

/**
 * @brief RPC worker 探针回调函数类型
 *
 * RPC worker 会在自身线程上下文中执行该回调，用于 daemon 确认 worker 仍可调度
 *
 * @param [in] ctx 注册探针时传入的用户上下文
 */
typedef void (*SnfRpcWorkerProbeCallback)(void *ctx);

/**
 * @brief RPC 方法枚举回调函数
 *
 * @param [in] method_name  已注册方法名
 * @param [in] ctx          用户上下文
 */
typedef void (*SnfRpcMethodEnumCb)(const char *method_name, void *ctx);

/**
 * @brief 通道发送回调函数类型
 *
 * RPC 模块通过此回调向通道发送响应/通知消息。
 * message 仅在回调执行期间有效，回调不得释放它；异步发送须在返回前复制消息。
 *
 * @param [in]  handle   通道句柄（注册时传入）
 * @param [in]  message  JSON 格式的消息字符串
 * @param [in]  msg_len  消息长度（不含终止符）
 *
 * @return     成功返回 0，失败返回负数错误码
 */
typedef int32_t (*SnfRpcChannelSend)(void *handle,
                                     const char *message,
                                     uint32_t msg_len);

/**
 * @brief   初始化 RPC 总线
 *
 * 创建 RPC 总线、工作线程和消息队列。在进行其他 RPC 操作前必须调用一次。
 *
 * @param [in]  src_name     RPC 总线的源名称（用作请求中的 dst 字段）
 * @param [in]  queue_size   请求消息队列大小，取值 1～3，不超过小请求缓冲池数量.
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcInit(const char *src_name, uint32_t queue_size);

/**
 * @brief   获取当前 RPC 总线源名称
 *
 * 返回在 snfRpcInit() 中设置的 src_name，总线未初始化时返回 NULL。
 *
 * @return  const char*  当前总线源名称，未初始化时返回 NULL
 */
const char *snfRpcGetSrcName(void);

/**
 * @brief   反初始化 RPC 总线
 *
 * 停止工作线程、清空队列、注销所有通道和方法。之后需重新调用 snfRpcInit 才能使用 RPC。
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcDeinit(void);

/**
 * @brief   注册通信通道
 *
 * 向 RPC 总线添加新通道用于消息路由。每个通道代表一条通信路径（如 HTTP 连接、WebSocket 等）。
 *
 * @param [in]  config               通道配置
 * @param [out] out_channel_handle   存储分配的通道句柄
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcChannelRegister(const SnfRpcChannelConfig *config,
                              SnfRpcChannelHandle *out_channel_handle);

/**
 * @brief   注销通信通道
 *
 * 从 RPC 总线移除已注册的通道。
 *
 * @param [in]  channel_handle   通道句柄
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcChannelUnregister(SnfRpcChannelHandle channel_handle);

/**
 * @brief   获取当前 RPC 方法处理器对应的请求上下文
 *
 * @param [out] ctx  输出请求上下文
 *
 * @return  SNF_RPC_OK 表示获取成功；当前不在 RPC 方法处理器中时返回 SNF_RPC_ERR_NOT_FOUND
 */
int32_t snfRpcGetCurrentRequestContext(SnfRpcRequestContext *ctx);

/**
 * @brief   注册 RPC 执行探针回调
 *
 * @param [in] cb   worker 线程执行的探针回调
 * @param [in] ctx  传给探针回调的上下文
 *
 * @return  SNF_RPC_OK 表示提交成功，SNF_RPC_ERR_BUSY 表示上一轮探针尚未执行
 */
int32_t snfRpcWorkerProbeRegister(SnfRpcWorkerProbeCallback cb, void *ctx);

/**
 * @brief   为通道设置远程源标识
 *
 * 缓存远程客户端的源标识（来自请求的 src 字段）。
 * 用作发送回客户端的响应/通知消息的 dst 字段。
 * 通常在处理请求时由 RPC 模块自动调用。
 *
 * @param [in]  channel_handle   通道句柄
 * @param [in]  remote_src       远程客户端源标识字符串
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcChannelSetRemoteSrc(SnfRpcChannelHandle channel_handle, const char *remote_src);

/**
 * @brief   为通道设置通知默认目标源标识
 *
 * 用于“连出”场景：在未收到对端请求前，也可以向固定目标发送通知。
 * 通道处理请求后，响应仍使用请求里的 src（通过 snfRpcChannelSetRemoteSrc 缓存）。
 *
 * @param [in]  channel_handle      通道句柄
 * @param [in]  notify_remote_src   通知默认目标源标识字符串
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcChannelSetNotifyRemoteSrc(SnfRpcChannelHandle channel_handle, const char *notify_remote_src);

/**
 * @brief   处理通道上的传入请求消息
 *
 * 当通道（传输层）接收到 JSON-RPC 请求时调用。
 * RPC 模块会解析、验证、认证并处理请求，然后通过同一通道发送响应。
 *
 * @param [in]  channel_handle   通道句柄
 * @param [in]  request_json     JSON 格式的请求字符串
 *
 * @return     成功返回 0，失败返回负数错误码
 *             注意：响应通过通道异步发送
 */
int32_t snfRpcChannelHandleRequest(SnfRpcChannelHandle channel_handle, const char *request_json);

/**
 * @brief   注册 RPC 方法
 *
 * 允许模块暴露可通过 RPC 调用的 API 方法。
 *
 * @param [in]  method_name      方法名称（必须唯一）
 * @param [in]  handler          处理方法调用的回调函数
 * @param [in]  user_ctx         用户定义的上下文（传入处理器）
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcMethodRegister(const char *method_name,
                             SnfRpcMethodHandler handler,
                             void *user_ctx);

/**
 * @brief   注销 RPC 方法
 *
 * 从 RPC 总线移除已注册的方法。
 *
 * @param [in]  method_name      要注销的方法名称
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcMethodUnregister(const char *method_name);

/**
 * @brief   枚举已注册的 RPC 方法
 *
 * @param [in]  filter      可选前缀过滤；NULL 或空串表示不过滤
 * @param [in]  cb          枚举回调（不能为空）
 * @param [in]  ctx         用户上下文
 *
 * @note       回调在调用任务持有方法互斥锁时执行，应快速返回，不得重入需要该锁的方法管理接口.
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcMethodEnumerate(const char *filter, SnfRpcMethodEnumCb cb, void *ctx);

/**
 * @brief   启用或禁用全局认证
 *
 * 控制 RPC 通道在处理请求前是否必须进行认证。
 *
 * @param [in]  enabled          true 启用认证，false 禁用认证
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcAuthSetEnabled(bool enabled);

/**
 * @brief   设置认证域（realm）
 *
 * 通常为设备型号与ID构成，用于HA1计算。
 *
 * @param [in]  realm            realm 字符串
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcAuthSetRealm(const char *realm);

/**
 * @brief   设置认证凭证
 *
 * 存储用户名和密码用于摘要认证。
 * 若 realm 已配置，会立即预计算并缓存 HA1；后续认证直接使用缓存值。
 *
 * @param [in]  username         用户名字符串
 * @param [in]  password         密码字符串（会被用于计算并缓存 HA1）
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcAuthSetCredentials(const char *username, const char *password);

/**
 * @brief   直接设置认证凭证的 HA1
 *
 * 当调用方已经持有 `SHA256(username:realm:password)` 的 64 位十六进制字符串时，
 * 可通过此接口直接配置认证凭证，避免在 RPC 层保存原始密码或重复计算 HA1。
 * 若后续修改 realm，调用方应重新设置匹配新 realm 的 HA1。
 *
 * @param [in]  username         用户名字符串
 * @param [in]  ha1              64 位十六进制 SHA-256 HA1 字符串
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcAuthSetCredentialsHa1(const char *username, const char *ha1);

/**
 * @brief   获取认证挑战信息（用于 HTTP/WS 传输层）
 *
 * @param [out] out_challenge    返回的挑战信息
 * @param [in]  refresh_nonce    是否生成新 nonce
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcAuthGetChallenge(SnfRpcAuthChallenge *out_challenge, bool refresh_nonce);

/**
 * @brief   通过指定通道发送广播通知
 *
 * 发送 JSON-RPC 通知（不期望响应）通过通道发送到缓存的远程客户端标识。
 *
 * @param [in]  channel_handle   通道句柄
 * @param [in]  method           方法名称
 * @param [in]  params           参数（可为 NULL）。所有权不转移 - 调用者需自行释放。
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcChannelNotify(SnfRpcChannelHandle channel_handle,
                            const char *method,
                            cJSON *params);

/**
 * @brief   向所有通道广播通知
 *
 * 遍历所有已注册的通道，向缓存了 remote_src 的客户端发送通知。
 *
 * @param [in]  method           方法名称
 * @param [in]  params           参数（可为 NULL）。所有权不转移 - 调用者需自行释放。
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcBroadcastNotify(const char *method, cJSON *params);

/**
 * @brief   向指定通道名称发送通知
 *
 * 根据通道名称查找已注册的通道，并向其发送通知。
 * 通道名称在注册时唯一标识一个连接（如 "ws-3-1741712345"）。
 *
 * @param [in]  channel_name     目标通道名称
 * @param [in]  method           方法名称
 * @param [in]  params           参数（可为 NULL）。所有权不转移 - 调用者需自行释放。
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcNotifyByChannelName(const char *channel_name,
                                  const char *method,
                                  cJSON *params);

/**
 * @brief   从代码中调用 RPC 方法
 *
 * 内部调用已注册的方法并等待结果。用于不经过传输通道的内部代码 API 调用。
 *
 * @param [in]  method           方法名称
 * @param [in]  params           参数（可为 NULL）。所有权不转移 - 调用者需自行释放。
 * @param [in]  timeout_ms       保留参数，当前直调实现不执行超时控制.
 *
 * @return     结果 JSON 节点指针，错误时返回 NULL。
 *             调用者使用完毕后需调用 cJSON_Delete() 释放返回值。
 */
cJSON *snfRpcCall(const char *method, cJSON *params, uint32_t timeout_ms);

/**
 * @brief   设置方法属性--认证豁免
 *          需在方法注册之后调用
 *
 * @param [in]   method          方法名称
 * @param [in]   enable          true-豁免 false-默认需认证
 * @return  int32_t
 */
int32_t snfRpcMethodSetExemptAuth(const char *method, bool enable);

/**
 * @brief   设置方法属性--内部方法
 *          需在方法注册之后调用
 *
 * @param [in]   method          方法名称
 * @param [in]   enable          true-内部方法 false-默认公开方法
 * @return  int32_t
 */
int32_t snfRpcMethodSetInternal(const char *method, bool enable);

/**
 * @brief   设置全局限流（令牌桶）
 *
 * @param [in]  rps         每秒允许请求数，0 表示禁用限流
 * @param [in]  burst       突发容量（最大令牌数）
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcSetRateLimit(uint32_t rps, uint32_t burst);

/**
 * @brief   配置熔断器（抗压保护）
 *
 * @param [in]  enabled     是否启用熔断
 * @param [in]  threshold   窗口内错误阈值
 * @param [in]  window_ms   统计窗口（毫秒）
 * @param [in]  cooldown_ms 熔断冷却时间（毫秒）
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcSetCircuitBreaker(bool enabled, uint32_t threshold, uint32_t window_ms, uint32_t cooldown_ms);

/**
 * @brief   获取 RPC 运行统计
 *
 * @param [out] out_stats   统计信息输出
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcGetStats(SnfRpcStats *out_stats);

/**
 * @brief   清零 RPC 运行统计
 *
 * @return     成功返回 0，失败返回负数错误码
 */
int32_t snfRpcResetStats(void);

#endif /* __SONOFF_RPC_H__ */
