/**
 * @file    sonoff_ui_test.c
 * @brief   UI显示测试
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include "sonoff_ui_handle.h"
#include "sonoff_ui_test.h"

static const SnfUiStatusData ui_test_status = {
    .codex = {
        .state = SNF_UI_AI_RUNNING,
        .primary_text = "正在调整界面",
        .secondary_text = "读取 ui_style.c",
        .metric_percent = 65,
        .metric_value = "65%",
        .metric_extra = "5H",
    },
    .cursor = {
        .state = SNF_UI_AI_IDLE,
        .primary_text = "任务已完成",
        .secondary_text = "已更新 3 个文件",
        .metric_percent = 64,
        .metric_value = "131k",
        .metric_extra = "",
    },
};

int32_t snfUiTestSubmit(void)
{

    return snfUiHandleSubmit(&ui_test_status);
}
