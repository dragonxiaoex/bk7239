/**
 * @file    sonoff_task_def.h
 * @brief   通用任务配置定义
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date:   2026-08-25
 * 
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 * 
 */
#ifndef __SONOFF_TASK_DEF_H__
#define __SONOFF_TASK_DEF_H__

#include "FreeRTOSConfig.h"
#include "os/os.h"

typedef enum {
    TASK_PRIORITY_IDLE = 0,                                 /* lowest, special for idle task */
    /* User task priority begin */
    TASK_PRIORITY_LOW = BEKEN_DEFAULT_WORKER_PRIORITY,      /* low */
    TASK_PRIORITY_NORMAL,                                   /* normal */
    TASK_PRIORITY_HIGH,                                     /* high */
    /* User task priority end */
    /*Be careful, the max-priority number can not be bigger than configMAX_PRIORITIES - 1, or kernel will crash!!! */
    TASK_PRIORITY_TIMER = configMAX_PRIORITIES - 1,         /* highest, special for timer task to keep time accuracy */
} task_priority_type_t;

#define SONOFF_WIFI_TASK_NAME                       "snf_wifi"
#define SONOFF_WIFI_TASK_STACKSIZE                  (4*1024)
#define SONOFF_WIFI_TASK_PRIO                       TASK_PRIORITY_LOW

#endif