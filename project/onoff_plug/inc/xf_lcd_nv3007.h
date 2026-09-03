/**
 * @file    xf_lcd_nv3007.h
 * @brief   NV3007 LCD驱动
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-31
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __PROJECT_ONOFF_PLUG_XF_LCD_NV3007_H__
#define __PROJECT_ONOFF_PLUG_XF_LCD_NV3007_H__

#include <stdint.h>

/**
 * @brief 初始化LCD.
 *
 * @return 0表示成功, 非0表示失败.
 */
int xf_lcd_init(void);

/**
 * @brief 控制LCD背光.
 *
 * @param [in] onoff - 0关闭背光, 非0打开背光.
 */
void xf_lcd_bl_output(uint8_t onoff);

/**
 * @brief 显示全屏图像.
 *
 * @param [in] data - RGB565图像数据.
 */
void xf_lcd_display_full(const uint8_t *data);

/**
 * @brief 显示纯色画面.
 *
 * @param [in] color - RGB565颜色.
 */
void xf_lcd_full_color(uint16_t color);

/**
 * @brief 显示指定区域图像.
 *
 * @param [in] x_start - 起始X坐标.
 * @param [in] x_end - 结束X坐标.
 * @param [in] y_start - 起始Y坐标.
 * @param [in] y_end - 结束Y坐标.
 * @param [in] data - RGB565图像数据.
 */
void xf_lcd_display_part(uint16_t x_start,
                         uint16_t x_end,
                         uint16_t y_start,
                         uint16_t y_end,
                         const uint8_t *data);

#endif /* __PROJECT_ONOFF_PLUG_XF_LCD_NV3007_H__ */
