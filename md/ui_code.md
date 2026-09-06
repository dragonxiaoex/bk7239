# 设备 UI 框架

本目录提供 428 × 142 LCD 的 LVGL 9.5 + FreeRTOS UI 框架。以下说明对应当前 `sonoff_ui_*` 源码（2026-09-05）。

## 模块职责

| 文件 | 职责与主要接口 |
| --- | --- |
| [sonoff_ui_type.h]| 定义 `SnfUiAiState`、`SnfUiAgentData`、`SnfUiStatusData` 和 `SNF_UI_*` 数据常量。 |
| [sonoff_ui_style.h] / [sonoff_ui_style.c] | 颜色、字体入口、屏幕尺寸、区域布局常量和 `SnfUiStyles`；提供 `snfUiStyleInit()`、`snfUiStyleGet()`。控件的部分具体坐标仍在行组件中设置。 |
| [sonoff_ui_agent_row.h]/ [sonoff_ui_agent_row.c] | 通用行组件 `SnfUiAgentRow`；提供 `snfUiAgentRowCreate()`、`snfUiAgentRowUpdate()`。 |
| [sonoff_ui_handle.h] / [sonoff_ui_handle.c] | 创建状态队列和 UI 任务，在任务内初始化 LVGL、LCD、tick、显示驱动并创建页面，周期调用 `lv_timer_handler()`。 |

Codex 和 Cursor 各调用一次 `snfUiAgentRowCreate()` 即创建完整的一行，通过 `SnfUiAgentRowConfig` 的 `source_name`、`metric_name`、`source_color` 个性化设置。两行共用同一个更新接口。

## 当前运行状态

当前任务显示初始页面，并在循环中非阻塞接收快照、刷新两行、执行 `lv_timer_handler()`，再通过 `vTaskDelayUntil()` 按 5 ms 周期等待。初始两行均为“摸鱼中...” / “等待新任务” / “暂无活动”，百分比为 `--`；Codex 附加文字为 `5H`，Cursor 为 `QUOTA N/A`。

长度为 1 的状态队列消费逻辑现已启用，提交成功后由 UI 任务异步刷新画面。

## 启动顺序

应用层在板级前置条件就绪后调用：

```c
#include "sonoff_ui_handle.h"

if (snfUiHandleStart() != 0) {
    /* 处理队列或任务创建失败 */
}
```

`snfUiHandleStart()` 先创建长度为 1、元素大小为 `sizeof(SnfUiStatusData)` 的队列，再创建任务。任务内按以下顺序初始化：

1. `lv_init()`。
2. `xf_lcd_init()`。
3. `lv_tick_set_cb(handleTickGet)`，以 `xTaskGetTickCount() * portTICK_PERIOD_MS` 提供毫秒时间。
4. `lv_port_disp_init()`。
5. `snfUiStyleInit()`。
6. `handleCreatePage()`：通过 `lv_obj_create(NULL)` 创建新屏幕，创建两条行组件、应用初始数据，再调用 `lv_screen_load()`。

当前应由 UI 任务完成上述初始化，避免应用层重复初始化。`sonoff_ui_handle.h` 中“调用前完成 LVGL 初始化”的注释仍是旧约定，此处以 `.c` 的实际执行顺序为准。

任务名为 `snf_ui`，传入 `xTaskCreate()` 的栈深度参数为 `2 * 1024`，实际优先级使用 `TASK_PRIORITY_LOW`。栈深度的单位应按目标 FreeRTOS 移植确认。源码中的 `SNF_UI_HANDLE_TASK_PRIO` 未用于任务创建，`SNF_UI_HANDLE_REFRESH_PERIOD_MS` 为 5，供定周期等待使用。

## 数据提交接口

| 接口 | 当前语义与返回值 |
| --- | --- |
| `snfUiHandleStart()` | 创建队列和任务；已有队列时直接返回成功。成功为 `0`，失败为 `-1`，不等待页面创建完成。 |
| `snfUiHandleSubmit(data)` | 按值覆盖队列中的完整快照，无阻塞等待；成功为 `0`，失败为 `-1`。 |
| `snfUiHandleSubmitFromIsr(data, task_woken)` | 在 ISR 中按值覆盖快照，要求两个指针均非空；成功为 `0`，失败为 `-1`。 |
| `snfUiHandleIsStarted()` | 仅检查队列是否非空，返回 `1` 或 `0`，不表示显示初始化已经完成。 |

网络或协议任务提交整屏数据，Codex 和 Cursor 两行都需填写。以下示例可入队并由 UI 任务异步显示。

```c
SnfUiStatusData data = {
    .codex = {
        .state = SNF_UI_AI_RUNNING,
        .primary_text = "正在规划固件界面",
        .secondary_text = "读取 device-protocol.md",
        .metric_percent = 65,
        .metric_extra = "5H",
    },
    .cursor = {
        .state = SNF_UI_AI_IDLE,
        .primary_text = "等待新任务",
        .secondary_text = "上次任务已完成",
        .metric_percent = 48,
        .metric_extra = "QUOTA N/A",
    },
};

(void)snfUiHandleSubmit(&data);
```

`SnfUiAgentData` 自持文本数组，主文本、次文本、附加文本容量分别为 96、96、24 字节（包含结尾的 `\0`，不是中文字符数）。生产者应提供正确终止的 UTF-8 文本；队列按值复制后可复用原缓冲区，提交时不要并发修改同一份数据。

`metric_percent` 为 `uint8_t`，正常范围为 `0` 至 `SNF_UI_PERCENT_MAX`（100）。`SNF_UI_PERCENT_NONE`（`UINT8_MAX`）显示 `--` 并清空进度条；其他大于 100 的值被限制为 100。

## 状态和显示规则

| `SnfUiAiState` | 状态文案 |
| --- | --- |
| `SNF_UI_AI_IDLE` | `摸鱼中...` |
| `SNF_UI_AI_RUNNING` | `摘棉花中...` |
| `SNF_UI_AI_WAITING` | `等待批准...` |
| `SNF_UI_AI_ERROR` | `出错了...` |
| `SNF_UI_AI_OFFLINE` | `已离线` |
| 未知枚举值 | `状态未知` |

每行高 71 像素，Codex 位于 `y=0`，Cursor 位于 `y=71`。状态、主文本和次文本使用 `LV_LABEL_LONG_DOT`；来源名称、指标名称、数值和附加文字使用 `LV_LABEL_LONG_CLIP`。当前未实现分页或跑马灯。

行组件直接显示数据，不解析 Bridge JSON，也不选择任务或计算额度窗口。`think_summary`、`content_summary` 等协议字段需由上层转换为 `SnfUiStatusData`，映射意图见 [UI 设计说明](../device-status-ui-v1.md)。

## 字体

当前七个字体入口已分别绑定具体资源：主文本使用思源黑体 Bold 24 px，来源名称使用 Roboto Condensed Bold 18 px，状态使用思源黑体 Medium 12 px，次文本使用思源黑体 Regular 12 px，指标数值使用 Roboto Condensed Bold 24 px，指标名称与附加文字使用 Roboto Condensed Regular 12 px。

来源名称标签的 Y 坐标已从 13 改为 17，使当前标题字体的 13 px 高大写字形与 11 px 圆点垂直居中；标签宽高保持 63 × 22 px。下列首次核查内容保留为历史记录。

### 首次实机现象与源码核查（历史记录）

- 实机反馈：界面颜色正常，中文未显示，`CODEX` / `CURSOR` 字体偏小、偏细，与设计图不一致。
- `sonoff/lvgl/lv_conf.h` 将默认字体设为 `lv_font_montserrat_14`，内置中文字体未启用，`LV_USE_FONT_PLACEHOLDER` 为 0；当前默认字体缺少中文字形，缺字也不会显示占位框。
- `sonoff_ui_style.c` 已分别绑定七类字体宏，但这些宏目前都指向同一默认字体，因此主次字号和来源名称的粗体效果尚未落地。标签控件的宽高不会自动放大字体。
- 工程已有 `sonoff/lvgl/lv_font_cn16.c`，其生成参数仅包含 ASCII 和“单击双击长按连接断开电压版本升级中温度湿度气压质量”等中文，不足以覆盖当前状态文案及任务摘要，不能直接作为本界面的完整中文字库。
- 来源名称控件当前宽 63 px、高 22 px，使用裁剪模式；后续选择字号和字重时需验证 `CURSOR` 六个字母的实际宽度。已有 `lv_font_cn16` 的行高为 20 px，高于当前状态、次要信息标签的 18 px，高度也需核对。

后续字体选型需同时考虑固定文案和 PC 动态摘要的中文覆盖范围；仅提取当前页面文字可用于静态验证，但不能满足后续动态文本。具体字体、字重、字号及字符范围尚未确定，布局仍按实机测试结果评估。

固件工程需提供包含所需中文字符的字体。这些宏应展开为 `const lv_font_t *`，覆盖定义和字体声明必须在编译 `sonoff_ui_style.c` 时可见；仅在调用方源文件中定义不会改变样式模块的字体。

## 集成依赖与任务约定

完整固件工程需提供 LVGL、FreeRTOS，以及当前引用的 `lv_port_disp.h`、`xf_lcd_nv3007.h`、`sonoff_log.h`、`sonoff_task_def.h` 及其实现。本目录不包含这些移植层、中文字体或独立构建配置。

对象创建、样式操作、行更新和 `lv_timer_handler()` 由 UI 任务统一执行，外部任务使用状态提交接口。当前采用 tick 回调；显示驱动负责 flush 完成通知。ISR 提交状态时应将 `task_woken` 初始化为 `pdFALSE`，并按目标 FreeRTOS 移植要求处理 `portYIELD_FROM_ISR()`。当前 UI 任务非阻塞轮询队列，不阻塞等待队列数据。


## 中文显示测试

`project/onoff_plug/src/sonoff_ui_test.c` 与对应头文件提供 `int32_t snfUiTestSubmit(void)`。在 `snfUiHandleStart()` 返回成功后，从普通任务上下文调用一次即可；返回 0 表示快照入队成功，负数表示失败。测试函数不启动 UI，也不创建周期任务。

```c
#include "sonoff_ui_test.h"

/* UI已启动，在任务上下文提交一次并检查返回值。 */
if (snfUiTestSubmit() != 0)
{
    /* 处理提交失败。 */
}
```

测试使用一份固定中文样例，便于重复对比调整效果：Codex 运行中，显示 `正在调整界面` / `读取 ui_style.c`、剩余额度 65%、`5H`；Cursor 空闲，显示 `任务已完成` / `已更新 3 个文件`、上下文使用率 48%、`N/A`。主次文本所用字符已核对包含在当前对应字库中，次文本同时测试中文、英文文件名和数字混排。状态文案仍由行组件生成。


## 最新布局调整

指标区分隔线现为 x=324，内容起点 x=336、宽 83 px；中间两类文本宽度扩大为 197 px。百分比标签宽 56 px，附加文字位于 x=394、宽 25 px。主文本已绑定 24 px 思源黑体 Bold；标签根据实机反馈从 y=8 下移至 y=11、高 30 px，次文本仍为 y=41，两标签区域不重叠。
