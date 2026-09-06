/**
 * @file    sonoff_ui_handle.c
 * @brief   UI任务与状态刷新
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "lvgl.h"

#include "sonoff_ui_agent_row.h"
#include "sonoff_ui_handle.h"
#include "sonoff_ui_style.h"
#include "lv_port_disp.h"
#include "xf_lcd_nv3007.h"
#include "sonoff_log.h"
#include "sonoff_task_def.h"
#include "sonoff_bridge_handle.h"

static const char *tag = "snf_ui_handle";

#define SNF_UI_HANDLE_TASK_NAME             "snf_ui"
#define SNF_UI_HANDLE_TASK_STACKSIZE        (4 * 1024)
#define SNF_UI_HANDLE_TASK_PRIO             (TASK_PRIORITY_LOW)
#define SNF_UI_HANDLE_REFRESH_PERIOD_MS     (5)

/**
 * @brief UI任务运行状态.
 */
typedef struct
{
    QueueHandle_t status_queue;
    SnfUiStatusData pending;
    SnfUiAgentRow codex_row;
    SnfUiAgentRow cursor_row;
} SnfUiHandleState;

static SnfUiHandleState ui_handle_state = {
    .status_queue = NULL,
    .codex_row = {0},
    .cursor_row = {0},
};

static const SnfUiStatusData ui_initial_status = {
    .codex = {
        .state = SNF_UI_AI_IDLE,
        .primary_text = "等待新任务",
        .secondary_text = "暂无活动",
        .metric_percent = SNF_UI_PERCENT_NONE,
        .metric_value = "--",
        .metric_extra = "5H",
    },
    .cursor = {
        .state = SNF_UI_AI_IDLE,
        .primary_text = "等待新任务",
        .secondary_text = "暂无活动",
        .metric_percent = SNF_UI_PERCENT_NONE,
        .metric_value = "--",
        .metric_extra = "",
    },
};

/**
 * @brief 把状态快照刷新到两行控件.
 *
 * @param [in] status - 整屏状态快照.
 */
static void handleApplyStatus(const SnfUiStatusData *status)
{
    SnfUiHandleState *handle_state = &ui_handle_state;

    snfUiAgentRowUpdate(&handle_state->codex_row, &status->codex);
    snfUiAgentRowUpdate(&handle_state->cursor_row, &status->cursor);
}

/**
 * @brief 创建并加载两行代理界面屏幕.
 */
static void handleCreatePage(void)
{
    SnfUiHandleState *handle_state = &ui_handle_state;
    const SnfUiStyles *styles = snfUiStyleGet();
    lv_obj_t *screen = lv_obj_create(NULL);
    const SnfUiAgentRowConfig codex_config = {
        .source_name = "CODEX",
        .metric_name = "QUOTA",
        .source_color = SNF_UI_COLOR_CODEX,
    };
    const SnfUiAgentRowConfig cursor_config = {
        .source_name = "CURSOR",
        .metric_name = "TOKEN",
        .source_color = SNF_UI_COLOR_CURSOR,
    };

    if (screen == NULL)
    {
        return;
    }

    lv_obj_remove_style_all(screen);
    lv_obj_add_style(screen, &styles->screen, LV_PART_MAIN);
    lv_obj_set_size(screen, SNF_UI_SCREEN_WIDTH, SNF_UI_SCREEN_HEIGHT);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    snfUiAgentRowCreate(&handle_state->codex_row, screen, &codex_config);
    lv_obj_set_pos(handle_state->codex_row.root, 0, 0);

    snfUiAgentRowCreate(&handle_state->cursor_row, screen, &cursor_config);
    lv_obj_set_pos(handle_state->cursor_row.root, 0, SNF_UI_AGENT_ROW_HEIGHT);

    handleApplyStatus(&ui_initial_status);
    lv_screen_load(screen);
}

/**
 * @brief 向LVGL提供系统毫秒时间.
 *
 * @return 自调度器启动以来的毫秒数.
 */
static uint32_t handleTickGet(void)
{
    return ((uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief UI任务入口.
 *
 * @param [in] arg - FreeRTOS任务参数, 本模块不使用.
 */
static void handleTask(void *arg)
{
    SnfUiHandleState *handle_state = &ui_handle_state;
    TickType_t last_wake;

    last_wake = xTaskGetTickCount();

    lv_init();
    xf_lcd_init();
    lv_tick_set_cb(handleTickGet);
    lv_port_disp_init();
    snfUiStyleInit();
    handleCreatePage();
    LOG_I(tag, "ui task start");

    snfBridgeHandleInit();

    for (;;)
    {
        if (xQueueReceive(handle_state->status_queue, &handle_state->pending, 0) == pdPASS)
        {
            handleApplyStatus(&handle_state->pending);
        }

        /* 本模块约定只有当前任务调用LVGL, 因此不需要额外的应用层互斥锁. */
        lv_timer_handler();

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SNF_UI_HANDLE_REFRESH_PERIOD_MS));
    }
}

int snfUiHandleStart(void)
{
    SnfUiHandleState *handle_state = &ui_handle_state;

    if (handle_state->status_queue != NULL)
    {
        return 0;
    }

    /* xQueueOverwrite()/xQueueOverwriteFromISR()要求队列长度为1. */
    handle_state->status_queue = xQueueCreate(1, sizeof(SnfUiStatusData));
    if (handle_state->status_queue == NULL)
    {
        return -1;
    }

    if (xTaskCreate(handleTask,
                    SNF_UI_HANDLE_TASK_NAME,
                    SNF_UI_HANDLE_TASK_STACKSIZE,
                    NULL,
                    SNF_UI_HANDLE_TASK_PRIO,
                    NULL) != pdPASS)
    {
        vQueueDelete(handle_state->status_queue);
        handle_state->status_queue = NULL;

        return -1;
    }

    return 0;
}

int snfUiHandleSubmit(const SnfUiStatusData *data)
{
    SnfUiHandleState *handle_state = &ui_handle_state;

    if ((data == NULL) || (handle_state->status_queue == NULL))
    {
        return -1;
    }

    if (xQueueOverwrite(handle_state->status_queue, data) != pdPASS)
    {
        return -1;
    }

    return 0;
}

int snfUiHandleSubmitFromIsr(const SnfUiStatusData *data, BaseType_t *task_woken)
{
    SnfUiHandleState *handle_state = &ui_handle_state;

    if ((data == NULL)
        || (handle_state->status_queue == NULL)
        || (task_woken == NULL))
    {
        return -1;
    }

    if (xQueueOverwriteFromISR(handle_state->status_queue, data, task_woken) != pdPASS)
    {
        return -1;
    }

    return 0;
}

int snfUiHandleIsStarted(void)
{
    const SnfUiHandleState *handle_state = &ui_handle_state;

    return (handle_state->status_queue != NULL) ? 1 : 0;
}
