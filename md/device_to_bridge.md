# Wi-Fi 设备接入 Bridge 规范

文档版本：`1.0`  
协议版本：`1`  
适用组件：`ai-assistant-bridge.exe`

本文档是 Wi-Fi 芯片/LCD 设备接入 PC Bridge 的实现依据。设备作为 TCP 客户端，主动连接局域网内的 Windows PC；Bridge 负责推送 Codex 和 Cursor 的任务状态、摘要、操作信息及额度信息。

## 1. 接入参数

| 项目 | 要求 |
|---|---|
| 传输层 | TCP 长连接 |
| 默认端口 | `7878` |
| 应用层格式 | JSON Lines，简称 JSONL |
| 字符编码 | UTF-8，无 BOM |
| 帧结束符 | 单个换行字节 `0x0A`，即 `\n` |
| 默认心跳 | Bridge 每 30 秒发送一次 `ping` |
| 默认断线判定 | Bridge 连续 75 秒未收到设备的任何完整消息后断开连接 |
| 连接方向 | 设备主动连接 PC，PC 不主动连接设备 |
| 协议版本 | `1` |

本协议不是 HTTP、WebSocket 或 UDP。设备不得在 JSON 前添加长度字段，也不得使用 `Content-Length`。

## 2. PC Bridge 的局域网配置

设备不能连接 `0.0.0.0`。`0.0.0.0` 只表示 Bridge 在 PC 的全部网卡上监听；设备必须连接 PC 的真实局域网 IPv4 地址，例如 `192.168.1.23`。

项目根目录的 `start-bridge.cmd` 实际读取：

```text
dist\bridge.json
```

接入硬件前，停止 Bridge，将其中的 TCP 配置改为：

```json
{
  "tcp": {
    "listen_address": "0.0.0.0",
    "port": 7878,
    "heartbeat_seconds": 30,
    "client_timeout_seconds": 75,
    "auth_token": "替换为设备和Bridge共同保存的随机密钥"
  }
}
```

修改后重新启动 Bridge。TCP 监听地址、端口和认证密钥只在 Bridge 启动时加载。

注意事项：

- 不要只修改 `config\bridge.json`；通过根目录 `start-bridge.cmd` 启动时使用的是 `dist\bridge.json`。
- 局域网监听时强烈建议配置 `auth_token`。建议至少使用 128 位随机值，例如 32 个随机十六进制字符。
- 当前协议没有 TLS，Token 和任务状态在局域网内以明文 TCP 传输。仅应在可信的 WPA2/WPA3 网络或隔离 VLAN 中使用，不要做路由器端口映射。
- 在 Windows 防火墙中允许 `ai-assistant-bridge.exe` 的专用网络 TCP 入站连接，端口为 `7878`。
- 使用 `ipconfig` 查看 PC 当前网卡的 IPv4 地址。建议在路由器中为 PC 设置 DHCP 地址保留，避免地址变化。

PC 端可先执行以下命令验证局域网监听：

```powershell
.\device-simulator.cmd -Address 192.168.1.23 -Port 7878 -Token "与bridge.json一致的Token"
```

请将示例 IP 和 Token 替换为实际值。从另一台局域网 PC 测试更能覆盖 Windows 防火墙问题。

## 3. TCP 与 JSONL 处理要求

TCP 是字节流，不保留消息边界。一次 `recv()` 可能只收到半条 JSON，也可能同时收到多条 JSON。设备必须维护接收缓冲区，并以字节 `0x0A` 拆分完整帧。

正确的处理顺序：

1. 将每次收到的字节追加到接收缓冲区。
2. 在字节层查找 `0x0A`。
3. 取出 `0x0A` 之前的完整帧，并移除可选的尾部 `0x0D`。
4. 对完整帧做一次 UTF-8 解码。
5. 解析 JSON 对象并按 `type` 分发。
6. 保留最后一条未完成帧，等待下次 `recv()` 补齐。

不要分别解码每个 TCP 数据块，因为一个中文 UTF-8 字符的多个字节可能分布在两次 `recv()` 中。

JSON 字符串内部的换行会编码为两个可见字节 `\` 和 `n`，不会成为 JSONL 帧结束符。设备发送的每条消息也必须在完整 JSON 后追加 `\n`。

推荐规则：

- 关闭 Nagle 或启用 `TCP_NODELAY`，但这不是协议必需条件。
- 单条消息解析失败时丢弃该行并记录错误，不要让解析器失去后续行边界。
- 未识别的字段必须忽略，以便兼容后续协议扩展。
- 未识别的 `type` 应忽略并继续保持连接。
- 固件应能处理至少 64 KiB 的单帧。`snapshot` 包含多个任务，实际实现最好采用流式解析，且只保留 LCD 真正需要的数据。

## 4. 建连和认证流程

### 4.1 标准时序

```text
Wi-Fi 设备                         PC Bridge
    |                                  |
    |---------- TCP connect ---------->|
    |<--------- server_hello -----------|
    |                                  |
    |  authentication_required=true    |
    |-------------- hello ------------>|
    |<------------- snapshot -----------|
    |                                  |
    |<----------- task_update -----------|
    |<---------- quota_update -----------|
    |<--------------- ping --------------|
    |--------------- pong ------------->|
    |                ...                |
```

如果 `authentication_required=false`，Bridge 会在 `server_hello` 后立即发送 `snapshot`，设备不必发送 `hello`。如果为 `true`，成功认证后的第一条业务消息就是 `snapshot`，没有单独的认证成功 ACK。

设备在等待认证或快照期间也必须能够处理并回复 `ping`。

### 4.2 `server_hello`：Bridge → 设备

TCP 建立后，Bridge 首先发送：

```json
{"type":"server_hello","protocol":1,"heartbeat_seconds":30,"authentication_required":true,"server_time_ms":1788700000000}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `type` | string | 固定为 `server_hello` |
| `protocol` | integer | 当前固定为 `1` |
| `heartbeat_seconds` | integer | Bridge 的心跳发送间隔 |
| `authentication_required` | boolean | 是否必须发送带 Token 的 `hello` |
| `server_time_ms` | integer | Bridge 当前 Unix 时间，单位毫秒 |

设备收到不支持的 `protocol` 时应断开连接并报告“协议版本不兼容”。

### 4.3 `hello`：设备 → Bridge

需要认证时发送：

```json
{"type":"hello","protocol":1,"device_id":"lcd-001","token":"与bridge.json一致的Token"}
```

| 字段 | 类型 | 要求 |
|---|---|---|
| `type` | string | 固定为 `hello` |
| `protocol` | integer | 发送 `1` |
| `device_id` | string | 建议使用稳定且唯一的设备 ID；当前 Bridge 仅作兼容字段 |
| `token` | string | 必须与 `dist\bridge.json` 中的 `auth_token` 完全一致 |

认证失败时 Bridge 发送：

```json
{"type":"error","code":"authentication_failed"}
```

随后 Bridge 关闭 TCP 连接。设备不应高频重试错误 Token，应进入配置错误状态或使用较长退避时间。

## 5. 消息方向汇总

| `type` | 方向 | 作用 |
|---|---|---|
| `server_hello` | Bridge → 设备 | 协议和心跳参数 |
| `hello` | 设备 → Bridge | Token 认证 |
| `snapshot` | Bridge → 设备 | 当前全部任务和额度快照 |
| `task_update` | Bridge → 设备 | 单个任务的完整当前状态 |
| `quota_update` | Bridge → 设备 | 单个来源的最新额度 |
| `ping` | 双向 | 探测连接；接收方回复 `pong` |
| `pong` | 双向 | 心跳响应 |
| `error` | Bridge → 设备 | 协议或认证错误 |

设备正常运行时只需发送 `hello`、`pong`，以及可选的主动 `ping`。不需要轮询任务或额度。

## 6. `snapshot` 完整快照

认证成功或无需认证时，Bridge 发送：

```json
{
  "type": "snapshot",
  "protocol": 1,
  "sequence": 25,
  "generated_at_ms": 1788700000000,
  "tasks": [
    {
      "source": "codex",
      "session_id": "01a123",
      "turn_id": "turn-456",
      "state": "thinking",
      "tool": null,
      "project": "ai assistant",
      "model": "gpt-5.6-sol",
      "think_summary": "正在检查设备协议",
      "content_summary": null,
      "plan_summary": null,
      "activity": "reasoning_started",
      "activity_summary": null,
      "error": null,
      "context_usage_percent": null,
      "context_tokens": null,
      "context_window_size": null,
      "input_tokens": null,
      "output_tokens": null,
      "cache_read_tokens": null,
      "cache_write_tokens": null,
      "last_event": "ThinkUpdate",
      "updated_at_ms": 1788699999000,
      "sequence": 24
    }
  ],
  "quotas": {
    "cursor": {
      "available": false,
      "display": "N/A",
      "reason": "personal_plan_no_public_api",
      "buckets": [],
      "updated_at_ms": 1788699900000
    }
  }
}
```

处理要求：

- 收到 `snapshot` 后，必须用它整体替换设备本地缓存，不要与断线前缓存继续合并。
- `tasks` 按更新时间从新到旧排列，但固件仍应读取 `updated_at_ms`，不要依赖数组顺序作为永久约定。
- `quotas` 是以来源名为键的对象，不是数组。
- Bridge 重启后任务缓存和 `sequence` 会重新开始。因此不得跨 TCP 重连永久保存并比较旧进程的 `sequence`；新连接的 `snapshot` 永远是权威基线。
- Bridge 进程运行期间会保留已知任务，包括 `completed` 和 `session_closed`。LCD 通常只展示最近的活动任务。

## 7. `task_update` 任务更新

```json
{
  "type": "task_update",
  "protocol": 1,
  "task": {
    "source": "cursor",
    "session_id": "42dd24bb-2394-4fe1-9e50-6c3445d9a3ec",
    "turn_id": "generation-001",
    "state": "using_tool",
    "tool": "Grep",
    "project": "ai assistant",
    "model": "grok-4.6",
    "think_summary": "正在定位相关代码",
    "content_summary": null,
    "plan_summary": null,
    "activity": "hook/preToolUse",
    "activity_summary": "正在调用 Grep",
    "error": null,
    "context_usage_percent": null,
    "context_tokens": null,
    "context_window_size": null,
    "input_tokens": null,
    "output_tokens": null,
    "cache_read_tokens": null,
    "cache_write_tokens": null,
    "last_event": "preToolUse",
    "updated_at_ms": 1788700001000,
    "sequence": 26
  }
}
```

`task_update.task` 是该任务在此刻的完整可转发视图，不是 JSON Merge Patch。设备应以 `source + ":" + session_id` 为任务键，使用新对象替换这个任务的旧缓存。

### 7.1 任务字段

| 字段 | 类型 | 含义 |
|---|---|---|
| `source` | string | 当前为 `codex` 或 `cursor` |
| `session_id` | string | 会话 ID；与 `source` 组合后唯一 |
| `turn_id` | string/null | 当前轮次 ID，同一会话发起新问题时可能变化 |
| `state` | string | 归一化任务状态，见下表 |
| `tool` | string/null | 当前工具名；离开 `using_tool` 后通常清空 |
| `project` | string/null | 项目或工作区名称 |
| `model` | string/null | 模型标识 |
| `think_summary` | string/null | 完整思考摘要块，不是隐藏思维链 |
| `content_summary` | string/null | 最新用户可见结果或最终回复摘要 |
| `plan_summary` | string/null | 正式计划事件摘要；很多任务可能始终为空 |
| `activity` | string/null | 机器可读活动标识，例如 `hook/preToolUse` |
| `activity_summary` | string/null | 短暂的用户可见操作文案，例如“正在读取文件” |
| `error` | string/null | 失败信息 |
| `context_usage_percent` | number/null | 当前对话上下文占用百分比，不是账号额度 |
| `context_tokens` | integer/null | 当前上下文 Token 数 |
| `context_window_size` | integer/null | 模型上下文窗口大小 |
| `input_tokens` | integer/null | 当前任务输入 Token 用量 |
| `output_tokens` | integer/null | 当前任务输出 Token 用量 |
| `cache_read_tokens` | integer/null | 当前任务缓存读取 Token 用量 |
| `cache_write_tokens` | integer/null | 当前任务缓存写入 Token 用量 |
| `last_event` | string/null | 最近一次来源事件名，主要用于诊断 |
| `updated_at_ms` | integer | Unix 时间，单位毫秒 |
| `sequence` | integer | Bridge 进程内单调递增的任务更新序号 |

Bridge 图形界面可分别关闭 THINK、SUMMARY、ACTION、额度转发。关闭后对应字段会从 JSON 中直接缺失，而不一定发送为 `null`：

| 界面选项 | 可能被移除的字段 |
|---|---|
| THINK | `think_summary` |
| SUMMARY | `content_summary`、`plan_summary` |
| ACTION | `tool`、`activity`、`activity_summary` |
| 额度 | 上下文及单次任务 Token 字段；同时影响额度消息 |

因此固件必须同时接受“字段不存在”和“字段值为 `null`”。主区与副区独立更新，互不影响：

- 主区只消费 `content_summary` 和 `think_summary`。本帧有非空 `content_summary` 时用它，否则用非空 `think_summary`。两者都缺失、为 `null` 或为空时，不因 `state=using_tool` 或其他状态清掉已显示的主区；若任务仍在推进且本地还没有主区文本，再显示“思考中”。
- `state=completed` 且本帧主区（`content_summary`/`think_summary`）和副区（`activity_summary`）都为空时，主区显示“等待新任务”，副区显示“上次任务已完成”；任一侧有内容则采用 Bridge 提供的数据。该兜底文案不是有效摘要，同一会话的下一轮活动任务不得继续保留。
- 副区只消费 `activity_summary`。出现非 `null` 字符串时更新副区；字段缺失或值为 `null` 时立即清空，不回退到 `tool`。

`activity_summary=null` 表示清除瞬时操作文案。PC 模拟器显示的 `ACTION: <cleared>` 是模拟器生成的提示，网络中不会发送字符串 `<cleared>`。

### 7.2 `state` 取值

| 值 | LCD 建议文案 | 含义 |
|---|---|---|
| `idle` | 空闲 | 会话已建立，暂无活动任务 |
| `thinking` | 思考中 | 模型正在处理 |
| `responding` | 回复中 | 正在生成或已得到用户可见消息 |
| `planning` | 规划中 | 正在生成或更新正式计划 |
| `using_tool` | 使用工具 | 正在调用工具；副区显示 `activity_summary`，主区保留已有思考或回复 |
| `waiting_approval` | 等待批准 | 等待用户在 PC 上批准操作 |
| `compacting` | 压缩上下文 | 正在压缩对话上下文 |
| `reviewing` | 审查中 | 正在执行代码审查 |
| `completed` | 已完成 | 当前轮次完成；主副区都空时显示“等待新任务”/“上次任务已完成”，否则用 Bridge 原文 |
| `failed` | 失败 | 当前轮次失败，显示 `error` |
| `interrupted` | 已中断 | 用户或系统中断任务 |
| `session_closed` | 会话关闭 | 会话结束，可从活动任务区域移除 |

### 7.3 更新顺序

- 同一 TCP 连接中的消息天然有序。
- 对同一任务，如果收到的 `task.sequence` 小于或等于已缓存序号，应忽略旧更新。
- `sequence` 是 Bridge 全局序号，可能因为其他任务或额度更新而跳号，不能要求连续加一。
- 断线重连后以新 `snapshot` 为准，不得用断线前序号拒绝新快照。
- 新的 `beforeSubmitPrompt`/`UserPromptSubmit` 会清空上一轮的摘要、操作和用量；设备以收到的新任务对象为准。

## 8. THINK、SUMMARY、PLAN 和 ACTION 的显示规则

- `think_summary` 采用完整块模式。Codex 默认在完整思考摘要生成后更新；Cursor 通常在一个 `afterAgentThought` 块完成后更新。设备不需要实现逐字流式拼接。
- `content_summary` 表示用户可见回复或任务结果，可以直接作为普通 UTF-8 文本显示。
- `plan_summary` 仅在模型产生正式计划事件时存在，不能假设每个任务都有计划；当前 LCD 主区不使用该字段。
- 主区选择顺序：非空 `content_summary`，否则非空 `think_summary`，否则在任务仍处于 `thinking`/`responding`/`planning`/`using_tool`/`waiting_approval`/`compacting`/`reviewing` 且本地尚无主区文本时显示“思考中”。本帧没有主区字段时，保留上一帧主区。
- `state=completed` 时，若本帧主区和副区都为空，主区改为“等待新任务”，副区改为“上次任务已完成”；否则采用 Bridge 提供的摘要和 ACTION。
- `activity_summary` 是瞬时操作提示，只驱动副区。出现时显示，变为 `null` 或字段缺失时立即隐藏。不使用 `tool` 填副区。
- 主区与副区互不影响：一次 `task_update` 可以只更新其中一侧。
- 文本可能包含 Markdown 符号和换行。资源有限的 LCD 可以按纯文本显示，不要求实现 Markdown 渲染。
- 截断文本时应按 UTF-8 字符边界或 Unicode 字符截断，不得直接截断到多字节字符中间。

## 9. `quota_update` 额度更新

Codex 额度在 Bridge 启动、任务结束以及周期轮询后可能更新。默认轮询间隔为 5 分钟。

```json
{
  "type": "quota_update",
  "protocol": 1,
  "source": "codex",
  "quota": {
    "buckets": [
      {
        "limit_id": "codex",
        "limit_name": null,
        "plan_type": "plus",
        "rate_limit_reached_type": null,
        "primary": {
          "used_percent": 35,
          "remaining_percent": 65,
          "window_duration_minutes": 300,
          "resets_at_unix": 1788718000
        },
        "secondary": null
      }
    ],
    "updated_at_ms": 1788700000000,
    "reset_credits_available": 1
  },
  "updated_at_ms": 1788700000000
}
```

处理规则：

- 一个账号可能有多个 `buckets`，不能只写死数组第一个元素。
- `primary` 和 `secondary` 均可能为 `null`。
- `remaining_percent` 已由 Bridge 计算并限制在 `0..100`。
- `window_duration_minutes=300` 表示 5 小时窗口。
- `resets_at_unix` 是 Unix 秒，不是毫秒。显示刷新时间时先按秒转换，再切换为设备本地时区。
- `updated_at_ms` 是 Unix 毫秒。
- `reset_credits_available` 和 bucket 的 `credits` 字段都是可选字段。
- GUI 未勾选该来源的“额度”时，`quota_update` 不会发送，`snapshot.quotas` 中也不会包含该来源。
- 没收到某来源额度不能解释为剩余 0%。

Cursor 个人套餐没有公开的实时剩余额度接口。如果启用了 Cursor 额度转发，Bridge 使用以下对象明确表示不可查询：

```json
{
  "available": false,
  "display": "N/A",
  "reason": "personal_plan_no_public_api",
  "buckets": [],
  "updated_at_ms": 1788700000000
}
```

Cursor 个人套餐没有账号剩余额度。LCD 右侧不再显示额度 `N/A` 或 `0%`，改为 `TOKEN`：主数值为 `input_tokens + output_tokens`，显示为 `131k` 这类 k 单位，不再单独显示 `OUT`。进度条用最近一次 `preCompact` 的 `context_tokens` 和 `context_window_size`：`min(100, context_tokens * 100 / context_window_size)`；还没有成对数据则条子清空。TOKEN 数字是单次任务用量，进度条是对话上下文占用，都不能当成账号剩余额度。

## 10. 心跳

Bridge 到设备：

```json
{"type":"ping","timestamp_ms":1788700030000}
```

设备应尽快回复：

```json
{"type":"pong","timestamp_ms":1788700030100}
```

`timestamp_ms` 使用 Unix 毫秒。Bridge 当前只用收到消息的时间刷新连接活跃状态，不要求设备回显相同时间戳。

设备也可以主动发送：

```json
{"type":"ping","timestamp_ms":1788700030000}
```

Bridge 会立即返回 `pong`。

实现要求：

- 每次收到 Bridge 的 `ping` 都回复 `pong`。
- 不要用 TCP Keepalive 代替应用层 `pong`。
- 设备的网络任务不能因 LCD 刷新而阻塞超过心跳超时时间。
- 收到 EOF、连接复位或写入失败后立即进入断线重连状态。

## 11. 设备端推荐状态机

```text
DISCONNECTED
    |
    | TCP 连接成功
    v
WAIT_SERVER_HELLO
    |
    | server_hello，protocol=1
    v
WAIT_SNAPSHOT
    | 需要认证：发送 hello
    | 任意阶段收到 ping：回复 pong
    |
    | snapshot
    v
RUNNING
    | 处理 task_update / quota_update / ping
    |
    | EOF、超时、Socket 错误
    v
RECONNECT_BACKOFF
    |
    +---- 1s、2s、5s、10s、最大 30s，并加入少量随机抖动 ----> DISCONNECTED
```

认证失败应与普通断网区分。普通断网自动退避重连；`authentication_failed` 应等待用户修正 Token，避免持续撞库式重试。

## 12. 固件处理伪代码

```c
connect(pc_ipv4, 7878);

while (socket_connected()) {
    bytes = socket_recv();
    rx_buffer.append(bytes);

    while (rx_buffer.contains(0x0A)) {
        frame = rx_buffer.take_until_lf();
        frame.remove_optional_trailing_cr();
        message = json_parse_utf8(frame);
        if (!message.valid) {
            continue;
        }

        switch (message.type) {
        case SERVER_HELLO:
            if (message.protocol != 1) {
                disconnect_with_protocol_error();
            } else if (message.authentication_required) {
                send_json_line(make_hello(device_id, auth_token));
            }
            break;

        case SNAPSHOT:
            replace_all_local_state(message);
            break;

        case TASK_UPDATE:
            key = message.task.source + ":" + message.task.session_id;
            if (is_new_sequence(key, message.task.sequence)) {
                replace_task(key, message.task);
                refresh_lcd();
            }
            break;

        case QUOTA_UPDATE:
            replace_quota(message.source, message.quota);
            refresh_lcd();
            break;

        case PING:
            send_json_line(make_pong(now_unix_ms()));
            break;

        case ERROR:
            handle_bridge_error(message.code);
            break;

        default:
            break;
        }
    }
}

schedule_reconnect_with_backoff();
```

## 13. LCD 任务选择建议

如果同时存在多个任务，建议按以下优先级选择主显示任务：

1. 排除 `session_closed`。
2. 优先显示 `thinking`、`responding`、`planning`、`using_tool`、`waiting_approval`、`compacting`、`reviewing`。
3. 活动状态相同时，选择 `updated_at_ms` 最大的任务。
4. 没有活动任务时，显示最近的 `completed`、`failed` 或 `interrupted`。
5. 使用 `source` 显示 Codex/Cursor 标识。

推荐界面逻辑：

- 主区：有 `content_summary` 用它，否则用 `think_summary`；都空且任务还在跑、本地也没有旧主区时显示“思考中”。不要因为 `state=using_tool` 清主区。
- `completed`：主副区都空时用“等待新任务”/“上次任务已完成”，否则用 Bridge 数据。
- 副区：只显示 `activity_summary`；`null` 或字段缺失时立即清空。
- `waiting_approval`：高亮提示用户回到 PC 批准。
- `failed`：状态区显示失败，主区仍按上述规则保留或更新摘要。
- `activity_summary` 清空后立即移除 ACTION 行。

## 14. 接入验收清单

- [ ] Bridge 使用 `dist\bridge.json` 启动，监听地址为 `0.0.0.0:7878`。
- [ ] Windows 网络配置为“专用网络”，防火墙已允许 Bridge TCP 入站。
- [ ] 设备连接的是 PC 局域网 IPv4，而不是 `0.0.0.0` 或 `127.0.0.1`。
- [ ] 设备能解析 `server_hello` 并检查 `protocol=1`。
- [ ] 开启认证时，设备发送正确 Token，并在认证后收到 `snapshot`。
- [ ] 接收缓存正确处理 TCP 分包、粘包和半个 UTF-8 中文字符。
- [ ] 收到 `ping` 后能稳定回复 `pong`，连接持续超过 10 分钟不断线。
- [ ] Cursor/Codex 提问时能收到 `task_update`，并按 `source + session_id` 更新任务。
- [ ] `activity_summary=null` 或字段缺失时会清除 ACTION 文案。
- [ ] THINK/SUMMARY 中文显示正常，不把 UTF-8 当作 GBK。
- [ ] 设备能显示 Codex 5 小时窗口的剩余百分比和本地刷新时间。
- [ ] Cursor `available=false` 时显示 N/A，而不是 0%。
- [ ] 断开 Wi-Fi 后能自动退避重连，并用新 `snapshot` 替换旧缓存。
- [ ] Bridge 重启后设备不会因旧 `sequence` 拒绝新快照。

## 15. 当前协议限制

- 协议 v1 没有 TLS、消息压缩和设备端主动查询接口。
- Bridge 不保存持久任务历史；Bridge 重启后状态从新事件重新建立。
- 设备不能通过此协议修改 Bridge 的复选框或启动/停止状态。
- Cursor 个人套餐的账号剩余额度不可查询，只能获得单次任务 Token 用量和 N/A 标记。
- `device_id` 当前未用于设备管理或访问控制，真正的访问控制依赖 `auth_token`。

