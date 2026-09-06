/**
 * @file    sonoff_ui_test.h
 * @brief   UI显示测试
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_UI_TEST_H__
#define __SONOFF_UI_TEST_H__

#include <stdint.h>

/**
 * @brief 提交一份中文UI测试快照.
 *
 * 在snfUiHandleStart()成功后从任务上下文调用，数据按值复制入队。
 * 返回成功仅表示入队成功，画面由UI任务异步刷新。
 * 状态文案仍由行组件根据枚举生成。
 *
 * @return 0表示成功, 负数表示失败.
 */
int32_t snfUiTestSubmit(void);

#endif /* #ifndef __SONOFF_UI_TEST_H__ */
