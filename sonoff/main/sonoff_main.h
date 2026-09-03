/**
 * @file    sonoff_main.h
 * @brief   主任务
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_MAIN_SONOFF_MAIN_H__
#define __SONOFF_MAIN_SONOFF_MAIN_H__

/**
 * @brief 主任务事件标识.
 */
typedef enum
{
    SNF_MAIN_EVT_AP_READY = 0, /* AP已就绪 */
} SnfMainEventId;

/**
 * @brief 主任务事件.
 */
typedef struct
{
    int id;           /* 事件标识 */
    const void *data; /* 事件数据 */
} SnfMainEvent;

/**
 * @brief 发送主任务事件.
 *
 * @param [in] id - 事件标识.
 * @param [in] data - 事件数据.
 * @return 0表示成功, 负数表示失败.
 */
int snfMainEventSend(int id, const void *data);

/**
 * @brief 初始化主任务.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfMainInit(void);

#endif /* __SONOFF_MAIN_SONOFF_MAIN_H__ */
