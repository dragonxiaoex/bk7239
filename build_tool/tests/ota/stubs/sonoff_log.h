/**
 * @file    sonoff_log.h
 * @brief   OTA主机测试接口替身
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_LOG_H__
#define __SONOFF_LOG_H__

#include <assert.h>
#define LOG_I(tag, ...) do { assert((tag) != NULL); } while (0)
#define LOG_W(tag, ...) do { assert((tag) != NULL); } while (0)

#endif /* __SONOFF_LOG_H__ */
