/**
 * @file    FreeRTOS.h
 * @brief   OTA主机测试的同步队列替身
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __FREERTOS_H__
#define __FREERTOS_H__

#include <stddef.h>
#include <stdint.h>

#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(value) (value)

typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef uint32_t TickType_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
int32_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks);
int32_t xSemaphoreGive(SemaphoreHandle_t mutex);
QueueHandle_t xQueueCreate(uint32_t length, size_t size);
int32_t xQueueSend(QueueHandle_t queue, const void *event, TickType_t ticks);
int32_t xQueueReceive(QueueHandle_t queue, void *event, TickType_t ticks);
void vQueueDelete(QueueHandle_t queue);
int32_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack,
                    void *argument, uint32_t priority, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t handle);

#endif /* __FREERTOS_H__ */
