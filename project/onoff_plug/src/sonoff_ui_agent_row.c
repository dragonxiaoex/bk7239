/**
 * @file    sonoff_ui_agent_row.c
 * @brief   代理状态行控件
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>

#include "sonoff_ui_agent_row.h"
#include "sonoff_ui_style.h"

/**
 * @brief 创建固定位置的矩形对象.
 *
 * @param [in] parent - 父对象.
 * @param [in] x - X坐标, 单位为像素.
 * @param [in] y - Y坐标, 单位为像素.
 * @param [in] width - 宽度, 单位为像素.
 * @param [in] height - 高度, 单位为像素.
 * @param [in] style - 可选样式, 为NULL时不添加样式.
 * @return 创建的对象.
 */
static lv_obj_t *agentRowCreateBox(lv_obj_t *parent,
                                   int32_t x,
                                   int32_t y,
                                   int32_t width,
                                   int32_t height,
                                   const lv_style_t *style)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_style_all(obj);
    if (style != NULL)
    {
        lv_obj_add_style(obj, style, LV_PART_MAIN);
    }
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

/**
 * @brief 创建固定位置的标签.
 *
 * @param [in] parent - 父对象.
 * @param [in] x - X坐标, 单位为像素.
 * @param [in] y - Y坐标, 单位为像素.
 * @param [in] width - 宽度, 单位为像素.
 * @param [in] height - 高度, 单位为像素.
 * @param [in] style - 标签样式.
 * @param [in] long_mode - 长文本显示模式.
 * @return 创建的标签.
 */
static lv_obj_t *agentRowCreateLabel(lv_obj_t *parent,
                                     int32_t x,
                                     int32_t y,
                                     int32_t width,
                                     int32_t height,
                                     const lv_style_t *style,
                                     lv_label_long_mode_t long_mode)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_remove_style_all(label);
    lv_obj_add_style(label, style, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_label_set_long_mode(label, long_mode);
    lv_label_set_text(label, "");

    return label;
}

/**
 * @brief 按运行状态刷新状态标签.
 *
 * @param [in,out] row - 已创建的行对象.
 * @param [in] state - 代理运行状态.
 */
static void agentRowSetState(SnfUiAgentRow *row, SnfUiAiState state)
{
    switch (state)
    {
        case SNF_UI_AI_IDLE:
        {
            lv_label_set_text(row->state_label, "摸鱼中...");
            lv_obj_set_style_text_color(row->state_label, SNF_UI_COLOR_IDLE, LV_PART_MAIN);
            break;
        }
        case SNF_UI_AI_RUNNING:
        {
            lv_label_set_text(row->state_label, "摘棉花中...");
            lv_obj_set_style_text_color(row->state_label, SNF_UI_COLOR_RUNNING, LV_PART_MAIN);
            break;
        }
        case SNF_UI_AI_WAITING:
        {
            lv_label_set_text(row->state_label, "等待批准...");
            lv_obj_set_style_text_color(row->state_label, SNF_UI_COLOR_WAITING, LV_PART_MAIN);
            break;
        }
        case SNF_UI_AI_ERROR:
        {
            lv_label_set_text(row->state_label, "出错了...");
            lv_obj_set_style_text_color(row->state_label, SNF_UI_COLOR_ERROR, LV_PART_MAIN);
            break;
        }
        case SNF_UI_AI_OFFLINE:
        {
            lv_label_set_text(row->state_label, "已离线");
            lv_obj_set_style_text_color(row->state_label, SNF_UI_COLOR_OFFLINE, LV_PART_MAIN);
            break;
        }
        default:
        {
            lv_label_set_text(row->state_label, "状态未知");
            lv_obj_set_style_text_color(row->state_label, SNF_UI_COLOR_OFFLINE, LV_PART_MAIN);
            break;
        }
    }
}

void snfUiAgentRowCreate(SnfUiAgentRow *row, lv_obj_t *parent, const SnfUiAgentRowConfig *config)
{
    const SnfUiStyles *styles;

    if ((row == NULL) || (parent == NULL) || (config == NULL))
    {
        return;
    }

    styles = snfUiStyleGet();

    row->root = agentRowCreateBox(parent,
                                  0,
                                  0,
                                  SNF_UI_SCREEN_WIDTH,
                                  SNF_UI_AGENT_ROW_HEIGHT,
                                  &styles->row);

    row->accent_line = agentRowCreateBox(row->root, 5, 5, 4, 61, NULL);
    lv_obj_set_style_bg_color(row->accent_line, config->source_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row->accent_line, LV_OPA_COVER, LV_PART_MAIN);

    row->source_dot = agentRowCreateBox(row->root, 18, 18, 11, 11, NULL);
    lv_obj_set_style_bg_color(row->source_dot, config->source_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row->source_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row->source_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    /* 当前18 px标题字体的大写字形高13 px, 在y=17处与y=18、高11 px的圆点居中对齐. */
    row->source_name = agentRowCreateLabel(row->root,
                                           35,
                                           17,
                                           63,
                                           22,
                                           &styles->source_name,
                                           LV_LABEL_LONG_CLIP);
    lv_label_set_text(row->source_name, (config->source_name != NULL) ? config->source_name : "");
    lv_obj_set_style_text_color(row->source_name, config->source_color, LV_PART_MAIN);

    row->state_label = agentRowCreateLabel(row->root,
                                           18,
                                           42,
                                           80,
                                           18,
                                           &styles->state,
                                           LV_LABEL_LONG_DOT);

    row->source_divider = agentRowCreateBox(row->root,
                                            SNF_UI_SOURCE_DIVIDER_X,
                                            6,
                                            1,
                                            59,
                                            &styles->divider);

    row->primary_label = agentRowCreateLabel(row->root,
                                             SNF_UI_INFO_X,
                                             11,
                                             SNF_UI_INFO_WIDTH,
                                             30,
                                             &styles->primary,
                                             LV_LABEL_LONG_DOT);

    row->secondary_label = agentRowCreateLabel(row->root,
                                               SNF_UI_INFO_X,
                                               41,
                                               SNF_UI_INFO_WIDTH,
                                               18,
                                               &styles->secondary,
                                               LV_LABEL_LONG_DOT);

    row->metric_divider = agentRowCreateBox(row->root,
                                            SNF_UI_METRIC_DIVIDER_X,
                                            6,
                                            1,
                                            59,
                                            &styles->divider);

    row->metric_name = agentRowCreateLabel(row->root,
                                           SNF_UI_METRIC_X,
                                           6,
                                           SNF_UI_METRIC_WIDTH,
                                           17,
                                           &styles->metric_name,
                                           LV_LABEL_LONG_CLIP);
    lv_label_set_text(row->metric_name, (config->metric_name != NULL) ? config->metric_name : "");

    row->metric_value = agentRowCreateLabel(row->root,
                                            SNF_UI_METRIC_X,
                                            22,
                                            SNF_UI_METRIC_WIDTH,
                                            27,
                                            &styles->metric_value,
                                            LV_LABEL_LONG_CLIP);

    row->metric_extra = agentRowCreateLabel(row->root,
                                            SNF_UI_METRIC_X,
                                            29,
                                            SNF_UI_METRIC_WIDTH,
                                            18,
                                            &styles->metric_extra,
                                            LV_LABEL_LONG_CLIP);

    row->metric_bar = lv_bar_create(row->root);
    lv_obj_remove_style_all(row->metric_bar);
    lv_obj_add_style(row->metric_bar, &styles->bar_track, LV_PART_MAIN);
    lv_obj_set_pos(row->metric_bar, SNF_UI_METRIC_X, 54);
    lv_obj_set_size(row->metric_bar, SNF_UI_METRIC_WIDTH, 7);
    lv_obj_set_style_bg_color(row->metric_bar, config->source_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(row->metric_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(row->metric_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_bar_set_range(row->metric_bar, 0, SNF_UI_PERCENT_MAX);
    lv_bar_set_value(row->metric_bar, 0, LV_ANIM_OFF);

    row->bottom_divider = agentRowCreateBox(row->root,
                                            0,
                                            SNF_UI_AGENT_ROW_HEIGHT - 1,
                                            SNF_UI_SCREEN_WIDTH,
                                            1,
                                            &styles->divider);
}

void snfUiAgentRowUpdate(SnfUiAgentRow *row, const SnfUiAgentData *data)
{
    if ((row == NULL) || (row->root == NULL) || (data == NULL))
    {
        return;
    }

    lv_label_set_text(row->primary_label, data->primary_text);
    lv_label_set_text(row->secondary_label, data->secondary_text);
    lv_label_set_text(row->metric_value,
                      (data->metric_value[0] != '\0') ? data->metric_value : "--");
    lv_label_set_text(row->metric_extra, data->metric_extra);

    if (data->metric_percent == SNF_UI_PERCENT_NONE)
    {
        lv_bar_set_value(row->metric_bar, 0, LV_ANIM_OFF);
    }
    else
    {
        uint8_t percent = (data->metric_percent > SNF_UI_PERCENT_MAX)
                          ? SNF_UI_PERCENT_MAX
                          : data->metric_percent;

        lv_bar_set_value(row->metric_bar, percent, LV_ANIM_OFF);
    }

    agentRowSetState(row, data->state);
}
