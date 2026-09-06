/**
 * @file    sonoff_ui_handle.h
 * @brief   UI任务与状态刷新
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_UI_HANDLE_H__
#define __SONOFF_UI_HANDLE_H__

#include <FreeRTOS.h>

#include "sonoff_ui_type.h"

/**
 * @brief 启动UI任务.
 *
 * 必须在lv_init()、显示驱动注册和LVGL tick配置完成后调用。
 * UI任务拥有整个活动屏幕, 并负责全部对象创建、刷新和lv_timer_handler()。
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfUiHandleStart(void);

/**
 * @brief 提交一份完整状态快照.
 *
 * 按值复制。队列长度固定为1, 新状态覆盖尚未消费的旧状态。
 *
 * @param [in] data - 整屏状态快照.
 * @return 0表示成功, 负数表示失败.
 */
int snfUiHandleSubmit(const SnfUiStatusData *data);

/**
 * @brief 在中断中提交一份完整状态快照.
 *
 * 按值复制。队列长度固定为1, 新状态覆盖尚未消费的旧状态。
 *
 * @param [in] data - 整屏状态快照.
 * @param [in,out] task_woken - FreeRTOS任务切换标志.
 * @return 0表示成功, 负数表示失败.
 */
int snfUiHandleSubmitFromIsr(const SnfUiStatusData *data, BaseType_t *task_woken);

/**
 * @brief 查询UI任务是否已启动.
 *
 * @return 非0表示已启动, 0表示未启动.
 */
int snfUiHandleIsStarted(void);

#endif /* #ifndef __SONOFF_UI_HANDLE_H__ */
