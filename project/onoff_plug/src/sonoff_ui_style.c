/**
 * @file    sonoff_ui_style.c
 * @brief   UI布局常量与LVGL样式
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdbool.h>

#include "sonoff_ui_style.h"

static SnfUiStyles ui_styles = {0};
static bool ui_styles_ready = false;

/**
 * @brief 初始化标签样式.
 *
 * @param [out] style - 待初始化的样式.
 * @param [in] color - 文本颜色.
 * @param [in] font - 文本字体.
 */
static void styleInitLabel(lv_style_t *style, lv_color_t color, const lv_font_t *font)
{
    lv_style_init(style);
    lv_style_set_text_color(style, color);
    lv_style_set_text_font(style, font);
    lv_style_set_pad_all(style, 0);
}

void snfUiStyleInit(void)
{
    SnfUiStyles *styles = &ui_styles;

    if (ui_styles_ready)
    {
        return;
    }

    lv_style_init(&styles->screen);
    lv_style_set_bg_color(&styles->screen, SNF_UI_COLOR_BACKGROUND);
    lv_style_set_bg_opa(&styles->screen, LV_OPA_COVER);
    lv_style_set_border_width(&styles->screen, 0);
    lv_style_set_pad_all(&styles->screen, 0);
    lv_style_set_radius(&styles->screen, 0);

    lv_style_init(&styles->row);
    lv_style_set_bg_opa(&styles->row, LV_OPA_TRANSP);
    lv_style_set_border_width(&styles->row, 0);
    lv_style_set_pad_all(&styles->row, 0);
    lv_style_set_radius(&styles->row, 0);

    styleInitLabel(&styles->source_name, SNF_UI_COLOR_PRIMARY, SNF_UI_FONT_SOURCE);
    styleInitLabel(&styles->state, SNF_UI_COLOR_IDLE, SNF_UI_FONT_STATE);
    styleInitLabel(&styles->primary, SNF_UI_COLOR_PRIMARY, SNF_UI_FONT_PRIMARY);
    styleInitLabel(&styles->secondary, SNF_UI_COLOR_SECONDARY, SNF_UI_FONT_SECONDARY);
    styleInitLabel(&styles->metric_name, SNF_UI_COLOR_SECONDARY, SNF_UI_FONT_METRIC_NAME);
    styleInitLabel(&styles->metric_value, SNF_UI_COLOR_PRIMARY, SNF_UI_FONT_METRIC_VALUE);
    styleInitLabel(&styles->metric_extra, SNF_UI_COLOR_SECONDARY, SNF_UI_FONT_METRIC_EXTRA);
    lv_style_set_text_align(&styles->metric_extra, LV_TEXT_ALIGN_RIGHT);

    lv_style_init(&styles->divider);
    lv_style_set_bg_color(&styles->divider, SNF_UI_COLOR_DIVIDER);
    lv_style_set_bg_opa(&styles->divider, LV_OPA_COVER);
    lv_style_set_border_width(&styles->divider, 0);
    lv_style_set_radius(&styles->divider, 0);

    lv_style_init(&styles->bar_track);
    lv_style_set_bg_color(&styles->bar_track, SNF_UI_COLOR_BAR_TRACK);
    lv_style_set_bg_opa(&styles->bar_track, LV_OPA_COVER);
    lv_style_set_border_width(&styles->bar_track, 0);
    lv_style_set_pad_all(&styles->bar_track, 0);
    lv_style_set_radius(&styles->bar_track, LV_RADIUS_CIRCLE);

    ui_styles_ready = true;
}

const SnfUiStyles *snfUiStyleGet(void)
{
    snfUiStyleInit();

    return &ui_styles;
}
