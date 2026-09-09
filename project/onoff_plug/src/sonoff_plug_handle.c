/**
 * @file    sonoff_plug_handle.c
 * @brief   插座开关控制
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>

#include <driver/gpio.h>

#include "sonoff_log.h"
#include "sonoff_plug_handle.h"
#include "sonoff_private_item.h"

static const char *tag = "SNF-PLUG";

#define SNF_PLUG_CONTROL_GPIO GPIO_20

int snfPlugOnOffRawSet(uint8_t onoff)
{
    bk_err_t ret;

    if (onoff != 0)
    {
        ret = bk_gpio_set_output_high(SNF_PLUG_CONTROL_GPIO);
    }
    else
    {
        ret = bk_gpio_set_output_low(SNF_PLUG_CONTROL_GPIO);
    }

    if (ret != BK_OK)
    {
        LOG_E(tag, "set %d failed, ret=%d", onoff, ret);
        return -1;
    }

    snfNvdmPlugOnOffSet(onoff);

    return 0;
}

int snfPlugOnOffSet(uint8_t onoff)
{
    if (snfPlugOnOffRawSet(onoff) != 0)
    {
        return -1;
    }

    if (snfMatterOnOffReport(onoff) != 0)
    {
        return -1;
    }

    return 0;
}

int snfPlugOnOffGet(void)
{
    if (bk_gpio_get_output(SNF_PLUG_CONTROL_GPIO) != 0)
    {
        return 1;
    }

    return 0;
}

void snfPlugHandleInit(void)
{
    int onoff = 0;
    if (bk_gpio_disable_input(SNF_PLUG_CONTROL_GPIO) != BK_OK)
    {
        LOG_E(tag, "disable input failed");
        return;
    }

    if (bk_gpio_enable_output(SNF_PLUG_CONTROL_GPIO) != BK_OK)
    {
        LOG_E(tag, "enable output failed");
        return;
    }

    onoff = snfNvdmPlugOnOffGet();
    if(onoff == 1)
    {
        bk_gpio_set_output_high(SNF_PLUG_CONTROL_GPIO);
    }
    else
    {
        bk_gpio_set_output_low(SNF_PLUG_CONTROL_GPIO);
    }

    return;
}

void snfPlugHandleDeinit(void)
{
    if (bk_gpio_disable_output(SNF_PLUG_CONTROL_GPIO) != BK_OK)
    {
        LOG_E(tag, "disable output failed");
    }

    return;
}
