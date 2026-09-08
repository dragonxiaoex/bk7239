/**
 * @file    sonoff_factory.c
 * @brief   产测模块
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date:   2026-09-08
 * 
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 * 
 */
#include "stdint.h"

#include "FreeRTOS.h"
#include "task.h"

#include "sonoff_factory.h"
#include "sonoff_log.h"
#include "sonoff_task_def.h"

#define SONOFF_FACTORY_TASK_NAME                "snf_factory"
#define SONOFF_FACTORY_TASK_STACKSIZE           (1024 * 1)
#define SONOFF_FACTORY_TASK_PRIO                TASK_PRIORITY_NORMAL

static const char *tag = "SNF-FACTORY";

static TaskHandle_t factory_task_handle = NULL;

static void snfFactoryTask(void *arg)
{
    while(1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void snfFactoryModeStart(void)
{
    LOG_RAW("factory mode\r\n");
    vTaskDelay(20 / portTICK_PERIOD_MS);
    LOG_RAW("factory mode\r\n");
    vTaskDelay(20 / portTICK_PERIOD_MS);
    LOG_RAW("factory mode\r\n");

    if (factory_task_handle == NULL)
    {
        if (xTaskCreate(snfFactoryTask,
                        SONOFF_FACTORY_TASK_NAME,
                        SONOFF_FACTORY_TASK_STACKSIZE,
                        NULL,
                        SONOFF_FACTORY_TASK_PRIO,
                        &factory_task_handle) != pdPASS)
        {
            LOG_E(tag, "factory task init failed");
            return;
        }
    }
}