# 设备状态屏 UI v1

参考图：[device-status-ui-v1.png](device-status-ui-v1.png)

代码入口：[设备 UI 框架说明](code/README.md)。当前实现使用 `sonoff_ui_*` 文件、`SnfUi*` 类型、`snfUi*` 函数和 `SNF_UI_*` 宏。

## 设备约束

- LCD：428 × 142，横屏
- 色深：RGB565 / 16 bit
- 接口：SPI
- 安装方式：悬挂在电脑显示器上
- 交互：无触控，不显示按钮或标签页

## 布局

- Codex 与 Cursor 分成上下两行，始终同时显示。
- 每行由来源区、任务信息区和指标区组成。
- 主任务信息使用大号白字；次要活动信息使用灰色。
- 来源颜色只用于左侧竖条、圆点、来源名称和对应进度条。
- 状态颜色只用于来源名称下方的状态文案。

信息密度与可读性待在实际 LCD 上测试后再评估；当前保留现有布局和目标字号，后续根据实测结果决定是否调整区域宽度、字号或摘要长度。

当前实现由 `snfUiAgentRowCreate()` 共用创建两行，每行 71 px。来源区分隔线位于 `x=104`，信息区从 `x=117` 开始、宽 177 px，指标区分隔线位于 `x=304`，指标区从 `x=316` 开始、宽 108 px。区域常量定义于 [sonoff_ui_style.h](code/sonoff_ui_style.h)，控件细节坐标见 [sonoff_ui_agent_row.c](code/sonoff_ui_agent_row.c)。

## 颜色

- 背景：`#0B0F14`
- 主文字：`#F0F4F8`
- 次要文字：`#87919D`
- Codex：`#38C4D8`
- Cursor：`#A98BFF`
- 空闲状态：`#58C98B`
- 运行状态：`#F06A6A`
- 等待批准：`#E4B84A`
- 错误状态：`#F06A6A`
- 离线或未知状态：`#66717D`
- 分隔线：`#252B33`
- 进度条轨道：`#2B3038`

固件实现时使用上述颜色对应的 RGB565 实色，不使用渐变、模糊或光晕。

## 状态文案

- 正常空闲：`摸鱼中...`
- 正常运行：`摘棉花中...`

后续计划为运行状态加入动态点效果：文案依次显示 `摘棉花中.` → `摘棉花中..` → `摘棉花中...`，循环变化。此效果尚未实现，动画间隔待定。

当前代码已提供独立的异常与等待状态文案：`SNF_UI_AI_WAITING` 显示 `等待批准...`，`SNF_UI_AI_ERROR` 显示 `出错了...`，`SNF_UI_AI_OFFLINE` 显示 `已离线`；未知枚举值显示 `状态未知`。连接状态检测和协议状态到枚举的转换由上层负责。

断连场景留待后续设计，本阶段暂不展开；上述离线文案仅记录现有代码能力。

## 信息映射

以下为上层协议适配的映射规则。当前 UI 接收 `SnfUiStatusData`，其中 `codex`、`cursor` 均为 `SnfUiAgentData`；行组件直接使用 `state`、`primary_text`、`secondary_text`、`metric_percent`、`metric_value` 和 `metric_extra`，不自行解析或选择协议字段。

### Codex

- 主信息：有 `content_summary` 用它，否则用 `think_summary`；都空且任务还在跑时显示“思考中”，不因 `using_tool` 清空。`completed` 且主副区都空时改为“等待新任务”。
- 次要信息：只显示 `activity_summary`；字段缺失或为 `null` 时清空。`completed` 且主副区都空时改为“上次任务已完成”。
- 右侧主指标：Codex 账户剩余额度百分比，例如 `QUOTA 65%` 表示剩余额度为 65%；进度条填充比例对应剩余额度。
- 额度窗口作为次要文字显示，例如 `5H`。

### Cursor

- 主信息和次要信息与 Codex 使用相同规则。
- 右侧主指标：标题为 `TOKEN`，主数值为 `input_tokens + output_tokens`（单位 k，例如 `131k`）。缺一侧时按 0 计入；两侧都缺时显示 `--`。不再显示 `OUT`。
- 进度条：用最近一次同时带有 `context_tokens` 和 `context_window_size` 的 `preCompact` 计算，`percent = min(100, context_tokens * 100 / context_window_size)`。后续帧这两个字段为 `null` 时沿用上次值；还没有成对数据时条子清空。超过窗口时封顶 100%。
- 个人账户额度不可查询，右侧不再显示 `N/A` 或 `0%`。

任一行的进度条不可用时，上层应传 `SNF_UI_PERCENT_NONE`；组件将进度条清零，主数值仍显示 `metric_value`（空则 `--`）。正常百分比上限为 `SNF_UI_PERCENT_MAX`（100）。

## 文本显示

- 主信息优先保证远距离可读性，建议约 18–20 px。
- 来源名称建议约 14–16 px。
- 状态和次要信息建议约 11–12 px。
- 当前状态、主文本和次文本使用省略号截断（`LV_LABEL_LONG_DOT`），来源名称及指标文字使用裁剪（`LV_LABEL_LONG_CLIP`）；尚未实现分页，不使用横向跑马灯。

以上字号为设计目标。当前 `SNF_UI_FONT_*` 已分别绑定各字号和字重的字体资源，主信息采用 20 px 思源黑体 Bold，中文覆盖取决于生成字库时收录的字符。文本数组容量分别为主文本 96、次文本 96、指标附加文本 24 字节，均需包含字符串终止符。

## 当前运行方式

`snfUiHandleStart()` 创建队列及 UI 任务，任务内部依次初始化 LVGL、LCD、tick 回调、显示移植层和样式，创建独立屏幕后通过 `lv_screen_load()` 加载。当前循环非阻塞接收状态快照并更新两行，执行 `lv_timer_handler()` 后通过 `vTaskDelayUntil()` 按 5 ms 周期等待。

初始两行均显示空闲状态、“等待新任务”和“暂无活动”，指标数值为 `--`。接收快照并更新两行的代码现已启用，画面会随 `snfUiHandleSubmit()` 入队的数据异步更新。启动和提交接口的准确约定见 [代码说明](code/README.md)。

当前 PNG 是放大后的视觉参考稿，不是可直接烧录的 428 × 142 像素资源；最终界面由固件按本规范绘制。


## 字体首轮测试建议（待实机确认）

首轮测试前默认字体曾切换为 `lv_font_cn16`，七类文字共用该字体；目前已分别绑定字体资源。原始 Source Han Sans OTF 可按不同字号和字重生成字库，16 px 是当前生成资源的大小。

建议中文继续使用思源黑体，英文来源名称与指标使用 Roboto Condensed。此为参考图风格匹配建议，并非确认参考图的原始字体。

以下表格保存为字体对比参考；主任务信息的 18 / 20 px 为首轮对比方案，后续采用 24 px 的实测记录见下文。

| 显示内容 | 字体 | 字号 |
| --- | --- | --- |
| CODEX / CURSOR | Roboto Condensed **Bold** | 18 px |
| 主任务信息 | 思源黑体 **Bold** | 18 px，另做 20 px 对比 |
| 状态文字 | 思源黑体 **Medium** | 12 px |
| 次要活动信息 | 思源黑体 **Regular** | 12 px |
| 65% / 128k | Roboto Condensed **Bold** | 24 px |
| QUOTA / TOKEN、5H | Roboto Condensed **Regular** | 12 px |

官方下载：

- [思源黑体简体中文静态 OTF](https://github.com/adobe-fonts/source-han-sans/tree/release/SubsetOTF/CN)：选择 `SourceHanSansCN-Bold.otf`、`SourceHanSansCN-Medium.otf`、`SourceHanSansCN-Regular.otf`。
- [Roboto 静态 TTF](https://github.com/googlefonts/roboto-2/tree/main/src/hinted)：选择 `RobotoCondensed-Bold.ttf`、`RobotoCondensed-Regular.ttf`。这是 Google Fonts 的旧版归档仓库，适合本次使用固定字重文件生成测试字库。

首轮建议沿用现有 4 bpp、不压缩的生成配置；每个字号分别输出不同名称的字体资源，并分别绑定 `SNF_UI_FONT_*`。英文字库可先收录 ASCII 可打印字符，中文字库先收录测试文案及 ASCII，动态摘要的广泛中文覆盖后续再定。

生成字号不等于字体行高。接入时检查 `line_height` 与标签高度，尤其是 24 px 主文本框和 27 px 指标数值框；同时检查来源名称 63 px 宽能否容纳 `CURSOR`，以及 `100%` 的宽度。字号、字重和布局最终以 LCD 实测为准。


## 字体实测与标题对齐更新

- 用户反馈字体组合显示效果不错，主信息选用 20 px 思源黑体 Bold；其余字体绑定采用上述首轮建议。
- CODEX / CURSOR 使用的 Roboto Condensed Bold 18 px 字库实际 `line_height=15`、`base_line=2`，大写字形高 13 px、纵向偏移为 0。
- 标题起始 Y 从 13 调整为 17，下移 4 px，使大写字形与位于 `y=18`、高 11 px 的圆点垂直居中。此调整针对当前字体字形度量，对齐效果待实机复核；后续更换字体时重新检查。


## 指标区收窄与主信息放大试验

- 用户反馈右侧留白偏多，现将右侧分隔线由 x=287 移到 x=324，右侧整体宽度收窄至 104 px，与左侧接近；中间主次文本宽度由 160 增至 197 px。
- 右侧分隔线在 x=304，指标区从 x=316 开始、宽 108 px。标题 `TOKEN`/`QUOTA` 和主数值 `131k`/`65%` 都使用整列宽度；Codex 的 `5H` 仍叠在数值右侧，Cursor 不再显示附加文字。进度条保持在 y=54。
- 主信息已接入 24 px 思源黑体 Bold。根据实机反馈，主文本标签从 y=8 下移 3 px 至 y=11、高 30 px；次文本仍从 y=41 开始，两标签区域不重叠，对齐效果待实机复核。
- 当前 `SNF_UI_FONT_PRIMARY` 已绑定 `LV_FONT_USE_HANSANS_BOLD_24`。


## 动态中文字符集准备

按用户缩减生僻字的要求，主信息和次信息先使用 GB2312 一级汉字的 3755 字，删除二级汉字的 3008 字，并加入 ASCII 和常见中文标点；状态信息继续使用固定文案字库。主信息生成 Bold 24 px，次信息生成 Regular 12 px，实际资源体积生成后评估。传输及文本缓冲区仍使用 UTF-8，GB2312 仅用于选择字符集合。

已生成 [GB2312 汉字清单](gb2312-hanzi-3755.txt)，可全文复制到 Font Converter 的 Symbols。该文件为 UTF-8 无 BOM，使用 Python 标准库 `gb2312` 编解码器遍历汉字区字节范围 B0–D7 / A1–FE，跳过未分配码位，已校验共 3755 个不重复汉字。清单不含 ASCII 和中文标点，需在转换器另行加入；ASCII 可使用 Unicode 范围 `0x20-0x7E`。不要将 GB2312 字节区间直接填入 Unicode Range。

一级汉字并非严格按本产品使用频率筛选，仍可能含少量不常见字，也不能覆盖全部动态摘要；后续遇到实际缺字时按需补入。


## Flash 受限后的精简字集试验

用户反馈 3755 字方案无法装入 Flash。当前生成的 Bold 24 px 字库为 4 bpp 未压缩，仅 `glyph_bitmap` 就有 1,038,721 字节（约 1014 KiB），不含索引等其他数据；C 源文件大小不能作为实际 Flash 占用。

下一轮使用 [UI 场景精简字集](ui-hanzi-compact.txt)，共 599 个不重复汉字。该集合按 AI 编程摘要、文件操作、任务状态、网络及设备相关词语人工选字，并补充当前 UI 源码字符串中的汉字；不是权威字频榜，也不保证任意中文摘要完整显示。原 3755 字清单保留作补字参考。

将精简清单复制到 Font Converter 的 Symbols，另加 ASCII `0x20-0x7E` 与常用中文标点，分别生成主信息 Bold 24 px 和次信息 Regular 12 px。先保持现有生成配置以对比字符缩减效果，重新构建后确认能否放入目标分区；最终大小还取决于字形、索引及其他固件内容。

本次仅生成精简字符清单，尚未替换固件字库。若仍超出空间，需根据构建时剩余 Flash 预算继续缩减，或评估较低 bpp / 字体压缩及其实际显示效果。后续 PC 摘要措辞可尽量采用常用词，遇到缺字按需补入。
