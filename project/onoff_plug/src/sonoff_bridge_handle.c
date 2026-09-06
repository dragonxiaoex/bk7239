/**
 * @file    sonoff_bridge_handle.c
 * @brief   Bridge通信处理与JSONL流式解析
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-06
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include "sonoff_bridge_handle.h"
#include "sonoff_log.h"
#include "sonoff_task_def.h"
#include "sonoff_private_item.h"
#include "sonoff_nvdm.h"
#include "sonoff_net.h"
#include "sonoff_ui_handle.h"

static const char *tag = "SNF-BRIDGE";

#define SNF_BRIDGE_HANDLE_TASK_NAME         "snf_bridge"
#define SNF_BRIDGE_HANDLE_TASK_STACKSIZE    (2 * 1024)
#define SNF_BRIDGE_HANDLE_TASK_PRIO         (TASK_PRIORITY_LOW)
#define SNF_BRIDGE_PORT                     (7878)
#define SNF_BRIDGE_RETRY_MS                 (1000)
#define SNF_BRIDGE_CONFIG_RETRY_MS          (30000)
#define SNF_BRIDGE_CONNECT_MS               (5000)
#define SNF_BRIDGE_HANDSHAKE_MS             (30000)
#define SNF_BRIDGE_IDLE_MS                  (75000)
#define SNF_BRIDGE_FRAME_MAX                (256 * 1024)
#define SNF_BRIDGE_JSON_DEPTH               (16)
#define SNF_BRIDGE_TOKEN_CAPACITY           (128)
#define SNF_BRIDGE_TOKEN_NONE               UINT64_MAX
#define SNF_BRIDGE_NUMBER_MAX               (9007199254740991.0)

static const char *const bridge_text_thinking = "思考中";
static const char *const bridge_text_wait_task = "等待新任务";
static const char *const bridge_text_last_done = "上次任务已完成";

/** @brief TCP会话阶段. */
typedef enum
{
    BRIDGE_DISCONNECTED = 0,
    BRIDGE_WAIT_HELLO,
    BRIDGE_WAIT_SNAPSHOT,
    BRIDGE_RUNNING,
} SnfBridgePhase;

/** @brief JSON容器中的语法位置. */
typedef enum
{
    BRIDGE_KEY_OR_END = 0,
    BRIDGE_KEY,
    BRIDGE_COLON,
    BRIDGE_VALUE,
    BRIDGE_VALUE_OR_END,
    BRIDGE_COMMA_OR_END,
} SnfBridgeJsonState;

/** @brief 仅识别协议需要的容器路径, 其他路径只做语法校验. */
typedef enum
{
    BRIDGE_GENERIC = 0,
    BRIDGE_ROOT,
    BRIDGE_TASKS,
    BRIDGE_TASK,
    BRIDGE_QUOTAS,
    BRIDGE_QUOTA,
    BRIDGE_BUCKETS,
    BRIDGE_BUCKET,
    BRIDGE_WINDOW,
} SnfBridgeJsonRole;

/** @brief 单个JSON容器的流式解析状态. */
typedef struct
{
    SnfBridgeJsonState state;
    SnfBridgeJsonRole role;
    bool object;
    char key[40];
    uint8_t quota_index;
} SnfBridgeJsonFrame;

/** @brief 主区文本来源, 决定同会话更新时要不要保留旧主区. */
typedef enum
{
    BRIDGE_PRIMARY_NONE = 0,    /* 本帧没有think/content */
    BRIDGE_PRIMARY_BRIDGE,      /* 来自think或content */
    BRIDGE_PRIMARY_FALLBACK,    /* completed空数据的本地提示 */
} SnfBridgePrimaryOrigin;

/** @brief 每个来源仅缓存一个当前会话及其序号. */
typedef struct
{
    char session[SNF_BRIDGE_TOKEN_CAPACITY];
    uint8_t source;
    uint8_t priority;
    uint64_t sequence;
    uint64_t updated_ms;
    uint64_t input_tokens;
    uint64_t output_tokens;
    uint64_t context_tokens;
    uint64_t window_tokens;
    SnfBridgePrimaryOrigin primary_origin;
    SnfUiAgentData ui;
} SnfBridgeTask;

/** @brief 当前解析任务的暂存字段, 不缓存完整摘要. */
typedef struct
{
    SnfBridgeTask value;
    char state[32];
    char think[SNF_UI_PRIMARY_TEXT_CAPACITY];
    char content[SNF_UI_PRIMARY_TEXT_CAPACITY];
    char plan[SNF_UI_PRIMARY_TEXT_CAPACITY];
    char error[SNF_UI_PRIMARY_TEXT_CAPACITY];
    char activity[SNF_UI_SECONDARY_TEXT_CAPACITY];
    char tool[SNF_UI_SECONDARY_TEXT_CAPACITY];
    bool sequence_valid;
    bool updated_valid;
} SnfBridgeTaskInput;

/** @brief 额度窗口的显示候选. */
typedef struct
{
    uint8_t percent;
    uint32_t minutes;
    bool codex_bucket;
    bool valid;
} SnfBridgeQuota;

/** @brief 单行消息的事务及词法状态, 与TCP接收块边界无关. */
typedef struct
{
    SnfBridgeJsonFrame frames[SNF_BRIDGE_JSON_DEPTH];
    uint32_t depth;
    uint32_t bytes;
    bool complete;
    bool invalid;
    bool in_string;
    bool in_scalar;
    bool token_clipped;
    uint8_t escape;
    uint8_t hex_left;
    uint8_t utf8_left;
    uint32_t unicode;
    uint32_t high_surrogate;
    uint32_t utf8_min;
    char token[SNF_BRIDGE_TOKEN_CAPACITY];
    uint32_t token_length;
    char type[32];
    char error_code[48];
    uint8_t source;
    bool protocol_valid;
    bool protocol_present;
    bool auth_required;
    bool auth_present;
    bool tasks_present;
    bool quotas_present;
    bool direct_quota_present;
    uint64_t timestamp;
    uint32_t heartbeat_seconds;
    SnfBridgeTaskInput task;
    SnfBridgeTask tasks[2];
    bool task_present[2];
    SnfBridgeQuota quotas[3];
    SnfBridgeQuota window;
    SnfBridgeQuota bucket;
    bool codex_bucket;
} SnfBridgeParser;

/** @brief 由Bridge任务独占的通信与显示状态. */
typedef struct
{
    int32_t socket_fd;
    bool started;
    SnfBridgePhase phase;
    TickType_t last_message;
    TickType_t phase_start;
    uint32_t idle_ms;
    uint32_t retry_ms;
    SnfBridgeTask tasks[2];
    bool task_present[2];
    SnfBridgeQuota quota;
    uint64_t cursor_context_tokens;
    uint64_t cursor_window_tokens;
    bool ui_dirty;
    SnfBridgeParser parser;
} SnfBridgeConfig;

static SnfBridgeConfig bridge_config = {
    .socket_fd = -1,
    .phase = BRIDGE_DISCONNECTED,
    .idle_ms = SNF_BRIDGE_IDLE_MS,
    .retry_ms = SNF_BRIDGE_RETRY_MS,
    .cursor_context_tokens = SNF_BRIDGE_TOKEN_NONE,
    .cursor_window_tokens = SNF_BRIDGE_TOKEN_NONE,
};

/** @brief 丢弃当前帧事务, 不改变已经显示的快照. */
static void bridgeResetParser(void)
{
    SnfBridgeParser *parser = &bridge_config.parser;

    memset(parser, 0, sizeof(*parser));
    parser->source = UINT8_MAX;
}

/** @brief 按完整UTF-8字符复制文本, 将换行及控制字符转换为空格. */
static void bridgeCopyText(char *output, size_t capacity, const char *input)
{
    size_t used = 0;
    size_t index = 0;

    while (input[index] != '\0')
    {
        uint8_t first = (uint8_t)input[index];
        size_t length = 1;

        if (first >= 0xF0)
        {
            length = 4;
        }
        else if (first >= 0xE0)
        {
            length = 3;
        }
        else if (first >= 0xC0)
        {
            length = 2;
        }
        else
        {
            length = 1;
        }
        if ((used + length) >= capacity)
        {
            break;
        }
        if ((first < 0x20) || (first == 0x7F))
        {
            output[used] = ' ';
        }
        else
        {
            memcpy(&output[used], &input[index], length);
        }
        used += length;
        index += length;
    }
    output[used] = '\0';
}

/** @brief 将协议来源转换为行索引, 未知来源忽略. */
static uint8_t bridgeSource(const char *source)
{
    if (strcmp(source, "codex") == 0)
    {
        return 0;
    }
    if (strcmp(source, "cursor") == 0)
    {
        return 1;
    }

    return UINT8_MAX;
}

/** @brief 比较窗口优先级: codex桶、5小时窗口、较短窗口. */
static bool bridgeQuotaBetter(const SnfBridgeQuota *candidate, const SnfBridgeQuota *current)
{
    if (!candidate->valid)
    {
        return false;
    }
    if (!current->valid)
    {
        return true;
    }
    if (candidate->codex_bucket != current->codex_bucket)
    {
        return candidate->codex_bucket;
    }
    if ((candidate->minutes == 300) != (current->minutes == 300))
    {
        return (candidate->minutes == 300);
    }
    if (candidate->minutes == 0)
    {
        return false;
    }

    return ((current->minutes == 0) || (candidate->minutes < current->minutes));
}

/** @brief 快照候选按活动优先、更新时间及序号选择. */
static bool bridgeTaskBetter(const SnfBridgeTask *candidate, const SnfBridgeTask *current)
{
    if (strcmp(candidate->session, current->session) == 0)
    {
        return (candidate->sequence > current->sequence);
    }

    return (candidate->priority > current->priority)
           || ((candidate->priority == current->priority) && (candidate->updated_ms > current->updated_ms))
           || ((candidate->priority == current->priority) && (candidate->updated_ms == current->updated_ms)
               && (candidate->sequence > current->sequence));
}

/** @brief 活动任务仍在推进, 可用“思考中”作为主区兜底. */
static bool bridgeTaskRunning(const char *state)
{
    return (strcmp(state, "thinking") == 0)
           || (strcmp(state, "responding") == 0)
           || (strcmp(state, "planning") == 0)
           || (strcmp(state, "using_tool") == 0)
           || (strcmp(state, "waiting_approval") == 0)
           || (strcmp(state, "compacting") == 0)
           || (strcmp(state, "reviewing") == 0);
}

/** @brief 打印本帧已解析的任务字段, 缺失或null打印为空串. */
static void bridgeLogTaskFields(const SnfBridgeTaskInput *input)
{
    const char *source = "unknown";

    if (input->value.source == 0)
    {
        source = "codex";
    }
    else if (input->value.source == 1)
    {
        source = "cursor";
    }
    else
    {
        /* 未知来源仍打印字段, 便于对照原始JSON. */
    }
    LOG_I(tag, "source:%s", source);
    LOG_I(tag, "session:%s", input->value.session);
    LOG_I(tag, "state:%s", input->state);
    LOG_I(tag, "think:%s", input->think);
    LOG_I(tag, "content:%s", input->content);
    LOG_I(tag, "plan:%s", input->plan);
    LOG_I(tag, "activity:%s", input->activity);
    LOG_I(tag, "tool:%s", input->tool);
    LOG_I(tag, "error:%s", input->error);
    if (input->value.input_tokens == SNF_BRIDGE_TOKEN_NONE)
    {
        LOG_I(tag, "input:");
    }
    else
    {
        LOG_I(tag, "input:%llu", (unsigned long long)input->value.input_tokens);
    }
    if (input->value.output_tokens == SNF_BRIDGE_TOKEN_NONE)
    {
        LOG_I(tag, "output:");
    }
    else
    {
        LOG_I(tag, "output:%llu", (unsigned long long)input->value.output_tokens);
    }
    if (input->value.context_tokens == SNF_BRIDGE_TOKEN_NONE)
    {
        LOG_I(tag, "context:");
    }
    else
    {
        LOG_I(tag, "context:%llu", (unsigned long long)input->value.context_tokens);
    }
    if (input->value.window_tokens == SNF_BRIDGE_TOKEN_NONE)
    {
        LOG_I(tag, "window:");
    }
    else
    {
        LOG_I(tag, "window:%llu", (unsigned long long)input->value.window_tokens);
    }
}

/** @brief 把协议state映射为LCD状态和任务优先级. */
static void bridgeFillTaskState(SnfBridgeTask *value, const char *state)
{
    value->priority = 3;
    value->ui.state = SNF_UI_AI_RUNNING;
    if ((strcmp(state, "completed") == 0) || (strcmp(state, "interrupted") == 0))
    {
        value->priority = 2;
        value->ui.state = SNF_UI_AI_IDLE;
    }
    else if (strcmp(state, "failed") == 0)
    {
        value->priority = 2;
        value->ui.state = SNF_UI_AI_ERROR;
    }
    else if ((strcmp(state, "idle") == 0) || (strcmp(state, "session_closed") == 0))
    {
        value->priority = (strcmp(state, "idle") == 0) ? 1 : 0;
        value->ui.state = SNF_UI_AI_IDLE;
    }
    else if (strcmp(state, "waiting_approval") == 0)
    {
        value->ui.state = SNF_UI_AI_WAITING;
    }
    else if (bridgeTaskRunning(state))
    {
        /* 活动态保持运行中. */
    }
    else
    {
        value->priority = 1;
        value->ui.state = SNF_UI_AI_OFFLINE;
    }
}

/**
 * @brief 按本帧字段选择主副区文案.
 *
 * 主区：content，否则 think。都没有时，活动任务显示“思考中”；
 * completed 且副区也空则显示“等待新任务”。
 * 副区：有 activity 就显示，否则清空；completed 兜底时为“上次任务已完成”。
 */
static void bridgeFillTaskText(SnfBridgeTaskInput *input)
{
    SnfBridgeTask *value = &input->value;
    const char *state = input->state;
    const char *primary = "";
    const char *secondary = input->activity;
    bool completed = (strcmp(state, "completed") == 0);

    if (input->content[0] != '\0')
    {
        primary = input->content;
        value->primary_origin = BRIDGE_PRIMARY_BRIDGE;
    }
    else if (input->think[0] != '\0')
    {
        primary = input->think;
        value->primary_origin = BRIDGE_PRIMARY_BRIDGE;
    }
    else if (completed && (secondary[0] == '\0'))
    {
        primary = bridge_text_wait_task;
        secondary = bridge_text_last_done;
        value->primary_origin = BRIDGE_PRIMARY_FALLBACK;
    }
    else if (bridgeTaskRunning(state))
    {
        primary = bridge_text_thinking;
        value->primary_origin = BRIDGE_PRIMARY_NONE;
    }
    else
    {
        value->primary_origin = BRIDGE_PRIMARY_NONE;
    }
    bridgeCopyText(value->ui.primary_text, sizeof(value->ui.primary_text), primary);
    bridgeCopyText(value->ui.secondary_text, sizeof(value->ui.secondary_text), secondary);
}

/** @brief 完成一个任务对象的显示映射并加入帧事务. */
static void bridgeFinishTask(void)
{
    SnfBridgeParser *parser = &bridge_config.parser;
    SnfBridgeTaskInput *input = &parser->task;
    SnfBridgeTask *value = &input->value;

    bridgeLogTaskFields(input);
    if (value->source > 1)
    {
        return;
    }
    if ((value->session[0] == '\0') || (input->state[0] == '\0')
        || !input->sequence_valid || !input->updated_valid)
    {
        parser->invalid = true;
        return;
    }

    bridgeFillTaskState(value, input->state);
    bridgeFillTaskText(input);
    if (!parser->task_present[value->source]
        || bridgeTaskBetter(value, &parser->tasks[value->source]))
    {
        parser->tasks[value->source] = *value;
        parser->task_present[value->source] = true;
    }
}

/** @brief JSON字符串解码后按Unicode字符追加, 超长文本只保留完整前缀. */
static void bridgeAppendCodepoint(uint32_t codepoint)
{
    SnfBridgeParser *parser = &bridge_config.parser;
    uint8_t bytes[4];
    uint32_t count;

    if ((codepoint == 0) || (codepoint > 0x10FFFF)
        || ((codepoint >= 0xD800) && (codepoint <= 0xDFFF)))
    {
        parser->invalid = true;
        return;
    }
    if (codepoint < 0x80)
    {
        bytes[0] = (uint8_t)codepoint;
        count = 1;
    }
    else if (codepoint < 0x800)
    {
        bytes[0] = (uint8_t)(0xC0 | (codepoint >> 6));
        bytes[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
        count = 2;
    }
    else if (codepoint < 0x10000)
    {
        bytes[0] = (uint8_t)(0xE0 | (codepoint >> 12));
        bytes[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
        count = 3;
    }
    else
    {
        bytes[0] = (uint8_t)(0xF0 | (codepoint >> 18));
        bytes[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
        count = 4;
    }
    if (parser->token_clipped || ((parser->token_length + count) >= sizeof(parser->token)))
    {
        parser->token_clipped = true;
        return;
    }
    memcpy(&parser->token[parser->token_length], bytes, count);
    parser->token_length += count;
    parser->token[parser->token_length] = '\0';
}

/** @brief 严格验证JSON数字语法, 避免接受NaN、前导零或不完整指数. */
static bool bridgeNumber(const char *text, double *value)
{
    size_t index = 0;
    size_t start;
    char *end;

    if (text[index] == '-')
    {
        index++;
    }
    if (text[index] == '0')
    {
        index++;
    }
    else
    {
        start = index;
        while (((uint8_t)text[index] >= '0') && ((uint8_t)text[index] <= '9'))
        {
            index++;
        }
        if (index == start)
        {
            return false;
        }
    }
    if (text[index] == '.')
    {
        index++;
        start = index;
        while (((uint8_t)text[index] >= '0') && ((uint8_t)text[index] <= '9'))
        {
            index++;
        }
        if (index == start)
        {
            return false;
        }
    }
    if ((text[index] == 'e') || (text[index] == 'E'))
    {
        index++;
        if ((text[index] == '+') || (text[index] == '-'))
        {
            index++;
        }
        start = index;
        while (((uint8_t)text[index] >= '0') && ((uint8_t)text[index] <= '9'))
        {
            index++;
        }
        if (index == start)
        {
            return false;
        }
    }
    if (text[index] != '\0')
    {
        return false;
    }
    *value = strtod(text, &end);

    return (*end == '\0');
}

/** @brief 读取可精确表示的非负协议整数. */
static bool bridgeInteger(double value)
{
    if (!(value >= 0) || !(value <= SNF_BRIDGE_NUMBER_MAX))
    {
        return false;
    }

    return (value == (double)(uint64_t)value);
}

/** @brief 分发完整标量, 缺失或null字段由对象初始化自然清除. */
static void bridgeScalar(bool string_value)
{
    SnfBridgeParser *parser = &bridge_config.parser;
    SnfBridgeJsonFrame *frame = &parser->frames[parser->depth - 1];
    SnfBridgeTaskInput *task = &parser->task;
    const char *key = frame->key;
    const char *text = parser->token;
    double number = 0;
    bool numeric = false;
    bool integer = false;

    if (string_value && ((frame->state == BRIDGE_KEY) || (frame->state == BRIDGE_KEY_OR_END)))
    {
        if (parser->token_clipped || (parser->token_length >= sizeof(frame->key)))
        {
            frame->key[0] = '\0';
        }
        else
        {
            memcpy(frame->key, text, parser->token_length + 1);
        }
        frame->state = BRIDGE_COLON;
        return;
    }
    if (!string_value)
    {
        numeric = bridgeNumber(text, &number);
        if (parser->token_clipped || (!numeric && (strcmp(text, "null") != 0)
            && (strcmp(text, "true") != 0) && (strcmp(text, "false") != 0)))
        {
            parser->invalid = true;
            return;
        }
        integer = numeric && bridgeInteger(number);
    }
    frame->state = BRIDGE_COMMA_OR_END;
    if (frame->role == BRIDGE_ROOT)
    {
        if (strcmp(key, "type") == 0)
        {
            if (string_value && !parser->token_clipped)
            {
                bridgeCopyText(parser->type, sizeof(parser->type), text);
            }
        }
        else if (strcmp(key, "source") == 0)
        {
            parser->source = string_value ? bridgeSource(text) : UINT8_MAX;
        }
        else if (strcmp(key, "protocol") == 0)
        {
            parser->protocol_present = true;
            parser->protocol_valid = integer && (number == 1);
        }
        else if (strcmp(key, "authentication_required") == 0)
        {
            parser->auth_present = !string_value && ((strcmp(text, "true") == 0)
                                                    || (strcmp(text, "false") == 0));
            parser->auth_required = !string_value && (strcmp(text, "true") == 0);
        }
        else if ((strcmp(key, "heartbeat_seconds") == 0) && integer && (number > 0) && (number <= 86400))
        {
            parser->heartbeat_seconds = (uint32_t)number;
        }
        else if ((strcmp(key, "timestamp_ms") == 0) && integer)
        {
            parser->timestamp = (uint64_t)number;
        }
        else if ((strcmp(key, "code") == 0) && string_value)
        {
            bridgeCopyText(parser->error_code, sizeof(parser->error_code), text);
        }
        else
        {
            /* 未使用的根字段不缓存. */
        }
    }
    else if (frame->role == BRIDGE_TASK)
    {
        if ((strcmp(key, "source") == 0) && string_value)
        {
            task->value.source = bridgeSource(text);
        }
        else if ((strcmp(key, "session_id") == 0) && string_value)
        {
            if (parser->token_clipped)
            {
                parser->invalid = true;
            }
            else
            {
                memcpy(task->value.session, text, parser->token_length + 1);
            }
        }
        else if ((strcmp(key, "sequence") == 0) && integer)
        {
            task->sequence_valid = true;
            task->value.sequence = (uint64_t)number;
        }
        else if ((strcmp(key, "updated_at_ms") == 0) && integer)
        {
            task->updated_valid = true;
            task->value.updated_ms = (uint64_t)number;
        }
        else if ((strcmp(key, "input_tokens") == 0) && integer)
        {
            task->value.input_tokens = (uint64_t)number;
        }
        else if ((strcmp(key, "output_tokens") == 0) && integer)
        {
            task->value.output_tokens = (uint64_t)number;
        }
        else if ((strcmp(key, "context_tokens") == 0) && integer)
        {
            task->value.context_tokens = (uint64_t)number;
        }
        else if ((strcmp(key, "context_window_size") == 0) && integer && (number > 0))
        {
            task->value.window_tokens = (uint64_t)number;
        }
        else if (string_value)
        {
            if (strcmp(key, "state") == 0)
            {
                bridgeCopyText(task->state, sizeof(task->state), text);
            }
            else if (strcmp(key, "think_summary") == 0)
            {
                bridgeCopyText(task->think, sizeof(task->think), text);
            }
            else if (strcmp(key, "content_summary") == 0)
            {
                bridgeCopyText(task->content, sizeof(task->content), text);
            }
            else if (strcmp(key, "plan_summary") == 0)
            {
                bridgeCopyText(task->plan, sizeof(task->plan), text);
            }
            else if (strcmp(key, "error") == 0)
            {
                bridgeCopyText(task->error, sizeof(task->error), text);
            }
            else if (strcmp(key, "activity_summary") == 0)
            {
                bridgeCopyText(task->activity, sizeof(task->activity), text);
            }
            else if (strcmp(key, "tool") == 0)
            {
                bridgeCopyText(task->tool, sizeof(task->tool), text);
            }
            else
            {
                /* 未使用的任务字段不缓存. */
            }
        }
        else
        {
            /* null保持为空串, 主区由apply决定是否保留旧摘要. */
        }
    }
    else if ((frame->role == BRIDGE_BUCKET) && (strcmp(key, "limit_id") == 0))
    {
        parser->codex_bucket = string_value && (strcmp(text, "codex") == 0);
    }
    else if (frame->role == BRIDGE_WINDOW)
    {
        if ((strcmp(key, "remaining_percent") == 0) && numeric && (number >= 0) && (number <= 100))
        {
            parser->window.percent = (uint8_t)(number + 0.5);
            parser->window.valid = true;
        }
        else if ((strcmp(key, "window_duration_minutes") == 0) && integer && (number <= UINT32_MAX))
        {
            parser->window.minutes = (uint32_t)number;
        }
        else
        {
            /* 重置时间等字段当前LCD无对应区域. */
        }
    }
    else
    {
        /* 未知路径仍完成词法和语法校验. */
    }
}

/** @brief 开始JSON容器, 同时识别协议对象路径. */
static void bridgeOpenContainer(bool object)
{
    SnfBridgeParser *parser = &bridge_config.parser;
    SnfBridgeJsonFrame *parent = NULL;
    SnfBridgeJsonFrame *frame;
    SnfBridgeJsonRole role = BRIDGE_GENERIC;
    uint8_t quota_index = 2;

    if (parser->depth >= SNF_BRIDGE_JSON_DEPTH)
    {
        parser->invalid = true;
        return;
    }
    if (parser->depth == 0)
    {
        if (!object || parser->complete)
        {
            parser->invalid = true;
            return;
        }
        role = BRIDGE_ROOT;
    }
    else
    {
        parent = &parser->frames[parser->depth - 1];
        quota_index = parent->quota_index;
        parent->state = BRIDGE_COMMA_OR_END;
        if ((parent->role == BRIDGE_ROOT) && (strcmp(parent->key, "tasks") == 0) && !object)
        {
            role = BRIDGE_TASKS;
            parser->tasks_present = true;
        }
        else if (object && ((parent->role == BRIDGE_TASKS)
                 || ((parent->role == BRIDGE_ROOT) && (strcmp(parent->key, "task") == 0))))
        {
            role = BRIDGE_TASK;
            memset(&parser->task, 0, sizeof(parser->task));
            parser->task.value.source = UINT8_MAX;
            parser->task.value.input_tokens = SNF_BRIDGE_TOKEN_NONE;
            parser->task.value.output_tokens = SNF_BRIDGE_TOKEN_NONE;
            parser->task.value.context_tokens = SNF_BRIDGE_TOKEN_NONE;
            parser->task.value.window_tokens = SNF_BRIDGE_TOKEN_NONE;
            parser->task.value.ui.metric_percent = SNF_UI_PERCENT_NONE;
        }
        else if ((parent->role == BRIDGE_ROOT) && (strcmp(parent->key, "quotas") == 0) && object)
        {
            role = BRIDGE_QUOTAS;
            parser->quotas_present = true;
        }
        else if (object && ((parent->role == BRIDGE_QUOTAS)
                 || ((parent->role == BRIDGE_ROOT) && (strcmp(parent->key, "quota") == 0))))
        {
            if (parent->role == BRIDGE_QUOTAS)
            {
                quota_index = bridgeSource(parent->key);
            }
            else
            {
                parser->direct_quota_present = true;
            }
            if (quota_index <= 2)
            {
                role = BRIDGE_QUOTA;
                memset(&parser->quotas[quota_index], 0, sizeof(parser->quotas[quota_index]));
            }
        }
        else if ((parent->role == BRIDGE_QUOTA) && (strcmp(parent->key, "buckets") == 0) && !object)
        {
            role = BRIDGE_BUCKETS;
        }
        else if ((parent->role == BRIDGE_BUCKETS) && object)
        {
            role = BRIDGE_BUCKET;
            memset(&parser->bucket, 0, sizeof(parser->bucket));
            parser->codex_bucket = false;
        }
        else if ((parent->role == BRIDGE_BUCKET) && object
                 && ((strcmp(parent->key, "primary") == 0) || (strcmp(parent->key, "secondary") == 0)))
        {
            role = BRIDGE_WINDOW;
            memset(&parser->window, 0, sizeof(parser->window));
        }
        else
        {
            /* 未知容器不提取业务字段. */
        }
    }
    frame = &parser->frames[parser->depth];
    memset(frame, 0, sizeof(*frame));
    frame->object = object;
    frame->role = role;
    frame->quota_index = quota_index;
    frame->state = object ? BRIDGE_KEY_OR_END : BRIDGE_VALUE_OR_END;
    parser->depth++;
}

/** @brief 结束JSON容器, 完成任务或额度候选的暂存. */
static void bridgeCloseContainer(bool object)
{
    SnfBridgeParser *parser = &bridge_config.parser;
    SnfBridgeJsonFrame *frame = &parser->frames[parser->depth - 1];

    if ((frame->object != object) || ((frame->state != BRIDGE_COMMA_OR_END)
        && (frame->state != BRIDGE_KEY_OR_END) && (frame->state != BRIDGE_VALUE_OR_END)))
    {
        parser->invalid = true;
        return;
    }
    if (frame->role == BRIDGE_TASK)
    {
        bridgeFinishTask();
    }
    else if (frame->role == BRIDGE_WINDOW)
    {
        if (bridgeQuotaBetter(&parser->window, &parser->bucket))
        {
            parser->bucket = parser->window;
        }
    }
    else if (frame->role == BRIDGE_BUCKET)
    {
        SnfBridgeQuota *quota = &parser->quotas[frame->quota_index];

        parser->bucket.codex_bucket = parser->codex_bucket;
        if (bridgeQuotaBetter(&parser->bucket, quota))
        {
            *quota = parser->bucket;
        }
    }
    else
    {
        /* 其他容器无需提交暂存内容. */
    }
    parser->depth--;
    if (parser->depth == 0)
    {
        parser->complete = true;
    }
}

/** @brief 解码字符串中的转义、UTF-16代理对及跨recv的UTF-8字符. */
static void bridgeStringByte(uint8_t byte)
{
    SnfBridgeParser *parser = &bridge_config.parser;

    if (parser->utf8_left != 0)
    {
        if ((byte & 0xC0) != 0x80)
        {
            parser->invalid = true;
            return;
        }
        parser->unicode = (parser->unicode << 6) | (byte & 0x3F);
        parser->utf8_left--;
        if (parser->utf8_left == 0)
        {
            if (parser->unicode < parser->utf8_min)
            {
                parser->invalid = true;
                return;
            }
            bridgeAppendCodepoint(parser->unicode);
        }
        return;
    }
    if (parser->hex_left != 0)
    {
        uint32_t digit;

        if ((byte >= '0') && (byte <= '9'))
        {
            digit = byte - '0';
        }
        else if ((byte >= 'a') && (byte <= 'f'))
        {
            digit = byte - 'a' + 10;
        }
        else if ((byte >= 'A') && (byte <= 'F'))
        {
            digit = byte - 'A' + 10;
        }
        else
        {
            parser->invalid = true;
            return;
        }
        parser->unicode = (parser->unicode << 4) | digit;
        parser->hex_left--;
        if (parser->hex_left == 0)
        {
            if (parser->high_surrogate != 0)
            {
                if ((parser->unicode < 0xDC00) || (parser->unicode > 0xDFFF))
                {
                    parser->invalid = true;
                    return;
                }
                bridgeAppendCodepoint(0x10000 + ((parser->high_surrogate - 0xD800) << 10)
                                      + (parser->unicode - 0xDC00));
                parser->high_surrogate = 0;
            }
            else if ((parser->unicode >= 0xD800) && (parser->unicode <= 0xDBFF))
            {
                parser->high_surrogate = parser->unicode;
                parser->escape = 2;
            }
            else
            {
                bridgeAppendCodepoint(parser->unicode);
            }
        }
        return;
    }
    if (parser->escape == 2)
    {
        parser->invalid = (byte != '\\');
        parser->escape = 3;
        return;
    }
    if (parser->escape == 3)
    {
        parser->invalid = (byte != 'u');
        parser->escape = 0;
        parser->hex_left = 4;
        parser->unicode = 0;
        return;
    }
    if (parser->escape == 1)
    {
        parser->escape = 0;
        switch (byte)
        {
            case 'u':
            {
                parser->hex_left = 4;
                parser->unicode = 0;
                break;
            }
            case 'b':
            {
                bridgeAppendCodepoint('\b');
                break;
            }
            case 'f':
            {
                bridgeAppendCodepoint('\f');
                break;
            }
            case 'n':
            {
                bridgeAppendCodepoint('\n');
                break;
            }
            case 'r':
            {
                bridgeAppendCodepoint('\r');
                break;
            }
            case 't':
            {
                bridgeAppendCodepoint('\t');
                break;
            }
            case '"':
            case '\\':
            case '/':
            {
                bridgeAppendCodepoint(byte);
                break;
            }
            default:
            {
                parser->invalid = true;
                break;
            }
        }
        return;
    }
    if (byte == '"')
    {
        parser->in_string = false;
        bridgeScalar(true);
    }
    else if (byte == '\\')
    {
        parser->escape = 1;
    }
    else if (byte < 0x20)
    {
        parser->invalid = true;
    }
    else if (byte < 0x80)
    {
        bridgeAppendCodepoint(byte);
    }
    else if ((byte >= 0xC2) && (byte <= 0xDF))
    {
        parser->unicode = byte & 0x1F;
        parser->utf8_min = 0x80;
        parser->utf8_left = 1;
    }
    else if ((byte >= 0xE0) && (byte <= 0xEF))
    {
        parser->unicode = byte & 0x0F;
        parser->utf8_min = 0x800;
        parser->utf8_left = 2;
    }
    else if ((byte >= 0xF0) && (byte <= 0xF4))
    {
        parser->unicode = byte & 0x07;
        parser->utf8_min = 0x10000;
        parser->utf8_left = 3;
    }
    else
    {
        parser->invalid = true;
    }
}

/** @brief 处理一个非换行字节, JSON无递归解析且不缓存整帧. */
static void bridgeJsonByte(uint8_t byte)
{
    SnfBridgeParser *parser = &bridge_config.parser;
    SnfBridgeJsonFrame *frame;
    bool whitespace = (byte == ' ') || (byte == '\r') || (byte == '\t');
    bool delimiter = whitespace || (byte == ',') || (byte == ']') || (byte == '}');

    parser->bytes++;
    if (parser->bytes > SNF_BRIDGE_FRAME_MAX)
    {
        parser->invalid = true;
    }
    if (parser->invalid)
    {
        return;
    }
    if (parser->in_string)
    {
        bridgeStringByte(byte);
        return;
    }
    if (parser->in_scalar)
    {
        if (!delimiter)
        {
            bridgeAppendCodepoint(byte);
            return;
        }
        parser->in_scalar = false;
        bridgeScalar(false);
        if (parser->invalid)
        {
            return;
        }
    }
    if (whitespace)
    {
        return;
    }
    if (parser->depth == 0)
    {
        if ((byte != '{') || parser->complete)
        {
            parser->invalid = true;
            return;
        }
        bridgeOpenContainer(true);
        return;
    }
    frame = &parser->frames[parser->depth - 1];
    if ((byte == '}') || (byte == ']'))
    {
        bridgeCloseContainer(byte == '}');
        return;
    }
    if (frame->state == BRIDGE_COLON)
    {
        parser->invalid = (byte != ':');
        frame->state = BRIDGE_VALUE;
        return;
    }
    if (frame->state == BRIDGE_COMMA_OR_END)
    {
        parser->invalid = (byte != ',');
        frame->state = frame->object ? BRIDGE_KEY : BRIDGE_VALUE;
        return;
    }
    if ((frame->state == BRIDGE_KEY) || (frame->state == BRIDGE_KEY_OR_END))
    {
        if (byte != '"')
        {
            parser->invalid = true;
            return;
        }
    }
    else if ((byte == '{') || (byte == '['))
    {
        bridgeOpenContainer(byte == '{');
        return;
    }
    parser->token_length = 0;
    parser->token[0] = '\0';
    parser->token_clipped = false;
    parser->in_string = (byte == '"');
    parser->in_scalar = !parser->in_string;
    if (parser->in_scalar)
    {
        bridgeAppendCodepoint(byte);
    }
}

/** @brief 把Token数格式化为k单位, 整千不带小数, 否则保留一位. */
static void bridgeFormatTokenK(char *output, size_t capacity, uint64_t tokens)
{
    uint64_t kilo = tokens / 1000;
    uint32_t tenth = (uint32_t)((tokens % 1000) / 100);

    if (tenth == 0)
    {
        snprintf(output, capacity, "%luk", (unsigned long)kilo);
    }
    else
    {
        snprintf(output, capacity, "%lu.%uk", (unsigned long)kilo, (unsigned int)tenth);
    }
}

/** @brief 按额度填写Codex右侧指标. */
static void bridgeFillCodexMetric(SnfUiAgentData *ui, const SnfBridgeQuota *quota)
{
    ui->metric_percent = SNF_UI_PERCENT_NONE;
    ui->metric_extra[0] = '\0';
    if (!quota->valid)
    {
        bridgeCopyText(ui->metric_value, sizeof(ui->metric_value), "--");
        return;
    }

    ui->metric_percent = quota->percent;
    snprintf(ui->metric_value, sizeof(ui->metric_value), "%u%%", (unsigned int)quota->percent);
    if (quota->minutes == 0)
    {
        return;
    }
    if ((quota->minutes % 60) == 0)
    {
        snprintf(ui->metric_extra, sizeof(ui->metric_extra), "%luH",
                 (unsigned long)(quota->minutes / 60));
    }
    else
    {
        snprintf(ui->metric_extra, sizeof(ui->metric_extra), "%luM",
                 (unsigned long)quota->minutes);
    }
}

/** @brief 记住最近一次同时带齐context_tokens和窗口的preCompact占用. */
static void bridgeRememberCursorContext(const SnfBridgeTask *task)
{
    SnfBridgeConfig *config = &bridge_config;

    if ((task->source != 1)
        || (task->context_tokens == SNF_BRIDGE_TOKEN_NONE)
        || (task->window_tokens == SNF_BRIDGE_TOKEN_NONE)
        || (task->window_tokens == 0))
    {
        return;
    }

    config->cursor_context_tokens = task->context_tokens;
    config->cursor_window_tokens = task->window_tokens;
}

/** @brief 用最近一次preCompact占用画Cursor进度条. */
static void bridgeFillCursorBar(SnfUiAgentData *ui)
{
    const SnfBridgeConfig *config = &bridge_config;
    uint64_t context = config->cursor_context_tokens;
    uint64_t window = config->cursor_window_tokens;

    ui->metric_percent = SNF_UI_PERCENT_NONE;
    if ((context == SNF_BRIDGE_TOKEN_NONE) || (window == SNF_BRIDGE_TOKEN_NONE) || (window == 0))
    {
        return;
    }
    if (context >= window)
    {
        ui->metric_percent = SNF_UI_PERCENT_MAX;
        return;
    }

    ui->metric_percent = (uint8_t)((context * 100) / window);
}

/** @brief 按input+output总和填写Cursor右侧TOKEN, 进度条用最近一次preCompact. */
static void bridgeFillCursorMetric(SnfUiAgentData *ui, const SnfBridgeTask *task)
{
    uint64_t input = task->input_tokens;
    uint64_t output = task->output_tokens;
    uint64_t total = 0;
    bool has_input = (input != SNF_BRIDGE_TOKEN_NONE);
    bool has_output = (output != SNF_BRIDGE_TOKEN_NONE);

    ui->metric_extra[0] = '\0';
    if (!has_input && !has_output)
    {
        bridgeCopyText(ui->metric_value, sizeof(ui->metric_value), "--");
        bridgeFillCursorBar(ui);
        return;
    }

    if (has_input)
    {
        total = input;
    }
    if (has_output)
    {
        if (output > (UINT64_MAX - total))
        {
            total = UINT64_MAX;
        }
        else
        {
            total += output;
        }
    }
    bridgeFormatTokenK(ui->metric_value, sizeof(ui->metric_value), total);
    bridgeFillCursorBar(ui);
}

/** @brief 将两来源的最优任务与独立额度组装为整屏快照. */
static void bridgePublish(void)
{
    SnfBridgeConfig *config = &bridge_config;
    SnfUiStatusData status = {0};

    status.codex.state = SNF_UI_AI_IDLE;
    status.cursor.state = SNF_UI_AI_IDLE;
    bridgeCopyText(status.codex.primary_text, sizeof(status.codex.primary_text), "等待新任务");
    bridgeCopyText(status.cursor.primary_text, sizeof(status.cursor.primary_text), "等待新任务");
    if (config->task_present[0] && (config->tasks[0].priority != 0))
    {
        status.codex = config->tasks[0].ui;
    }
    if (config->task_present[1] && (config->tasks[1].priority != 0))
    {
        status.cursor = config->tasks[1].ui;
    }
    bridgeFillCodexMetric(&status.codex, &config->quota);
    if (config->task_present[1] && (config->tasks[1].priority != 0))
    {
        bridgeFillCursorMetric(&status.cursor, &config->tasks[1]);
    }
    else
    {
        bridgeCopyText(status.cursor.metric_value, sizeof(status.cursor.metric_value), "--");
        status.cursor.metric_extra[0] = '\0';
        bridgeFillCursorBar(&status.cursor);
    }
    if (snfUiHandleSubmit(&status) == 0)
    {
        config->ui_dirty = false;
    }
}

/** @brief 有界等待socket可读或可写, 超时返回0, 错误返回负数. */
static int32_t bridgeWaitSocket(bool write, uint32_t milliseconds)
{
    SnfBridgeConfig *config = &bridge_config;
    fd_set descriptors;
    struct timeval timeout = {0};

    FD_ZERO(&descriptors);
    FD_SET(config->socket_fd, &descriptors);
    timeout.tv_sec = (long)(milliseconds / 1000);
    timeout.tv_usec = (long)((milliseconds % 1000) * 1000);

    return lwip_select(config->socket_fd + 1, write ? NULL : &descriptors,
                       write ? &descriptors : NULL, NULL, &timeout);
}

/** @brief 完整发送一条JSONL, 正确处理短写及有限时间的发送阻塞. */
static int32_t bridgeSend(const char *line)
{
    SnfBridgeConfig *config = &bridge_config;
    size_t length = strlen(line);
    size_t sent = 0;
    TickType_t start = xTaskGetTickCount();

    while (sent < length)
    {
        int32_t result;

        if ((snfNetGetState() != SNF_NET_STATE_READY)
            || ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(SNF_BRIDGE_CONNECT_MS)))
        {
            return -1;
        }
        result = (int32_t)lwip_send(config->socket_fd, &line[sent], length - sent, 0);
        if (result > 0)
        {
            sent += (size_t)result;
        }
        else if ((result < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)))
        {
            result = bridgeWaitSocket(true, SNF_BRIDGE_RETRY_MS);
            if ((result < 0) && (errno != EINTR))
            {
                return -1;
            }
        }
        else
        {
            return -1;
        }
    }

    return 0;
}

/** @brief 发送设备认证, Token仅用于协议传输且不写日志. */
static int32_t bridgeSendHello(void)
{
    /* 当前使用用户提供的十六进制Token; 后续配置持久化需另行约定NVDM键. */
    static const char hello[] =
        "{\"type\":\"hello\",\"protocol\":1,\"device_id\":\"sonoff-lcd\","
        "\"token\":\"829aeafe1d9a8679ade5182f07e4831e67e7809f9fe157833c6a63d80f1ed468\"}\n";

    return bridgeSend(hello);
}

/** @brief 本帧没有新摘要时, 保留上一帧真正来自Bridge的主区. */
static bool bridgeKeepPrimary(const SnfBridgeTask *incoming, const SnfBridgeTask *current)
{
    return (incoming->primary_origin == BRIDGE_PRIMARY_NONE)
           && (current->primary_origin == BRIDGE_PRIMARY_BRIDGE)
           && (current->ui.primary_text[0] != '\0');
}

/** @brief 更新当前会话; 其他会话仅以新的活动任务接替, 不回切历史任务. */
static int32_t bridgeApplyTask(void)
{
    SnfBridgeConfig *config = &bridge_config;
    SnfBridgeParser *parser = &config->parser;

    if (parser->tasks_present)
    {
        LOG_E(tag, "Invalid task_update shape");
        return 0;
    }
    for (uint32_t source = 0; source < 2; source++)
    {
        if (parser->task_present[source])
        {
            const SnfBridgeTask *incoming = &parser->tasks[source];
            SnfBridgeTask *current = &config->tasks[source];
            bool same_session = false;
            bool replace = !config->task_present[source];
            char kept_primary[SNF_UI_PRIMARY_TEXT_CAPACITY] = {0};
            bool keep_primary = false;
            uint64_t kept_input = current->input_tokens;
            uint64_t kept_output = current->output_tokens;
            uint64_t kept_context = current->context_tokens;
            uint64_t kept_window = current->window_tokens;

            if (config->task_present[source])
            {
                same_session = (strcmp(incoming->session, current->session) == 0);
                if (same_session)
                {
                    replace = (incoming->sequence > current->sequence);
                    keep_primary = replace && bridgeKeepPrimary(incoming, current);
                }
                else
                {
                    replace = (incoming->priority == 3) && (incoming->sequence > current->sequence)
                              && (incoming->updated_ms >= current->updated_ms);
                }
            }
            if (keep_primary)
            {
                memcpy(kept_primary, current->ui.primary_text, sizeof(kept_primary));
            }
            if (replace)
            {
                *current = *incoming;
                if (keep_primary)
                {
                    memcpy(current->ui.primary_text, kept_primary, sizeof(current->ui.primary_text));
                    current->primary_origin = BRIDGE_PRIMARY_BRIDGE;
                }
                if (same_session)
                {
                    if (incoming->input_tokens == SNF_BRIDGE_TOKEN_NONE)
                    {
                        current->input_tokens = kept_input;
                    }
                    if (incoming->output_tokens == SNF_BRIDGE_TOKEN_NONE)
                    {
                        current->output_tokens = kept_output;
                    }
                    if (incoming->context_tokens == SNF_BRIDGE_TOKEN_NONE)
                    {
                        current->context_tokens = kept_context;
                    }
                    if (incoming->window_tokens == SNF_BRIDGE_TOKEN_NONE)
                    {
                        current->window_tokens = kept_window;
                    }
                }
                bridgeRememberCursorContext(incoming);
                config->task_present[source] = true;
                config->ui_dirty = true;
            }
        }
    }

    return 0;
}

/** @brief 仅在换行且整帧校验成功后处理消息. */
static int32_t bridgeDispatch(void)
{
    SnfBridgeConfig *config = &bridge_config;
    SnfBridgeParser *parser = &config->parser;
    const char *type = parser->type;

    if ((strcmp(type, "server_hello") != 0) && (strcmp(type, "snapshot") != 0)
        && (strcmp(type, "task_update") != 0) && (strcmp(type, "quota_update") != 0)
        && (strcmp(type, "ping") != 0) && (strcmp(type, "pong") != 0) && (strcmp(type, "error") != 0))
    {
        /* 未知消息类型整体忽略, 包括扩展类型自有的protocol字段. */
        config->last_message = xTaskGetTickCount();

        return 0;
    }
    if (parser->protocol_present && !parser->protocol_valid)
    {
        LOG_E(tag, "Bridge protocol version incompatible");
        config->retry_ms = SNF_BRIDGE_CONFIG_RETRY_MS;
        return -1;
    }
    config->last_message = xTaskGetTickCount();
    if (strcmp(type, "ping") == 0)
    {
        char pong[96];

        /* 回显Bridge的Unix毫秒, 不把FreeRTOS运行时间伪装成Unix时间. */
        snprintf(pong, sizeof(pong), "{\"type\":\"pong\",\"timestamp_ms\":%llu}\n",
                 (unsigned long long)parser->timestamp);

        return bridgeSend(pong);
    }
    if (strcmp(type, "pong") == 0)
    {
        return 0;
    }
    if (strcmp(type, "error") == 0)
    {
        if (strcmp(parser->error_code, "authentication_failed") == 0)
        {
            LOG_E(tag, "Bridge authentication failed; retry after 30 seconds");
            config->retry_ms = SNF_BRIDGE_CONFIG_RETRY_MS;
        }
        else
        {
            LOG_E(tag, "Bridge reported a protocol error");
        }

        return -1;
    }
    if (strcmp(type, "server_hello") == 0)
    {
        if ((config->phase != BRIDGE_WAIT_HELLO) || !parser->protocol_present || !parser->auth_present)
        {
            LOG_E(tag, "Invalid server_hello");
            return -1;
        }
        if (parser->auth_required)
        {
            if (bridgeSendHello() != 0)
            {
                return -1;
            }
        }
        config->idle_ms = SNF_BRIDGE_IDLE_MS;
        if (parser->heartbeat_seconds > (SNF_BRIDGE_IDLE_MS / 2500))
        {
            /* 运行期心跳参数允许变化, 等待至少两个心跳周期并留余量. */
            config->idle_ms = parser->heartbeat_seconds * 2500;
        }
        config->phase = BRIDGE_WAIT_SNAPSHOT;
        config->phase_start = xTaskGetTickCount();

        return 0;
    }
    if (strcmp(type, "snapshot") == 0)
    {
        if (((config->phase != BRIDGE_WAIT_SNAPSHOT) && (config->phase != BRIDGE_RUNNING))
            || !parser->protocol_present || !parser->tasks_present || !parser->quotas_present)
        {
            LOG_E(tag, "Invalid snapshot or handshake order");
            return 0;
        }
        memcpy(config->tasks, parser->tasks, sizeof(config->tasks));
        memcpy(config->task_present, parser->task_present, sizeof(config->task_present));
        if (config->task_present[1])
        {
            bridgeRememberCursorContext(&config->tasks[1]);
        }
        config->quota = parser->quotas[0];
        config->phase = BRIDGE_RUNNING;
        config->ui_dirty = true;

        return 0;
    }
    if (strcmp(type, "task_update") == 0)
    {
        if ((config->phase == BRIDGE_RUNNING) && parser->protocol_present)
        {
            return bridgeApplyTask();
        }

        return 0;
    }
    if (strcmp(type, "quota_update") == 0)
    {
        if ((config->phase == BRIDGE_RUNNING) && parser->protocol_present && parser->direct_quota_present)
        {
            if (parser->source == 0)
            {
                config->quota = parser->quotas[2];
                config->ui_dirty = true;
            }
        }
    }

    return 0;
}

/** @brief 拆分JSONL行, 错误帧丢弃至下一个LF以恢复同步. */
static int32_t bridgeReceiveBytes(const uint8_t *bytes, size_t length)
{
    SnfBridgeParser *parser = &bridge_config.parser;
    int32_t result = 0;

    for (size_t index = 0; index < length; index++)
    {
        if (bytes[index] == '\n')
        {
            if (parser->invalid || !parser->complete || parser->in_string || parser->in_scalar)
            {
                LOG_E(tag, "Discard invalid/incomplete JSONL frame, bytes=%lu", (unsigned long)parser->bytes);
            }
            else
            {
                result = bridgeDispatch();
            }
            bridgeResetParser();
            if (result != 0)
            {
                return result;
            }
        }
        else
        {
            bridgeJsonByte(bytes[index]);
        }
    }

    return 0;
}

/** @brief 集中关闭socket并清除本次连接的序号、任务和半帧. */
static void bridgeDisconnect(void)
{
    SnfBridgeConfig *config = &bridge_config;

    if (config->socket_fd >= 0)
    {
        lwip_close(config->socket_fd);
        config->socket_fd = -1;
    }
    config->phase = BRIDGE_DISCONNECTED;
    bridgeResetParser();
    memset(config->tasks, 0, sizeof(config->tasks));
    memset(config->task_present, 0, sizeof(config->task_present));
    memset(&config->quota, 0, sizeof(config->quota));
    config->ui_dirty = false;
    /* 断线期间保留LCD最后画面; 新连接仅以完整snapshot作为显示基线. */
}

/** @brief 从已有NVDM适配读取PC的局域网IPv4地址. */
static int32_t bridgeBuildPeerAddress(struct sockaddr_in *address)
{
    char ip_string[16] = {0};
    uint32_t host;

    if (snfTestItemGet(ip_string, sizeof(ip_string)) != 0)
    {
        LOG_E(tag, "Bridge IP configuration unavailable");
        return -1;
    }
    ip_string[sizeof(ip_string) - 1] = '\0';
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = lwip_htons(SNF_BRIDGE_PORT);
    if (inet_aton(ip_string, &address->sin_addr) == 0)
    {
        LOG_E(tag, "Invalid Bridge IPv4 address");
        return -1;
    }
    host = lwip_ntohl(address->sin_addr.s_addr);
    if ((host == 0) || ((host >> 24) == 127) || ((host >> 24) >= 224))
    {
        LOG_E(tag, "Bridge requires a unicast PC IPv4 address");
        return -1;
    }

    return 0;
}

/** @brief 按Wi-Fi连接、等待IP、TCP连接的顺序推进一次. */
static int32_t bridgeConnect(void)
{
    SnfBridgeConfig *config = &bridge_config;
    SnfWifiStaConfig sta_config = {0};
    struct sockaddr_in address = {0};
    int32_t net_state = snfNetGetState();
    int32_t nonblocking = 1;
    int32_t option = 1;
    int32_t result;
    int32_t socket_error = 0;
    socklen_t error_size = sizeof(socket_error);
    TickType_t start;

    if ((net_state == SNF_NET_STATE_IDLE) || (net_state == SNF_NET_STATE_RECONNECT_WAIT))
    {
        if (snfNvdmReadStr(NVDM_USER_GROUP, "wifi.ssid", (uint8_t *)sta_config.ssid,
                           sizeof(sta_config.ssid)) != 0)
        {
            LOG_E(tag, "Wi-Fi SSID unavailable");
            return -1;
        }
        if (snfNvdmReadStr(NVDM_USER_GROUP, "wifi.password", (uint8_t *)sta_config.password,
                           sizeof(sta_config.password)) != 0)
        {
            LOG_E(tag, "Wi-Fi password unavailable");
            return -1;
        }
        if (sta_config.ssid[0] == '\0')
        {
            return -1;
        }
        if (snfNetStaConnect(&sta_config) != 0)
        {
            LOG_E(tag, "Wi-Fi connection request failed");
        }

        return -1;
    }
    if (net_state != SNF_NET_STATE_READY)
    {
        /* CONNECTING与WAIT_IP阶段只等待, 不反复重启Wi-Fi; AP模式也不强制切换. */
        return -1;
    }
    if (bridgeBuildPeerAddress(&address) != 0)
    {
        return -1;
    }
    config->socket_fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (config->socket_fd < 0)
    {
        LOG_E(tag, "Bridge socket creation failed");
        return -1;
    }
    if (lwip_ioctl(config->socket_fd, FIONBIO, &nonblocking) != 0)
    {
        bridgeDisconnect();
        return -1;
    }
    if (lwip_setsockopt(config->socket_fd, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(option)) != 0)
    {
        LOG_I(tag, "TCP_NODELAY unavailable");
    }
    result = lwip_connect(config->socket_fd, (const struct sockaddr *)&address, sizeof(address));
    if ((result < 0) && (errno != EINPROGRESS) && (errno != EWOULDBLOCK))
    {
        LOG_E(tag, "Bridge connect failed, errno=%d", errno);
        bridgeDisconnect();
        return -1;
    }
    start = xTaskGetTickCount();
    while (result < 0)
    {
        int32_t ready = bridgeWaitSocket(true, SNF_BRIDGE_RETRY_MS);

        if ((snfNetGetState() != SNF_NET_STATE_READY)
            || ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(SNF_BRIDGE_CONNECT_MS)))
        {
            bridgeDisconnect();
            return -1;
        }
        if (ready > 0)
        {
            if (lwip_getsockopt(config->socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) != 0)
            {
                bridgeDisconnect();
                return -1;
            }
            if (socket_error != 0)
            {
                LOG_E(tag, "Bridge connect failed, socket error=%ld", (long)socket_error);
                bridgeDisconnect();
                return -1;
            }
            result = 0;
        }
        else if ((ready < 0) && (errno != EINTR))
        {
            bridgeDisconnect();
            return -1;
        }
        else
        {
            /* 本次1秒检查尚未完成连接. */
        }
    }
    bridgeResetParser();
    config->idle_ms = SNF_BRIDGE_IDLE_MS;
    config->phase = BRIDGE_WAIT_HELLO;
    config->last_message = xTaskGetTickCount();
    config->phase_start = config->last_message;
    LOG_I(tag, "Bridge TCP connected");

    return 0;
}

/** @brief 接收一次网络事件, 1秒轮询以便及时检查Wi-Fi和超时. */
static int32_t bridgeReceiver(void)
{
    SnfBridgeConfig *config = &bridge_config;
    uint8_t buffer[512];
    TickType_t now = xTaskGetTickCount();
    int32_t result;

    if ((snfNetGetState() != SNF_NET_STATE_READY)
        || ((TickType_t)(now - config->last_message) >= pdMS_TO_TICKS(config->idle_ms))
        || ((config->phase != BRIDGE_RUNNING)
            && ((TickType_t)(now - config->phase_start) >= pdMS_TO_TICKS(SNF_BRIDGE_HANDSHAKE_MS))))
    {
        LOG_I(tag, "Bridge network/handshake timeout");
        return -1;
    }
    if (config->ui_dirty)
    {
        bridgePublish();
    }
    result = bridgeWaitSocket(false, SNF_BRIDGE_RETRY_MS);
    if (result == 0)
    {
        return 0;
    }
    if (result < 0)
    {
        return (errno == EINTR) ? 0 : -1;
    }
    result = (int32_t)lwip_recv(config->socket_fd, buffer, sizeof(buffer), 0);
    if (result == 0)
    {
        LOG_I(tag, "Bridge peer closed connection");
        return -1;
    }
    if (result < 0)
    {
        return ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) ? 0 : -1;
    }
    result = bridgeReceiveBytes(buffer, (size_t)result);
    if ((result == 0) && config->ui_dirty)
    {
        bridgePublish();
    }

    return result;
}

/** @brief Bridge常驻任务, 连接失败时保留当前网络阶段并定期重试. */
static void bridgeHandleTask(void *arg)
{
    SnfBridgeConfig *config = &bridge_config;

    for (;;)
    {
        if (config->phase == BRIDGE_DISCONNECTED)
        {
            vTaskDelay(pdMS_TO_TICKS(config->retry_ms));
            config->retry_ms = SNF_BRIDGE_RETRY_MS;
            bridgeConnect();
        }
        else if (bridgeReceiver() != 0)
        {
            bridgeDisconnect();
        }
        else
        {
            /* 下一轮继续等待网络事件. */
        }
    }
}

int snfBridgeHandleInit(void)
{
    SnfBridgeConfig *config = &bridge_config;

    /* 启动标记的检查与设置不可被另一个初始化调用抢占. */
    taskENTER_CRITICAL();
    if (config->started)
    {
        taskEXIT_CRITICAL();
        return 0;
    }
    config->started = true;
    taskEXIT_CRITICAL();

    if (xTaskCreate(bridgeHandleTask, SNF_BRIDGE_HANDLE_TASK_NAME, SNF_BRIDGE_HANDLE_TASK_STACKSIZE,
                    NULL, SNF_BRIDGE_HANDLE_TASK_PRIO, NULL) != pdPASS)
    {
        taskENTER_CRITICAL();
        config->started = false;
        taskEXIT_CRITICAL();
        LOG_E(tag, "Bridge task creation failed");
        return -1;
    }

    return 0;
}
