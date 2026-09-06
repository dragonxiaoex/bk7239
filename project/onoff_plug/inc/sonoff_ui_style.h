/**
 * @file    sonoff_ui_style.h
 * @brief   UI布局常量与LVGL样式
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_UI_STYLE_H__
#define __SONOFF_UI_STYLE_H__

#include "lvgl.h"

/**
 * @brief 屏幕与代理行尺寸, 单位为像素.
 */
#define SNF_UI_SCREEN_WIDTH             (428)                           /* 屏幕宽度 */
#define SNF_UI_SCREEN_HEIGHT            (142)                           /* 屏幕高度 */
#define SNF_UI_AGENT_ROW_HEIGHT         (SNF_UI_SCREEN_HEIGHT / 2)      /* 单行高度 */

/**
 * @brief 428x142像素布局坐标.
 */
#define SNF_UI_SOURCE_DIVIDER_X         (104)       /* 来源区分隔线X */
#define SNF_UI_METRIC_DIVIDER_X         (304)       /* 指标区分隔线X */
#define SNF_UI_INFO_X                   (117)       /* 信息区X */
#define SNF_UI_INFO_WIDTH               (177)       /* 信息区宽度 */
#define SNF_UI_METRIC_X                 (316)       /* 指标区X */
#define SNF_UI_METRIC_WIDTH             (108)       /* 指标区宽度 */

/**
 * @brief UI配色.
 */
#define SNF_UI_COLOR_BACKGROUND         (lv_color_hex(0x0B0F14))    /* 背景 */
#define SNF_UI_COLOR_PRIMARY            (lv_color_hex(0xF0F4F8))    /* 主文本 */
#define SNF_UI_COLOR_SECONDARY          (lv_color_hex(0x87919D))    /* 次文本 */
#define SNF_UI_COLOR_CODEX              (lv_color_hex(0x38C4D8))    /* Codex来源色 */
#define SNF_UI_COLOR_CURSOR             (lv_color_hex(0xA98BFF))    /* Cursor来源色 */
#define SNF_UI_COLOR_IDLE               (lv_color_hex(0x58C98B))    /* 空闲 */
#define SNF_UI_COLOR_RUNNING            (lv_color_hex(0xF06A6A))    /* 运行中 */
#define SNF_UI_COLOR_WAITING            (lv_color_hex(0xE4B84A))    /* 等待 */
#define SNF_UI_COLOR_ERROR              (lv_color_hex(0xF06A6A))    /* 出错 */
#define SNF_UI_COLOR_OFFLINE            (lv_color_hex(0x66717D))    /* 离线 */
#define SNF_UI_COLOR_DIVIDER            (lv_color_hex(0x252B33))    /* 分隔线 */
#define SNF_UI_COLOR_BAR_TRACK          (lv_color_hex(0x2B3038))    /* 进度条轨道 */

/**
 * @brief 默认字体.
 *
 * LVGL默认字体通常不含中文。可在包含本文件之前覆盖这些宏。
 */
#ifndef SNF_UI_FONT_PRIMARY
#define SNF_UI_FONT_PRIMARY             LV_FONT_USE_HANSANS_BOLD_24
#endif

#ifndef SNF_UI_FONT_SOURCE
#define SNF_UI_FONT_SOURCE              LV_FONT_USE_ROBOTO_BOLD_18
#endif

#ifndef SNF_UI_FONT_STATE
#define SNF_UI_FONT_STATE               LV_FONT_USE_HANSANS_MEDIUM_12
#endif

#ifndef SNF_UI_FONT_SECONDARY
#define SNF_UI_FONT_SECONDARY           LV_FONT_USE_HANSANS_REGULAR_12
#endif

#ifndef SNF_UI_FONT_METRIC_NAME
#define SNF_UI_FONT_METRIC_NAME         LV_FONT_USE_ROBOTO_REGULAR_12
#endif

#ifndef SNF_UI_FONT_METRIC_VALUE
#define SNF_UI_FONT_METRIC_VALUE        LV_FONT_USE_ROBOTO_BOLD_24
#endif

#ifndef SNF_UI_FONT_METRIC_EXTRA
#define SNF_UI_FONT_METRIC_EXTRA        LV_FONT_USE_ROBOTO_REGULAR_12
#endif

/**
 * @brief UI样式集合.
 */
typedef struct
{
    lv_style_t screen;          /**< 屏幕背景. */
    lv_style_t row;             /**< 代理行容器. */
    lv_style_t source_name;     /**< 来源名称. */
    lv_style_t state;           /**< 运行状态. */
    lv_style_t primary;         /**< 主文本. */
    lv_style_t secondary;       /**< 次文本. */
    lv_style_t metric_name;     /**< 指标名称. */
    lv_style_t metric_value;    /**< 指标数值. */
    lv_style_t metric_extra;    /**< 指标附加文本. */
    lv_style_t divider;         /**< 分隔线. */
    lv_style_t bar_track;       /**< 进度条轨道. */
} SnfUiStyles;

/**
 * @brief 初始化UI样式.
 *
 * 可重复调用, 仅首次生效.
 */
void snfUiStyleInit(void);

/**
 * @brief 获取UI样式集合.
 *
 * 若尚未初始化, 会先完成初始化.
 *
 * @return 样式集合指针.
 */
const SnfUiStyles *snfUiStyleGet(void);

#endif /* #ifndef __SONOFF_UI_STYLE_H__ */
