/**
 * @file    xf_lcd_spi.c
 * @brief   LCD硬件SPI适配
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
#include <driver/spi.h>

#include "xf_lcd_spi.h"

/** @brief LCD使用的SPI控制器. */
#define XF_LCD_SPI_ID        SPI_ID_0

/** @brief LCD SPI时钟频率. */
#define XF_LCD_SPI_BAUD_RATE (8000000U)

/** @brief LCD SPI时钟引脚. */
#define XF_LCD_SPI_SCK_PIN   GPIO_15

/** @brief LCD SPI数据输出引脚. */
#define XF_LCD_SPI_MOSI_PIN  GPIO_14

/** @brief BK7239N SPI单次发送长度上限. */
#define XF_LCD_SPI_WRITE_MAX (4095U)

int xf_lcd_spi_init(void)
{
    spi_config_t spi_config = {
        .role = SPI_ROLE_MASTER,
        .bit_width = SPI_BIT_WIDTH_8BITS,
        .polarity = SPI_POLARITY_LOW,
        .phase = SPI_PHASE_1ST_EDGE,
        .wire_mode = SPI_4WIRE_MODE,
        .baud_rate = XF_LCD_SPI_BAUD_RATE,
        .bit_order = SPI_MSB_FIRST,
    };
    int ret;

    ret = bk_spi_driver_init();
    if (ret == BK_OK)
    {
        ret = bk_spi_init(XF_LCD_SPI_ID, &spi_config);
    }
    if (ret == BK_OK)
    {
        ret = bk_gpio_map_dev_to_pin(XF_LCD_SPI_SCK_PIN, GPIO_DEV_SPI0_SCK);
    }
    if (ret == BK_OK)
    {
        ret = bk_gpio_map_dev_to_pin(XF_LCD_SPI_MOSI_PIN, GPIO_DEV_SPI0_MOSI);
    }

    return ret;
}

void xf_lcd_spi_deinit(void)
{
    gpio_config_t gpio_config = {
        .io_mode = GPIO_INPUT_ENABLE,
        .pull_mode = GPIO_PULL_DOWN_EN,
        .func_mode = GPIO_SECOND_FUNC_DISABLE,
    };

    (void)bk_spi_deinit(XF_LCD_SPI_ID);
    (void)bk_gpio_set_config(XF_LCD_SPI_SCK_PIN, &gpio_config);
    (void)bk_gpio_set_config(XF_LCD_SPI_MOSI_PIN, &gpio_config);
}

int xf_lcd_spi_write_byte(uint8_t value)
{
    int ret;

    ret = bk_spi_write_bytes(XF_LCD_SPI_ID, &value, sizeof(value));

    return ret;
}

int xf_lcd_spi_write(const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;
    uint32_t remaining = length;
    uint32_t write_length;
    int ret;

    if ((data == NULL) || (length == 0U))
    {
        ret = BK_ERR_PARAM;
    }
    else
    {
        ret = BK_OK;
        while ((remaining > 0U) && (ret == BK_OK))
        {
            if (remaining > XF_LCD_SPI_WRITE_MAX)
            {
                write_length = XF_LCD_SPI_WRITE_MAX;
            }
            else
            {
                write_length = remaining;
            }

            ret = bk_spi_write_bytes(XF_LCD_SPI_ID, &data[offset], write_length);
            offset += write_length;
            remaining -= write_length;
        }
    }

    return ret;
}
