/**
 * @file    sonoff_main.c
 * @brief   主任务
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "sonoff_cli.h"
#include "sonoff_http.h"
#include "sonoff_log.h"
#include "sonoff_main.h"
#include "sonoff_net.h"
#include "sonoff_nvdm.h"
#include "sonoff_task_def.h"
#include "sonoff_net_test.h"

/** @brief 主任务日志标签. */
static const char *tag = "SNF-MAIN";

/** @brief 主任务事件队列长度. */
#define SONOFF_MAIN_EVENT_QUEUE_SIZE (10U)

/**
 * @brief 主任务运行状态.
 */
typedef struct
{
    TaskHandle_t task_handle;  /* 主任务句柄 */
    QueueHandle_t event_queue; /* 主任务事件队列 */
} SnfMainState;

/** @brief 主任务运行状态实例. */
static SnfMainState main_state_data = {
    .task_handle = NULL,
    .event_queue = NULL,
};

int snfMainEventSend(int id, const void *data)
{
    SnfMainState *main_state = &main_state_data;
    SnfMainEvent event = {0};
    int ret = 0;

    event.id = id;
    event.data = data;

    if (main_state->event_queue == NULL)
    {
        LOG_E(tag, "event queue is not initialized");
        ret = -1;
    }
    else if (xQueueSend(main_state->event_queue, &event, portMAX_DELAY) != pdPASS)
    {
        LOG_E(tag, "event send failed");
        ret = -2;
    }

    return ret;
}

/**
 * @brief 接收网络管理层事件并转换为主任务事件.
 *
 * @param [in] state - 网络状态.
 * @param [in] data - 事件数据.
 */
static void netEventCallback(SnfNetState state, const void *data)
{
    (void)data;

    if (state == SNF_NET_STATE_AP_READY)
    {
        if (snfMainEventSend(SNF_MAIN_EVT_AP_READY, NULL) != 0)
        {
            LOG_E(tag, "send AP ready event failed");
        }
    }
}

/**
 * @brief 执行主任务事件.
 *
 * @param [in] arg - 任务参数.
 */
static void snfMainTask(void *arg)
{
    SnfMainState *main_state = &main_state_data;

    while (1)
    {
        SnfMainEvent event = {0};

        if (xQueueReceive(main_state->event_queue, &event, portMAX_DELAY) == pdPASS)
        {
            LOG_I(tag, "event id: %d", event.id);

            switch (event.id)
            {
                case SNF_MAIN_EVT_AP_READY:
                    if (snfHttpServerStart() != 0)
                    {
                        LOG_E(tag, "http server start failed");
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

int snfMainInit(void)
{
    SnfMainState *main_state = &main_state_data;
    int ret;

    ret = snfNvdmInit();
    if (ret != 0)
    {
        LOG_E(tag, "nvdm init failed, ret=%d", ret);
        return -1;
    }

    ret = snfCliInit();
    if (ret != 0)
    {
        LOG_E(tag, "cli init failed, ret=%d", ret);
        return -2;
    }

    if (main_state->event_queue == NULL)
    {
        main_state->event_queue = xQueueCreate(SONOFF_MAIN_EVENT_QUEUE_SIZE,
                                                sizeof(SnfMainEvent));
        if (main_state->event_queue == NULL)
        {
            LOG_E(tag, "event queue init failed");
            return -3;
        }
    }

    if (main_state->task_handle == NULL)
    {
        if (xTaskCreate(snfMainTask,
                        SONOFF_MAIN_TASK_NAME,
                        SONOFF_MAIN_TASK_STACKSIZE,
                        NULL,
                        SONOFF_MAIN_TASK_PRIO,
                        &main_state->task_handle) != pdPASS)
        {
            LOG_E(tag, "main task init failed");
            return -4;
        }
    }

    ret = snfNetRegisterEventCallback(netEventCallback);
    if (ret != 0)
    {
        LOG_E(tag, "network event callback register failed, ret=%d", ret);
        return -5;
    }

    ret = snfNetInit();
    if (ret != 0)
    {
        LOG_E(tag, "network manager init failed, ret=%d", ret);
        return -6;
    }

    /* only for net test */
    snfNetTestInit();

    return 0;
}
