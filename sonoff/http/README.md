# HTTP RPC 与 WebSocket

本目录实现设备端的 `http://<IP>/rpc` 和 `ws://<IP>/rpc`，共用 TCP 80 端口。HTTP 每次处理一条 RPC 调用，发送响应后关闭连接；WebSocket 保持连接并支持设备通知。

当前服务仅路由 `/rpc`，不返回网页，也不再路由原 CGI 和 OTA 上传入口。其他路径返回 HTTP 404。

## 应用接入

应用负责在网络服务启动前完成以下工作：

1. 初始化设备控制模块，准备作为响应 `src` 的设备 ID。
2. 调用 `snfRpcInit(device_id, 3)` 初始化 RPC。
3. 使用 `snfRpcMethodRegister()` 注册业务方法，例如 `Switch.Set`。
4. 如需认证，分别配置 HTTP 和 RPC 的认证信息。
5. 调用 `snfHttpServerStart()`。现有主任务在 AP 就绪事件中调用该接口，应用需保证前面的初始化和注册已完成。

HTTP 模块不创建 RPC 总线、不注册开关业务。RPC 未初始化时，调用或升级返回 HTTP 503。`Switch.Set` 必须由产品层实现；仅验证 HTTP 通道时，可先使用 RPC 自带的 `Rpc.GetStats`。

业务 handler 返回独立的 `{"result":{...}}` 或 `{"error":{...}}` 对象，所有权交给 RPC；外层 `id/src/dst` 由 RPC 生成。HTTP/WebSocket 直接发送 RPC 生成的 JSON。

## HTTP 调用

```bash
export SONOFF=192.168.33.1

curl -i -H 'Content-Type: application/json' \
  -d '{"id":1,"src":"user_1","method":"Rpc.GetStats"}' \
  "http://${SONOFF}/rpc"
```

业务方法注册后，可调用开关：

```bash
curl -i -H 'Content-Type: application/json' \
  -d '{"id":2,"src":"user_1","method":"Switch.Set","params":{"id":0,"on":true}}' \
  "http://${SONOFF}/rpc"
```

也接受普通 `curl -d` 发送的 JSON 正文，不强制检查请求 `Content-Type`。响应包含 `Content-Type: application/json`、准确的 `Content-Length` 和 `Connection: close`。正常 RPC 响应（包含 RPC 错误对象）使用 HTTP 200。

HTTP 只接受一条带数字 `id` 的 JSON 对象；无 ID 通知、批量数组和不合法 JSON 返回 400，且不执行业务。此通道不发送通知。

## 独立 HTTP 摘要认证

HTTP 初始没有认证数据，无需认证即可调用。应用使用 [sonoff_http.h](sonoff_http.h) 中的接口配置，配置仅保存在内存；持久化和上电恢复由应用负责。

```c
snfHttpSetAuthInfo("admin", "your-password", device_id);
snfHttpSetAuthUri("/rpc");
```

第三个参数为认证域 `realm`，传入 `NULL` 使用 `sonoff`。HTTP 只保存计算后的 SHA-256 HA1，不长期保存明文密码。用户名最多 31 字节，realm 和受保护路径最多 63 字节；设置接口返回 0 表示成功，负数表示失败。

受保护路径默认是 `/rpc`。`snfHttpSetAuthUri()` 指定精确匹配的资源路径，不创建登录接口；设置为 `/auth` 不会保护 `/rpc`，也不会创建 `/auth` 路由。

```bash
curl -i --digest -u 'admin:your-password' \
  -H 'Content-Type: application/json' \
  -d '{"id":3,"src":"user_1","method":"Rpc.GetStats"}' \
  "http://${SONOFF}/rpc"
```

认证使用 SHA-256、`qop="auth"` 和 HTTP 请求的 `method:uri`。未携带凭据或校验失败时返回 HTTP 401 和 `WWW-Authenticate`，curl 自动重新发起请求。HTTP 的 `nc` 按标准 8 位十六进制文本参与摘要计算。

质询跨短连接保留，最多缓存 8 条，有效期 5 分钟；计数必须递增。更新凭据或受保护路径会使旧质询失效。清除认证信息可恢复免认证：

```c
snfHttpSetAuthInfo(NULL, NULL, NULL);
```

这些设置不修改 RPC 或 WebSocket 的消息认证配置。

## WebSocket 调用与通知

```bash
websocat "ws://${SONOFF}/rpc"
```

连接后输入完整 JSON：

```json
{"id":4,"src":"user_1","method":"Switch.Set","params":{"id":0,"on":true}}
```

WebSocket 认证沿用 RPC 的 `auth` 字段，升级握手不要求 HTTP Digest。应用需要通过 `snfRpcAuthSetRealm()`、`snfRpcAuthSetCredentials()` 或 `snfRpcAuthSetCredentialsHa1()`、`snfRpcAuthSetEnabled()` 配置 RPC 认证。字段计算方式见 [SONOFF 认证文档](https://sonoff-api.github.io/docs/General/Authentication)。

当前 RPC 在未配置 HA1 时拒绝非豁免方法，即使 RPC 认证开关关闭。无认证联调需要应用配置凭据后关闭 RPC 认证，或者把明确允许免认证的测试方法设为豁免。HTTP 自身未配置认证时不受这个前提限制。

连接收到包含有效 `src` 的请求后，RPC 缓存远端标识，应用可通过 `snfRpcBroadcastNotify()` 等已有接口发送通知。握手完成但尚未发送请求的连接不会接收通知。通知由应用业务产生，HTTP 模块不自行生成开关状态通知。

帧处理支持客户端掩码、文本分片重组、Ping/Pong、Close 和 UTF-8 校验。拒绝二进制数据帧，不协商压缩扩展。

## 资源与边界

| 项目 | 当前配置 |
|---|---|
| 同时在线连接 | 4 条，HTTP 与 WebSocket 共用 |
| HTTP 请求头 | 最大 2048 字节 |
| RPC 请求/重组后的文本消息 | 最大 4096 字节，不含结束符 |
| HTTP 接收超时 | 10 秒 |
| HTTP 等待 RPC 响应 | 10 秒，超时返回 504 |
| 套接字发送超时 | 5 秒 |
| WebSocket 空闲连接 | 不套用 HTTP 接收超时 |
| HTTP 正文分帧 | 必须提供 Content-Length；不支持 chunked，返回 501 |
| Expect | 支持 100-continue，认证先于正文接收 |
| 连接任务栈 | 2048 个 StackType_t 元素，本平台为 8 KiB |

监听任务持续接收新连接，每条连接有独立任务；同一连接的控制帧、RPC 响应和通知串行发送。套接字发送处理短写，输入流缓存处理 TCP 分包和粘包。

RPC 发送回调只借用 JSON 字符串，本模块在回调返回前发送完毕。连接关闭后，其标识立即失效，迟到回调不会写入复用后的连接。HTTP 超时结束的是传输等待，不能撤销已经提交给 RPC 的业务操作。

文件分工：`sonoff_http.c` 管理连接和 RPC 通道，`sonoff_http_io.c` 负责收包与头解析，`sonoff_http_auth.c` 负责 HTTP Digest，`sonoff_websocket.c` 负责握手计算与帧处理。
