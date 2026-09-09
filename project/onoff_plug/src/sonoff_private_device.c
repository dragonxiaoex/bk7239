/**
 * @file    sonoff_private_device.c
 * @brief   不同项目的私有化入口
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date:   2026-09-09
 * 
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 * 
 */
#include "stdint.h"

#include "sonoff_private_device.h"
#include "sonoff_led_handle.h"

int snfPrivateDeviceStart(void)
{
    snfLedHandleInit();
    return 0;
}

int snfPrivateDeviceStop(void)
{
    return 0;
}