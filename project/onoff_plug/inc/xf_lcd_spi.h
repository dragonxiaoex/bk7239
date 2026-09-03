/**
 * @file    xf_lcd_spi.h
 * @brief   LCD硬件SPI适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-31
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __PROJECT_ONOFF_PLUG_XF_LCD_SPI_H__
#define __PROJECT_ONOFF_PLUG_XF_LCD_SPI_H__

#include <stdint.h>

/**
 * @brief 初始化LCD硬件SPI.
 *
 * @return 0表示成功, 非0表示失败.
 */
int xf_lcd_spi_init(void);

/**
 * @brief 释放LCD硬件SPI.
 */
void xf_lcd_spi_deinit(void);

/**
 * @brief 发送一个字节.
 *
 * @param [in] value - 待发送数据.
 * @return 0表示成功, 非0表示失败.
 */
int xf_lcd_spi_write_byte(uint8_t value);

/**
 * @brief 发送数据块.
 *
 * @param [in] data - 待发送数据.
 * @param [in] length - 数据长度.
 * @return 0表示成功, 非0表示失败.
 */
int xf_lcd_spi_write(const uint8_t *data, uint32_t length);

#endif /* __PROJECT_ONOFF_PLUG_XF_LCD_SPI_H__ */
