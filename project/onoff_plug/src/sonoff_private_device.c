/**
 * @file    sonoff_private_device.c
 * @brief   不同项目的私有化入口
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include "sonoff_plug_handle.h"
#include "sonoff_private_device.h"

int snfPrivateDeviceStart(void)
{
    snfPlugHandleInit();

    return 0;
}

int snfPrivateDeviceStop(void)
{
    snfPlugHandleDeinit();

    return 0;
}
