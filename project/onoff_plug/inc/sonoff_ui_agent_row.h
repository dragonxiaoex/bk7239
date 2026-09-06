/**
 * @file    sonoff_ui_agent_row.h
 * @brief   代理状态行控件
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_UI_AGENT_ROW_H__
#define __SONOFF_UI_AGENT_ROW_H__

#include "lvgl.h"

#include "sonoff_ui_type.h"

/**
 * @brief 代理状态行对象.
 */
typedef struct
{
    lv_obj_t *root;             /**< 行根对象. */

    /* 来源区 */
    lv_obj_t *accent_line;      /**< 来源色条. */
    lv_obj_t *source_dot;       /**< 来源色点. */
    lv_obj_t *source_name;      /**< 来源名称. */
    lv_obj_t *state_label;      /**< 运行状态. */
    lv_obj_t *source_divider;   /**< 来源区分隔线. */

    /* 信息区 */
    lv_obj_t *primary_label;    /**< 主文本. */
    lv_obj_t *secondary_label;  /**< 次文本. */
    lv_obj_t *metric_divider;   /**< 指标区分隔线. */

    /* 指标区 */
    lv_obj_t *metric_name;      /**< 指标名称. */
    lv_obj_t *metric_value;     /**< 指标数值. */
    lv_obj_t *metric_extra;     /**< 指标附加文本. */
    lv_obj_t *metric_bar;       /**< 指标进度条. */
    lv_obj_t *bottom_divider;   /**< 行底部分隔线. */
} SnfUiAgentRow;

/**
 * @brief 代理状态行创建配置.
 */
typedef struct
{
    const char *source_name;    /**< 来源名称, 可为NULL. */
    const char *metric_name;    /**< 指标名称, 可为NULL. */
    lv_color_t source_color;    /**< 来源强调色. */
} SnfUiAgentRowConfig;

/**
 * @brief 在父对象中创建代理状态行.
 *
 * @param [out] row - 行对象, 由本函数填充控件句柄.
 * @param [in] parent - 父对象.
 * @param [in] config - 行创建配置.
 */
void snfUiAgentRowCreate(SnfUiAgentRow *row, lv_obj_t *parent, const SnfUiAgentRowConfig *config);

/**
 * @brief 用最新数据刷新代理状态行.
 *
 * @param [in,out] row - 已创建的行对象.
 * @param [in] data - 显示数据, 文本在调用期间必须有效.
 */
void snfUiAgentRowUpdate(SnfUiAgentRow *row, const SnfUiAgentData *data);

#endif /* #ifndef __SONOFF_UI_AGENT_ROW_H__ */
