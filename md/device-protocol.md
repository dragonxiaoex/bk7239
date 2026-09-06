# Wi-Fi 设备 TCP 协议 v1

## 传输

- Wi-Fi 设备主动连接 PC Bridge。
- 默认端口：`7878`。
- 编码：UTF-8，无 BOM。
- 帧格式：JSON Lines，一行一个完整 JSON 对象，以 `\n` 结束。
- Bridge 每 30 秒发送一次 `ping`；设备应回复 `pong`。超过配置的客户端超时时间没有收到任何消息，Bridge 会断开连接。
- 设备断线重连后，Bridge 会重新发送完整 `snapshot`，所以设备不需要补拉丢失增量。

## 建连

Bridge 首先发送：

```json
{"type":"server_hello","protocol":1,"heartbeat_seconds":30,"authentication_required":false,"server_time_ms":1787934000000}
```

如果 `authentication_required` 为 `true`，设备必须发送：

```json
{"type":"hello","device_id":"lcd-001","token":"配置中的 auth_token","protocol":1}
```

认证成功或未启用认证时，Bridge 发送完整快照：

```json
{
  "type": "snapshot",
  "protocol": 1,
  "sequence": 8,
  "generated_at_ms": 1787934000000,
  "tasks": [],
  "quotas": {}
}
```

## 任务更新

```json
{
  "type": "task_update",
  "protocol": 1,
  "task": {
    "source": "codex",
    "session_id": "01a...",
    "turn_id": "01b...",
    "state": "using_tool",
    "tool": "Shell",
    "project": "ai assistant",
    "model": "gpt-5.6-sol",
    "think_summary": "正在检查 Bridge 的 TCP 状态……",
    "content_summary": "正在整理用户可见的结果……",
    "plan_summary": "[inProgress] 验证事件流",
    "activity": "item/commandExecution/started",
    "activity_summary": "正在运行命令",
    "error": null,
    "context_usage_percent": null,
    "context_tokens": null,
    "context_window_size": null,
    "input_tokens": null,
    "output_tokens": null,
    "cache_read_tokens": null,
    "cache_write_tokens": null,
    "last_event": "ContentUpdate",
    "updated_at_ms": 1787934000000,
    "sequence": 9
  }
}
```

`state` 当前可能值：

| 值 | 含义 |
|---|---|
| `idle` | 会话已建立，暂无任务 |
| `thinking` | 模型正在处理 |
| `responding` | 正在生成用户可见消息，读取 `content_summary` |
| `planning` | 正在生成或更新计划，读取 `plan_summary` |
| `using_tool` | 正在调用工具，读取 `tool` |
| `waiting_approval` | 等待用户批准 |
| `compacting` | 正在压缩上下文 |
| `reviewing` | 正在执行代码审查 |
| `completed` | 当前任务停止 |
| `failed` | 当前任务失败，读取 `error` |
| `interrupted` | 当前任务被中断 |
| `session_closed` | 会话结束 |

设备应按每个任务的 `sequence` 忽略旧更新。

`source` 当前可为 `codex` 或 `cursor`。Cursor 的 `preCompact` 事件会填充 `context_usage_percent`、`context_tokens` 和 `context_window_size`；这些字段是对话上下文占用，不是账户计费额度。Cursor 的 `afterAgentResponse` / `stop` 会填充 `input_tokens`、`output_tokens`、`cache_read_tokens` 和 `cache_write_tokens`，表示单次任务用量。

## 额度更新

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
          "resets_at_unix": 1787946567
        },
        "secondary": null
      }
    ],
    "updated_at_ms": 1787934000000,
    "reset_credits_available": 1
  },
  "updated_at_ms": 1787934000000
}
```

一个账号可能返回多个额度 bucket。固件不应假定永远只有一个窗口。

Cursor 个人套餐没有官方公开的实时剩余额度 API。Bridge 会在快照的 `quotas.cursor` 中发送：

```json
{
  "available": false,
  "display": "N/A",
  "reason": "personal_plan_no_public_api",
  "buckets": []
}
```

固件应显示为“Cursor 余额 N/A”，不能显示成 0%。`source=codex` 仍使用正常的账户额度 bucket。

## 心跳

Bridge 到设备：

```json
{"type":"ping","timestamp_ms":1787934000000}
```

设备到 Bridge：

```json
{"type":"pong","timestamp_ms":1787934000000}
```

设备也可以主动发送 `ping`，Bridge 会立即回复 `pong`。

## LCD 侧建议

- 以 `source + session_id` 作为任务键。
- 默认展示 `updated_at_ms` 最新的未关闭任务。
- `using_tool` 时显示工具名称；其他状态隐藏旧工具名称。
- `think_summary` 是模型通过 App Server 提供的可读推理摘要；模型可能只提供一个阶段标题，Bridge 不会尝试解密或伪造隐藏思维链。
- 默认完整块模式下，Bridge 忽略 reasoning delta；`think_summary` 只在 `item/completed` 或任务结束后的最终读取时更新。固件可以把每次更新当作一个完整文本块处理。
- `content_summary` 是最新的用户可见 agent message（commentary 或 final answer）的截断文本。
- `plan_summary` 只表示 App Server 的正式 `turn/plan/updated` 或 `plan` item；普通模式可能始终为空。
- `activity` 保留归一化的 App Server 活动，例如 `reasoning_started`、`item/webSearch/started`、`turn_completed`。
- `activity_summary` 是瞬时的用户可见活动文案：优先使用 agent commentary；Cursor 代理根据 App Server 工具事件生成，Codex Hook relay 根据 `PreToolUse.tool_name` 和 `tool_input` 就地生成。例如读取技能定义时显示“正在读取 OpenAI Docs 技能”。操作完成时字段恢复为 `null`，LCD 应隐藏活动行。relay 不会把完整 `tool_input` 转发给 Bridge。
- Cursor 原生 `think_summary` 来自 `afterAgentThought` 的完整 thinking block，通常按块更新而不是逐字流式更新；`content_summary` 来自 `afterAgentResponse`。
- `context_usage_percent` 非空时可单独显示“上下文占用”，不要放进账户“剩余额度”栏。
- 所有文本按 UTF-8 接收，显示侧自行做滚动、换行和截断。
- 收到 `snapshot` 时用其整体替换本地状态；收到增量时按 `sequence` 合并。
