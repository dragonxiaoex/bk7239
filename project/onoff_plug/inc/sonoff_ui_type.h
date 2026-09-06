/**
 * @file    sonoff_ui_type.h
 * @brief   UI公共数据类型
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_UI_TYPE_H__
#define __SONOFF_UI_TYPE_H__

#include <stdint.h>

/**
 * @brief 代理行文本缓冲区容量.
 */
#define SNF_UI_PRIMARY_TEXT_CAPACITY    (96)        /* 主文本容量 */
#define SNF_UI_SECONDARY_TEXT_CAPACITY  (96)        /* 次文本容量 */
#define SNF_UI_METRIC_VALUE_CAPACITY    (16)        /* 指标主数值容量 */
#define SNF_UI_METRIC_EXTRA_CAPACITY    (24)        /* 指标附加文本容量 */

/**
 * @brief 百分比取值约定.
 */
#define SNF_UI_PERCENT_MAX              (100)       /* 百分比上限 */
#define SNF_UI_PERCENT_NONE             (UINT8_MAX) /* 当前无可显示百分比 */

/**
 * @brief 代理运行状态.
 */
typedef enum
{
    SNF_UI_AI_IDLE = 0,     /**< 空闲. */
    SNF_UI_AI_RUNNING,      /**< 运行中. */
    SNF_UI_AI_WAITING,      /**< 等待批准. */
    SNF_UI_AI_ERROR,        /**< 出错. */
    SNF_UI_AI_OFFLINE,      /**< 离线. */
} SnfUiAiState;

/**
 * @brief 单个代理的显示数据.
 *
 * 文本由结构体自身持有。生产者把该结构体发送到队列后，可以立即复用自己的缓冲区。
 */
typedef struct
{
    SnfUiAiState state;                                     /**< 代理运行状态. */
    char primary_text[SNF_UI_PRIMARY_TEXT_CAPACITY];        /**< 主文本. */
    char secondary_text[SNF_UI_SECONDARY_TEXT_CAPACITY];    /**< 次文本. */
    uint8_t metric_percent;                                 /**< 进度条百分比, SNF_UI_PERCENT_NONE表示清空. */
    char metric_value[SNF_UI_METRIC_VALUE_CAPACITY];        /**< 指标主数值, 如65%或128k. */
    char metric_extra[SNF_UI_METRIC_EXTRA_CAPACITY];        /**< 指标附加文本. */
} SnfUiAgentData;

/**
 * @brief 整屏状态快照.
 */
typedef struct
{
    SnfUiAgentData codex;   /**< Codex行数据. */
    SnfUiAgentData cursor;  /**< Cursor行数据. */
} SnfUiStatusData;

#endif /* #ifndef __SONOFF_UI_TYPE_H__ */
