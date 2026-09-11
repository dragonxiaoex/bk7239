# RPC 模块 - 远程过程调用框架

## 概述

RPC（远程过程调用）模块提供请求分发、响应关联和通知能力，扩展了 `src` 和 `dst` 字段，采用单一全局总线架构。HTTP、WebSocket、MQTT、UART 等传输通过通道回调接入；本目录提供 RPC 核心和本地客户端，网络连接与报文收发由对应传输模块负责。

## 架构

```
┌──────────────────────────────────────────────────────────┐
│                   应用层                                 │
└──────────────────────────────────────────────────────────┘
                           ↓
┌──────────────────────────────────────────────────────────┐
│                    RPC 总线（核心）                      │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  通道       │  │   方法       │  │ 认证/队列   │  │
│  └─────────────┘  └──────────────┘  └──────────────┘  │
└──────────────────────────────────────────────────────────┘
                           ↓
┌──────────────────────────────────────────────────────────┐
│           传输层（多协议支持）                           │
│  ┌─────────┐  ┌──────────┐  ┌────────┐  ┌──────────┐   │
│  │ HTTP    │  │WebSocket │  │ MQTT   │  │  UART    │   │
│  └─────────┘  └──────────┘  └────────┘  └──────────┘   │
└──────────────────────────────────────────────────────────┘
```

## 核心功能

- **单一全局实例**：整个系统使用单一 RPC 总线
- **多协议支持**：HTTP、WebSocket、MQTT、UART、UDP、TCP、本地 IPC
- **消息队列**：异步请求缓冲，防止阻塞
- **身份验证**：摘要式身份验证，支持全局启用/禁用和方法豁免列表
- **方法注册**：由不同模块分布式注册方法
- **广播通知**：推送通知到所有连接的客户端
- **内部调用**：代码内 API 调用，无传输开销

## 消息格式

本实现兼容 JSON-RPC 2.0 结构，但对字段存在性做了放宽：

- 请求可以省略 `jsonrpc` 字段（当存在时会被验证为字符串 `"2.0"`）；
- 请求中的 `dst` 字段为可选，用于指明目标 RPC 名称，缺省时由传输通道或路由规则决定；
- 服务端生成的响应不包含 `jsonrpc` 字段（响应仍包含 `id` 以用于请求/响应关联）。

示例请求（省略 `jsonrpc` 与 `dst`）：

```json
{
    "id": 1,
    "src": "client-abc",
    "method": "device.getStatus",
    "params": {
        "verbose": true
    },
    "auth": {
        "realm": "esp32c6_device_001",
        "username": "admin",
        "nonce": "...",
        "cnonce": "...",
        "nc": 1,
        "response": "...",
        "algorithm": "SHA-256"
    }
}
```

示例响应（不包含 `jsonrpc` 字段）：

```json
{
    "id": 1,
    "src": "rpc-bus",
    "result": {
        "status": "success",
        "data": {}
    }
}
```

通知（无 `id`，`dst` 可选）：

```json
{
    "src": "rpc-bus",
    "method": "notify_method",
    "params": {}
}
```

## API 参考

### 总线管理

```c
// 初始化 RPC 总线
int32_t snfRpcInit(const char* src_name, uint32_t queue_size);

// 查询初始化时设置的 RPC 总线 SRC 名称
const char *snfRpcGetSrcName(void);

// 去初始化 RPC 总线
int32_t snfRpcDeinit(void);
```

### 通道管理

```c
// 注册通信通道
int32_t snfRpcChannelRegister(const SnfRpcChannelConfig* config,
                              SnfRpcChannelHandle* out_channel_handle);

// 注销通道
int32_t snfRpcChannelUnregister(SnfRpcChannelHandle channel_handle);

// 设置远程源标识符用于响应路由
int32_t snfRpcChannelSetRemoteSrc(SnfRpcChannelHandle channel_handle,
                                   const char* remote_src);

// 处理通道上的传入请求
int32_t snfRpcChannelHandleRequest(SnfRpcChannelHandle channel_handle,
                                    const char* request_json);
```

### 方法管理

```c
// 注册 RPC 方法
int32_t snfRpcMethodRegister(const char* method_name,
                             SnfRpcMethodHandler handler,
                             void* user_ctx);
int32_t snfRpcMethodUnregister(const char* method_name);

// 方法处理器类型定义
typedef cJSON* (*SnfRpcMethodHandler)(const char* method_name,
                                            cJSON* params,
                                            void* user_ctx);
```

**方法处理器返回值规范**：

处理器必须返回一个JSON对象，其中必须包含以下之一：

- `result`：成功响应，包含方法执行的结果数据
- `error`：错误响应，包含错误码和错误消息

```json
// 成功响应格式
{
    "result": {
        "status": "online",
        "data": {...}
    }
}

// 错误响应格式
{
    "error": {
        "code": -32602,
        "message": "Invalid params"
    }
}
```

### 身份验证

```c
// 启用/禁用全局身份验证
int32_t snfRpcAuthSetEnabled(bool enabled);

// 设置认证域（realm）
int32_t snfRpcAuthSetRealm(const char* realm);

// 设置身份验证凭证
int32_t snfRpcAuthSetCredentials(const char* username, const char* password);

// 直接设置预计算的 HA1（SHA256(username:realm:password)）
int32_t snfRpcAuthSetCredentialsHa1(const char* username, const char* ha1);

// 添加方法到身份验证豁免列表
int32_t snfRpcMethodSetExemptAuth(const char* method_name, bool enable);

// 获取认证挑战参数（realm/nonce/nc）
int32_t snfRpcAuthGetChallenge(SnfRpcAuthChallenge* out_challenge, bool refresh_nonce);
```

**认证参数说明（与实际代码一致）**：

- `realm`：认证域，服务端配置值，客户端必须原样带回
- `username`：用户名
- `nonce`：服务端随机串
- `cnonce`：客户端随机串
- `nc`：请求计数，支持8位十六进制字符串（如`00000001`）或数字
- `response`：摘要计算结果（SHA-256十六进制）
- `algorithm`：可选，若携带必须为`SHA-256`

**摘要计算规则**：

- `HA1 = SHA256(username:realm:password)`
- `HA2 = SHA256(method_prefix:method_suffix)`
- `response = SHA256(HA1:nonce:nc:cnonce:auth:HA2)`

说明：服务端可通过 `snfRpcAuthSetCredentialsHa1()` 直接注入已预计算的 `HA1`，这样无需在 RPC 层保存原始密码，也无需在请求校验时重复计算 `HA1`。

说明：当方法名为`module.action`时，`HA2`输入为`module:action`；若方法名不含`.`，则输入为`method:`。

**认证握手流程（RPC JSON，常见于WS）**：

1. 客户端首次访问受保护接口（未带或带错认证信息）。
2. 服务端返回JSON-RPC错误响应，错误码为`401`（或`SNF_RPC_ERR_ACCESS_DENIED`），并携带认证挑战参数。
3. 客户端使用用户名/密码和挑战参数计算`response`，再次发起RPC请求并带上完整`auth`字段。
4. 验证通过后，返回正常JSON-RPC应答（`result`）。
5. 后续请求复用认证上下文，`nc`必须递增；同一`nonce`下若`nc`不递增会被判定为重放并拒绝。

**认证失败挑战示例（WS JSON-RPC）**：

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "error": {
        "code": 401,
        "message": "{\"auth_type\":\"digest\",\"nonce\":\"...\",\"nc\":1,\"realm\":\"...\",\"algorithm\":\"SHA-256\"}"
    }
}
```

说明：测试页面会从`error.message`中解析challenge并自动重发带`auth`字段的请求。

**Web界面行为建议**：

- 在收到RPC错误码`401`/`SNF_RPC_ERR_ACCESS_DENIED`且本地无有效认证信息时，弹出登录框让用户输入账号密码。
- 若已缓存认证信息但再次收到认证拒绝，清理旧认证并重新弹出登录。
- 登录成功后缓存`realm/nonce/nc/cnonce`相关状态，后续请求递增`nc`，并更新response。

### 消息传递

```c
// 通过特定通道发送通知
int32_t snfRpcChannelNotify(SnfRpcChannelHandle channel_handle,
                            const char* method,
                            cJSON* params);

// 向所有通道广播通知
int32_t snfRpcBroadcastNotify(const char* method, cJSON* params);

// 在当前任务内直接调用方法；timeout_ms 目前为保留参数，不执行超时控制
cJSON *snfRpcCall(const char *method, cJSON *params, uint32_t timeout_ms);
```

### 轻量本地客户端（rpc_client）

`rpc_client` 用于内部模块（例如 cloud）以“本地持久连接对象”的方式接入 RPC 服务。

与 `snfRpcCall` 的差异：
- `snfRpcCall`：模块内直调，不具备客户端身份和通知接收能力
- `rpc_client`：具备 `src` 身份、请求 `id` 关联、异步回调和通知接收

首版行为：
- 不经过网络传输，复用 `snfRpcChannelRegister + snfRpcChannelHandleRequest`
- 使用 `LOCAL + PERSISTENT + auth_supported=false` 的内部通道
- 不内置分发线程和接收队列

`LOCAL` 表示通道不经过网络传输；方法是否属于内部方法由 `snfRpcMethodSetInternal()` 设置。
当前各通道的 `allow_internal_methods` 默认为 `false`，包括本地客户端通道，因此通道请求只能调用未标记为内部的方法。
代码内的 `snfRpcCall()` 直接执行 handler，不经过该通道权限检查，可以调用内部方法。

接口示例：

```c
int32_t snfRpcClientCreate(const SnfRpcClientConfig* config, SnfRpcClientHandle* out_client);
int32_t snfRpcClientConnect(SnfRpcClientHandle client_handle);
int32_t snfRpcClientCallAsync(SnfRpcClientHandle client_handle,
                              const char* method,
                              cJSON* params,
                              uint32_t timeout_ms,
                              SnfRpcClientResponseHandler response_handler,
                              void* user_ctx,
                              uint32_t* out_request_id);
int32_t snfRpcClientCallSync(SnfRpcClientHandle client_handle,
                             const char* method,
                             cJSON* params,
                             uint32_t timeout_ms,
                             cJSON** out_result,
                             cJSON** out_error);
int32_t snfRpcClientSetNotifyHandler(SnfRpcClientHandle client_handle,
                                     SnfRpcClientNotifyHandler handler,
                                     void* user_ctx);
int32_t snfRpcClientPollTimeouts(SnfRpcClientHandle client_handle);
```

线程上下文约束：

- `rpc_client` 的响应回调与通知回调默认运行在 RPC 下行回调上下文
- 若上层处理逻辑耗时，应在上层模块自行转入自己的队列或任务
- 响应回调的 `result/error` 和通知回调的 `params` 是独立副本，所有权交给回调；回调使用完毕后负责 `cJSON_Delete()`。通知方法名仅在回调期间有效。

异步请求需要由调用方定期调用 `snfRpcClientPollTimeouts()` 驱动超时回调。同步请求通过二值信号量等待 RPC worker 的响应，不能在同一个 worker 的方法处理器或回调中调用，否则会阻塞其自身处理请求。

## 使用示例

### 1. 初始化 RPC 总线

```c
#include "sonoff_rpc.h"

// 使用总线名称和队列大小初始化
snfRpcInit("device-rpc", 3);

// 启用身份验证
snfRpcAuthSetEnabled(true);
snfRpcAuthSetRealm("esp32c6_device_001");
snfRpcAuthSetCredentials("admin", "password123");

// 或者：如果外部已经持有 HA1，则可直接配置
// snfRpcAuthSetCredentialsHa1("admin", "<64-char-sha256-ha1>");

// 添加豁免方法
snfRpcMethodSetExemptAuth("system.ping", true);
snfRpcMethodSetExemptAuth("system.info", true);
```

### 2. 注册方法

**方法处理器必须返回带有 `result` 或 `error` 字段的JSON对象**

```c
// 成功响应的方法处理器
cJSON* handleDeviceStatus(const char* method,
                                  cJSON* params,
                                  void* user_ctx)
{
    // 创建响应对象
    cJSON* response = cJSON_CreateObject();

    // 创建结果对象
    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "online");
    cJSON_AddNumberToObject(result, "uptime", 3600);

    // 将结果添加到响应的result字段
    cJSON_AddItemToObject(response, "result", result);

    return response;
}

// 错误响应的方法处理器
cJSON* handleDeviceControl(const char* method,
                                   cJSON* params,
                                   void* user_ctx)
{
    cJSON* response = cJSON_CreateObject();

    // 验证参数
    if (params == NULL) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32602);
        cJSON_AddStringToObject(error, "message", "Invalid params");
        cJSON_AddItemToObject(response, "error", error);
        return response;
    }

    // 处理请求成功
    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "executed");
    cJSON_AddItemToObject(response, "result", result);

    return response;
}

// 注册方法
snfRpcMethodRegister("device.getStatus",
                     handleDeviceStatus,
                     NULL);

snfRpcMethodRegister("device.control",
                     handleDeviceControl,
                     NULL);
```

### 3. 方法处理器高级示例

#### 示例：处理参数并返回结果

```c
cJSON* handleSystemReboot(const char* method,
                                 cJSON* params,
                                 void* user_ctx)
{
    cJSON* response = cJSON_CreateObject();

    // 验证参数
    if (params == NULL) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32602);
        cJSON_AddStringToObject(error, "message", "Missing params");
        cJSON_AddItemToObject(response, "error", error);
        return response;
    }

    // 获取延迟参数
    cJSON* delay_node = cJSON_GetObjectItem(params, "delay_ms");
    int32_t delay_ms = 3000;  // 默认3秒

    if (JSON_IS_NUMBER(delay_node)) {
        delay_ms = (int32_t)delay_node->valuedouble;
    }

    // 执行重启逻辑
    if (delay_ms < 0) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32602);
        cJSON_AddStringToObject(error, "message", "Invalid delay value");
        cJSON_AddItemToObject(response, "error", error);
        return response;
    }

    // 成功响应
    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "scheduled");
    cJSON_AddNumberToObject(result, "delay_ms", delay_ms);
    cJSON_AddItemToObject(response, "result", result);

    return response;
}

snfRpcMethodRegister("system.reboot", handleSystemReboot, NULL);
```

#### 示例：错误处理

```c
cJSON* handleConfigSet(const char* method,
                              cJSON* params,
                              void* user_ctx)
{
    cJSON* response = cJSON_CreateObject();

    // 检查参数
    if (params == NULL) {
        goto error_invalid_params;
    }

    cJSON* key_node = cJSON_GetObjectItem(params, "key");
    cJSON* value_node = cJSON_GetObjectItem(params, "value");

    if (!JSON_IS_STRING(key_node)) {
        goto error_invalid_params;
    }

    const char* key = key_node->valuestring;

    // 执行配置设置
    int ret = config_set(key, value_node);
    if (ret != 0) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32500);
        cJSON_AddStringToObject(error, "message", "Config operation failed");
        cJSON_AddItemToObject(response, "error", error);
        return response;
    }

    // 成功
    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddItemToObject(response, "result", result);
    return response;

error_invalid_params:
    {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32602);
        cJSON_AddStringToObject(error, "message", "Invalid params");
        cJSON_AddItemToObject(response, "error", error);
        return response;
    }
}

snfRpcMethodRegister("config.set", handleConfigSet, NULL);
```

### 4. 注册传输通道

```c
// HTTP 通道接收回调
int32_t httpRecvCb(void* handle, char** out_message,
                     uint32_t* out_msg_len, uint32_t timeout_ms)
{
    // 模拟接收: char* msg = http_get_pending_request()
    // *out_message = msg;
    // *out_msg_len = strlen(msg);
    // return 0;
    return -1;  // 无消息
}

// HTTP 通道发送回调
int32_t httpSendCb(void* handle, const char* message, uint32_t msg_len)
{
    // 发送响应: http_send_response(message, msg_len);
    return 0;
}

// HTTP 通道释放回调
void httpFreeCb(void* handle, char* message)
{
    free(message);
}

// 注册通道
SnfRpcChannelConfig http_config = {
    .channel_name = "http_server",
    .transport_type = SNF_RPC_TRANSPORT_HTTP,
    .conn_mode = SNF_RPC_CONN_TEMPORARY,
    .channel_handle = http_server_ctx,
    .send_cb = (void*)httpSendCb,
    .recv_cb = (void*)httpRecvCb,
    .free_cb = (void*)httpFreeCb,
    .auth_supported = true
};

SnfRpcChannelHandle http_channel;
snfRpcChannelRegister(&http_config, &http_channel);
```

### 5. 处理请求

当 HTTP 服务器接收到 JSON-RPC 请求时：

```c
// 从客户端接收的 JSON-RPC 请求
const char* request_json = "{\"jsonrpc\":\"2.0\",\"id\":1,"
                          "\"src\":\"http-client-1\",\"dst\":\"device-rpc\","
                          "\"method\":\"device.getStatus\",\"params\":{}}";

// 传递给 RPC 模块
snfRpcChannelHandleRequest(http_channel, request_json);

// RPC 模块将：
// 1. 解析 JSON
// 2. 校验 src 并缓存响应目标，dst 当前不做匹配校验
// 3. 检查身份验证
// 4. 查找方法处理器
// 5. 调用处理器（处理器返回result或error）
// 6. 构建完整响应
// 7. 通过通道发送响应
```

### 6. 发送通知

```c
// 创建通知参数
cJSON* params = cJSON_CreateObject();
cJSON_AddStringToObject(params, "device_id", "device-001");
cJSON_AddStringToObject(params, "status", "reboot");

// 广播到所有通道
snfRpcBroadcastNotify("device.notify", params);

// 或发送到特定通道
snfRpcChannelNotify(http_channel, "device.notify", params);

// 清理
cJSON_Delete(params);
```

### 7. 内部方法调用

`snfRpcCall()` 返回已解包的 `result` 或 `error` 节点，直接读取其中的业务字段。

```c
// 从应用代码调用方法
cJSON* params = cJSON_CreateObject();
cJSON_AddNumberToObject(params, "delay_ms", 1000);

cJSON* response = snfRpcCall("device.reboot", params, 5000);

if (JSON_IS_OBJECT(response))
{
    /* 读取示例方法的成功响应字段 */
    cJSON* status_node = cJSON_GetObjectItem(response, "status");
    if (JSON_IS_STRING(status_node)) {
        const char* status = status_node->valuestring;
    }

    /* 读取错误响应字段 */
    cJSON* code_node = cJSON_GetObjectItem(response, "code");
    if (JSON_IS_NUMBER(code_node)) {
        int32_t code = (int32_t)code_node->valuedouble;
    }
}

cJSON_Delete(response);
cJSON_Delete(params);
```

### 8. 清理

```c
snfRpcChannelUnregister(http_channel);
snfRpcMethodUnregister("device.getStatus");
snfRpcDeinit();
```

## 错误码

返回状态的接口使用 [sonoff_rpc.h](sonoff_rpc.h) 中的 `SnfRpcStatus`，成功为 `SNF_RPC_OK`，失败为负数。`snfRpcClientPollTimeouts()` 成功时返回本次处理的超时数量；返回指针、状态枚举或布尔值的接口以各自声明为准。

```c
SNF_RPC_OK                      (0)         // 成功
SNF_RPC_ERR_INVALID_ARG         (-0x3215)   // 无效参数
SNF_RPC_ERR_NO_MEMORY           (-0x320D)   // 内存不足
SNF_RPC_ERR_NOT_FOUND           (-0x3217)   // 未找到
SNF_RPC_ERR_PARSE_ERROR         (-0x3201)   // JSON 解析错误
SNF_RPC_ERR_METHOD_NOT_FOUND    (-0x3203)   // 方法未找到
SNF_RPC_ERR_ACCESS_DENIED       (-0x3209)   // 身份验证失败
SNF_RPC_ERR_QUEUE_FULL          (-0x320A)   // 请求队列已满
SNF_RPC_ERR_NOT_INITED          (-0x320B)   // RPC 未初始化
```

## 线程安全

- RPC 总线使用互斥锁保护通道和方法列表
- 请求队列是线程安全的
- 方法处理器从工作线程上下文调用
- 方法处理器数据结构需要外部同步

## 性能考虑

- **单一工作线程**：顺序处理请求
- **异步处理**：请求队列防止阻塞
- **JSON 开销**：所有消息的序列化/反序列化成本
- **请求内存**：保留请求缓冲池；解析得到的 JSON 节点在处理后释放。
- **响应内存**：`snfRpcSendJsonMessage()` 使用 `cJSON_PrintUnformatted()` 动态分配字符串，发送回调返回后释放，发送失败也会释放；不再保留响应缓冲池。响应 JSON 最大长度仍为 2048 字节（不含终止符），超限返回 `SNF_RPC_ERR_INVALID_SIZE`。长度检查发生在序列化完成后，不能用此上限约束序列化期间的内存峰值。
- **发送回调**：消息字符串仅在回调执行期间有效，回调不得释放它；异步发送须在返回前复制消息。

## 与其他模块的集成

RPC 模块与以下模块集成：

- **SDK cJSON** (`cJSON.h`)：消息解析和构建；类型判断使用 `sonoff_rpc.h` 中的 `JSON_IS_*` 宏，取值前需校验节点类型。
- **日志模块** (`sonoff_log.h`)：使用各源文件的 `tag` 和 `LOG_E`、`LOG_W`、`LOG_I` 输出日志；原调试日志使用 `LOG_I`。
- **SHA-256** ([sonoff_sha256.h](../utils/sonoff_sha256.h))：复用项目流式哈希接口计算认证摘要，再转换为小写十六进制字符串。
- **SDK 随机数** (`driver/trng.h`)：通过 `bk_rand()` 生成认证 nonce。
- **FreeRTOS**：直接使用互斥锁、二值/计数信号量、消息队列和任务接口；接口超时单位为毫秒，通过 `pdMS_TO_TICKS()` 转换。客户端等待项使用 `TimeOut_t`、`vTaskSetTimeOutState()` 和 `xTaskCheckForTimeOut()` 记录与检查超时。
- **任务配置** ([sonoff_task_def.h](../main/sonoff_task_def.h))：RPC worker 的任务名称、栈元素数和优先级由 `SONOFF_RPC_TASK_*` 宏定义。与 OTA、HTTP 一致，栈大小直接传入 `xTaskCreate()`，单位为 `StackType_t` 元素，本平台每个元素为 4 字节。

公开接口、类型和错误码分别使用 `snfRpc*`、`SnfRpc*` 和 `SNF_RPC_*` 命名。模块不再依赖 AIS 头文件；FreeRTOS 接口内部按 `pdPASS`/`pdFALSE` 判断，不与 RPC 返回码混用。

SDK 的 `cJSON_Duplicate()` 在复制常量键时会分配新键名，需要清除副本的 `cJSON_StringIsConst` 标记以便正常释放。修正在 [cJSON 覆盖文件](../../sonoff_modify/idk_modify/components/json/cJSON.c) 中，由项目构建覆盖流程应用；原厂源文件保持不变。

## 限制

- 单一工作线程 - 适合中等请求速率
- 方法处理器必须快速完成（阻塞总线）
- 请求队列容量为 1～3，小请求缓冲池为 3×512 字节；超过 512 字节的请求共用一个按需分配的大缓冲区，单条请求上限为 4096 字节（不含终止符）。
- 身份验证是可选的（必须明确启用）
- 方法名和 src 字符串缓冲区为 64 字节，最多存放 63 字节内容和终止符。
