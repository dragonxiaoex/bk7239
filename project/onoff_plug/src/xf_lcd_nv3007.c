/**
 * @file    xf_lcd_nv3007.c
 * @brief   NV3007 LCD驱动
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-31
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <os/os.h>

#include "xf_lcd_nv3007.h"
#include "xf_lcd_spi.h"

#define LCD_COL                 (142U)
#define LCD_ROW                 (428U)
#define LCD_COL_OFFSET          (14U)
#define LCD_ROW_OFFSET          (0U)

#define LCD_DC_PIN              GPIO_16
#define LCD_RST_PIN             GPIO_17
#define LCD_CS_PIN              GPIO_18
#define LCD_BL_PIN              GPIO_9

#define LCD_RESET_DELAY_MS      (100U)
#define LCD_SLEEP_OUT_DELAY_MS  (220U)
#define LCD_DISPLAY_ON_DELAY_MS (200U)

/**
 * @brief 初始化一个LCD控制引脚.
 *
 * @param [in] gpio_id - GPIO编号.
 * @param [in] output_high - 初始电平, 0为低电平, 非0为高电平.
 * @return 0表示成功, 非0表示失败.
 */
static int xfLcdGpioOutputInit(gpio_id_t gpio_id, uint8_t output_high)
{
    gpio_config_t gpio_config = {
        .io_mode = GPIO_OUTPUT_ENABLE,
        .pull_mode = GPIO_PULL_DISABLE,
        .func_mode = GPIO_SECOND_FUNC_DISABLE,
    };
    int ret;

    ret = bk_gpio_set_config(gpio_id, &gpio_config);
    if (ret == BK_OK)
    {
        if (output_high != 0U)
        {
            ret = bk_gpio_set_output_high(gpio_id);
        }
        else
        {
            ret = bk_gpio_set_output_low(gpio_id);
        }
    }

    return ret;
}

/**
 * @brief 初始化LCD通信及控制引脚.
 *
 * @return 0表示成功, 非0表示失败.
 */
static int xfLcdGpioInit(void)
{
    int ret;

    ret = bk_gpio_driver_init();
    if (ret == BK_OK)
    {
        ret = xf_lcd_spi_init();
    }
    if (ret == BK_OK)
    {
        ret = xfLcdGpioOutputInit(LCD_CS_PIN, 1U);
    }
    if (ret == BK_OK)
    {
        ret = xfLcdGpioOutputInit(LCD_RST_PIN, 1U);
    }
    if (ret == BK_OK)
    {
        ret = xfLcdGpioOutputInit(LCD_DC_PIN, 1U);
    }
    if (ret == BK_OK)
    {
        ret = xfLcdGpioOutputInit(LCD_BL_PIN, 0U);
    }

    return ret;
}

/**
 * @brief 复位LCD控制器.
 */
static void xfLcdReset(void)
{
    (void)bk_gpio_set_output_low(LCD_RST_PIN);
    rtos_delay_milliseconds(LCD_RESET_DELAY_MS);
    (void)bk_gpio_set_output_high(LCD_RST_PIN);
    rtos_delay_milliseconds(LCD_RESET_DELAY_MS);
}

/**
 * @brief 发送LCD命令.
 *
 * @param [in] command - LCD命令.
 */
static void xf_lcd_send_cmd(uint8_t command)
{
    (void)bk_gpio_set_output_low(LCD_DC_PIN);
    (void)bk_gpio_set_output_low(LCD_CS_PIN);
    (void)xf_lcd_spi_write_byte(command);
    (void)bk_gpio_set_output_high(LCD_CS_PIN);
}

/**
 * @brief 发送一个LCD数据字节.
 *
 * @param [in] data - LCD数据.
 */
static void xf_lcd_send_data(uint8_t data)
{
    (void)bk_gpio_set_output_high(LCD_DC_PIN);
    (void)bk_gpio_set_output_low(LCD_CS_PIN);
    (void)xf_lcd_spi_write_byte(data);
    (void)bk_gpio_set_output_high(LCD_CS_PIN);
}

/**
 * @brief 发送LCD数据块.
 *
 * @param [in] data - LCD数据.
 * @param [in] length - 数据长度.
 */
static void xf_lcd_send_data_array(const uint8_t *data, uint32_t length)
{
    (void)bk_gpio_set_output_high(LCD_DC_PIN);
    (void)bk_gpio_set_output_low(LCD_CS_PIN);
    (void)xf_lcd_spi_write(data, length);
    (void)bk_gpio_set_output_high(LCD_CS_PIN);
}

/**
 * @brief 写入NV3007初始化寄存器.
 */
static void xf_lcd_nv3007_init(void)
{
    xf_lcd_send_cmd(0x36);
    xf_lcd_send_data(0x60);

    /* NV3006A1N IVO2.6初始化序列. */
    xf_lcd_send_cmd(0xff);
    xf_lcd_send_data(0xa5);
    xf_lcd_send_cmd(0x9a);
    xf_lcd_send_data(0x08);
    xf_lcd_send_cmd(0x9b);
    xf_lcd_send_data(0x08);
    xf_lcd_send_cmd(0x9c);
    xf_lcd_send_data(0xb0);
    xf_lcd_send_cmd(0x9d);
    xf_lcd_send_data(0x16);
    xf_lcd_send_cmd(0x9e);
    xf_lcd_send_data(0xc4);
    xf_lcd_send_cmd(0x8f);
    xf_lcd_send_data(0x55);
    xf_lcd_send_data(0x04);
    xf_lcd_send_cmd(0x84);
    xf_lcd_send_data(0x90);
    xf_lcd_send_cmd(0x83);
    xf_lcd_send_data(0x7b);
    xf_lcd_send_cmd(0x85);
    xf_lcd_send_data(0x33);
    xf_lcd_send_cmd(0x60);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0x70);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0x61);
    xf_lcd_send_data(0x02);
    xf_lcd_send_cmd(0x71);
    xf_lcd_send_data(0x02);
    xf_lcd_send_cmd(0x62);
    xf_lcd_send_data(0x04);
    xf_lcd_send_cmd(0x72);
    xf_lcd_send_data(0x04);
    xf_lcd_send_cmd(0x6c);
    xf_lcd_send_data(0x29);
    xf_lcd_send_cmd(0x7c);
    xf_lcd_send_data(0x29);
    xf_lcd_send_cmd(0x6d);
    xf_lcd_send_data(0x31);
    xf_lcd_send_cmd(0x7d);
    xf_lcd_send_data(0x31);
    xf_lcd_send_cmd(0x6e);
    xf_lcd_send_data(0x0f);
    xf_lcd_send_cmd(0x7e);
    xf_lcd_send_data(0x0f);
    xf_lcd_send_cmd(0x66);
    xf_lcd_send_data(0x21);
    xf_lcd_send_cmd(0x76);
    xf_lcd_send_data(0x21);
    xf_lcd_send_cmd(0x68);
    xf_lcd_send_data(0x3A);
    xf_lcd_send_cmd(0x78);
    xf_lcd_send_data(0x3A);
    xf_lcd_send_cmd(0x63);
    xf_lcd_send_data(0x07);
    xf_lcd_send_cmd(0x73);
    xf_lcd_send_data(0x07);
    xf_lcd_send_cmd(0x64);
    xf_lcd_send_data(0x05);
    xf_lcd_send_cmd(0x74);
    xf_lcd_send_data(0x05);
    xf_lcd_send_cmd(0x65);
    xf_lcd_send_data(0x02);
    xf_lcd_send_cmd(0x75);
    xf_lcd_send_data(0x02);
    xf_lcd_send_cmd(0x67);
    xf_lcd_send_data(0x23);
    xf_lcd_send_cmd(0x77);
    xf_lcd_send_data(0x23);
    xf_lcd_send_cmd(0x69);
    xf_lcd_send_data(0x08);
    xf_lcd_send_cmd(0x79);
    xf_lcd_send_data(0x08);
    xf_lcd_send_cmd(0x6a);
    xf_lcd_send_data(0x13);
    xf_lcd_send_cmd(0x7a);
    xf_lcd_send_data(0x13);
    xf_lcd_send_cmd(0x6b);
    xf_lcd_send_data(0x13);
    xf_lcd_send_cmd(0x7b);
    xf_lcd_send_data(0x13);
    xf_lcd_send_cmd(0x6f);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0x7f);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0x50);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0x52);
    xf_lcd_send_data(0xd6);
    xf_lcd_send_cmd(0x53);
    xf_lcd_send_data(0x08);
    xf_lcd_send_cmd(0x54);
    xf_lcd_send_data(0x08);
    xf_lcd_send_cmd(0x55);
    xf_lcd_send_data(0x1e);
    xf_lcd_send_cmd(0x56);
    xf_lcd_send_data(0x1c);
    /* GOA映射选择. */
    xf_lcd_send_cmd(0xa0);
    xf_lcd_send_data(0x2b);
    xf_lcd_send_data(0x24);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xa1);
    xf_lcd_send_data(0x87);
    xf_lcd_send_cmd(0xa2);
    xf_lcd_send_data(0x86);
    xf_lcd_send_cmd(0xa5);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xa6);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xa7);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xa8);
    xf_lcd_send_data(0x36);
    xf_lcd_send_cmd(0xa9);
    xf_lcd_send_data(0x7e);
    xf_lcd_send_cmd(0xaa);
    xf_lcd_send_data(0x7e);
    xf_lcd_send_cmd(0xB9);
    xf_lcd_send_data(0x85);
    xf_lcd_send_cmd(0xBA);
    xf_lcd_send_data(0x84);
    xf_lcd_send_cmd(0xBB);
    xf_lcd_send_data(0x83);
    xf_lcd_send_cmd(0xBC);
    xf_lcd_send_data(0x82);
    xf_lcd_send_cmd(0xBD);
    xf_lcd_send_data(0x81);
    xf_lcd_send_cmd(0xBE);
    xf_lcd_send_data(0x80);
    xf_lcd_send_cmd(0xBF);
    xf_lcd_send_data(0x01);
    xf_lcd_send_cmd(0xC0);
    xf_lcd_send_data(0x02);
    xf_lcd_send_cmd(0xc1);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xc2);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xc3);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xc4);
    xf_lcd_send_data(0x33);
    xf_lcd_send_cmd(0xc5);
    xf_lcd_send_data(0x7e);
    xf_lcd_send_cmd(0xc6);
    xf_lcd_send_data(0x7e);
    xf_lcd_send_cmd(0xC8);
    xf_lcd_send_data(0x33);
    xf_lcd_send_data(0x33);
    xf_lcd_send_cmd(0xC9);
    xf_lcd_send_data(0x68);
    xf_lcd_send_cmd(0xCA);
    xf_lcd_send_data(0x69);
    xf_lcd_send_cmd(0xCB);
    xf_lcd_send_data(0x6a);
    xf_lcd_send_cmd(0xCC);
    xf_lcd_send_data(0x6b);
    xf_lcd_send_cmd(0xCD);
    xf_lcd_send_data(0x33);
    xf_lcd_send_data(0x33);
    xf_lcd_send_cmd(0xCE);
    xf_lcd_send_data(0x6c);
    xf_lcd_send_cmd(0xCF);
    xf_lcd_send_data(0x6d);
    xf_lcd_send_cmd(0xD0);
    xf_lcd_send_data(0x6e);
    xf_lcd_send_cmd(0xD1);
    xf_lcd_send_data(0x6f);
    xf_lcd_send_cmd(0xAB);
    xf_lcd_send_data(0x03);
    xf_lcd_send_data(0x67);
    xf_lcd_send_cmd(0xAC);
    xf_lcd_send_data(0x03);
    xf_lcd_send_data(0x6b);
    xf_lcd_send_cmd(0xAD);
    xf_lcd_send_data(0x03);
    xf_lcd_send_data(0x68);
    xf_lcd_send_cmd(0xAE);
    xf_lcd_send_data(0x03);
    xf_lcd_send_data(0x6c);
    xf_lcd_send_cmd(0xb3);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xb4);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xb5);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xB6);
    xf_lcd_send_data(0x32);
    xf_lcd_send_cmd(0xB7);
    xf_lcd_send_data(0x7e);
    xf_lcd_send_cmd(0xB8);
    xf_lcd_send_data(0x7e);
    xf_lcd_send_cmd(0xe0);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0xe1);
    xf_lcd_send_data(0x03);
    xf_lcd_send_data(0x0f);
    xf_lcd_send_cmd(0xe2);
    xf_lcd_send_data(0x04);
    xf_lcd_send_cmd(0xe3);
    xf_lcd_send_data(0x01);
    xf_lcd_send_cmd(0xe4);
    xf_lcd_send_data(0x0e);
    xf_lcd_send_cmd(0xe5);
    xf_lcd_send_data(0x01);
    xf_lcd_send_cmd(0xe6);
    xf_lcd_send_data(0x19);
    xf_lcd_send_cmd(0xe7);
    xf_lcd_send_data(0x10);
    xf_lcd_send_cmd(0xe8);
    xf_lcd_send_data(0x10);
    xf_lcd_send_cmd(0xea);
    xf_lcd_send_data(0x12);
    xf_lcd_send_cmd(0xeb);
    xf_lcd_send_data(0xd0);
    xf_lcd_send_cmd(0xec);
    xf_lcd_send_data(0x04);
    xf_lcd_send_cmd(0xed);
    xf_lcd_send_data(0x07);
    xf_lcd_send_cmd(0xee);
    xf_lcd_send_data(0x07);
    xf_lcd_send_cmd(0xef);
    xf_lcd_send_data(0x09);
    xf_lcd_send_cmd(0xf0);
    xf_lcd_send_data(0xd0);
    xf_lcd_send_cmd(0xf1);
    xf_lcd_send_data(0x0e);

    xf_lcd_send_data(0x17);
    xf_lcd_send_cmd(0xf2);
    xf_lcd_send_data(0x2c);
    xf_lcd_send_data(0x1b);
    xf_lcd_send_data(0x0b);
    xf_lcd_send_data(0x20);
    /* 单点模式. */
    xf_lcd_send_cmd(0xe9);
    xf_lcd_send_data(0x29);
    xf_lcd_send_cmd(0xec);
    xf_lcd_send_data(0x04);
    /* 撕裂效应信号设置. */
    xf_lcd_send_cmd(0x35);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0x44);
    xf_lcd_send_data(0x00);
    xf_lcd_send_data(0x10);
    xf_lcd_send_cmd(0x46);
    xf_lcd_send_data(0x10);
    xf_lcd_send_cmd(0xff);
    xf_lcd_send_data(0x00);
    xf_lcd_send_cmd(0x3a);
    xf_lcd_send_data(0x05);
    xf_lcd_send_cmd(0x11);
    rtos_delay_milliseconds(LCD_SLEEP_OUT_DELAY_MS);
    xf_lcd_send_cmd(0x29);
    rtos_delay_milliseconds(LCD_DISPLAY_ON_DELAY_MS);
}

int xf_lcd_init(void)
{
    int ret;

    ret = xfLcdGpioInit();
    if (ret == BK_OK)
    {
        xfLcdReset();
        xf_lcd_nv3007_init();
    }

    return ret;
}

void xf_lcd_bl_output(uint8_t onoff)
{
    if (onoff != 0U)
    {
        (void)bk_gpio_set_output_high(LCD_BL_PIN);
    }
    else
    {
        (void)bk_gpio_set_output_low(LCD_BL_PIN);
    }
}

/**
 * @brief 设置LCD显示区域.
 *
 * @param [in] x_start - 起始X坐标.
 * @param [in] x_end - 结束X坐标.
 * @param [in] y_start - 起始Y坐标.
 * @param [in] y_end - 结束Y坐标.
 */
static void xfLcdAddrSet(uint16_t x_start, uint16_t x_end, uint16_t y_start, uint16_t y_end)
{
    uint16_t start;
    uint16_t end;

    start = x_start + LCD_ROW_OFFSET;
    end = x_end + LCD_ROW_OFFSET;
    xf_lcd_send_cmd(0x2a);
    xf_lcd_send_data((uint8_t)(start >> 8U));
    xf_lcd_send_data((uint8_t)(start & 0xffU));
    xf_lcd_send_data((uint8_t)(end >> 8U));
    xf_lcd_send_data((uint8_t)(end & 0xffU));

    start = y_start + LCD_COL_OFFSET;
    end = y_end + LCD_COL_OFFSET;
    xf_lcd_send_cmd(0x2b);
    xf_lcd_send_data((uint8_t)(start >> 8U));
    xf_lcd_send_data((uint8_t)(start & 0xffU));
    xf_lcd_send_data((uint8_t)(end >> 8U));
    xf_lcd_send_data((uint8_t)(end & 0xffU));
    xf_lcd_send_cmd(0x2c);
}

void xf_lcd_display_full(const uint8_t *data)
{
    if (data != NULL)
    {
        xfLcdAddrSet(0U, LCD_ROW - 1U, 0U, LCD_COL - 1U);
        xf_lcd_send_data_array(data, LCD_ROW * LCD_COL * 2U);
    }
}

void xf_lcd_full_color(uint16_t color)
{
    uint8_t line_data[LCD_COL * 2U];
    uint16_t pixel;
    uint16_t line;

    for (pixel = 0U; pixel < LCD_COL; pixel++)
    {
        line_data[pixel * 2U] = (uint8_t)(color >> 8U);
        line_data[(pixel * 2U) + 1U] = (uint8_t)(color & 0xffU);
    }

    xfLcdAddrSet(0U, LCD_ROW - 1U, 0U, LCD_COL - 1U);
    for (line = 0U; line < LCD_ROW; line++)
    {
        xf_lcd_send_data_array(line_data, sizeof(line_data));
    }
}

void xf_lcd_display_part(uint16_t x_start,
                         uint16_t x_end,
                         uint16_t y_start,
                         uint16_t y_end,
                         const uint8_t *data)
{
    uint32_t data_length;

    if ((data != NULL) && (x_start <= x_end) && (y_start <= y_end)
        && (x_end < LCD_ROW) && (y_end < LCD_COL))
    {
        data_length = ((uint32_t)x_end - x_start + 1U) * ((uint32_t)y_end - y_start + 1U) * 2U;
        xfLcdAddrSet(x_start, x_end, y_start, y_end);
        xf_lcd_send_data_array(data, data_length);
    }
}
