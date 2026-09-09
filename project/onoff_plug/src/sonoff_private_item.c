/**
 * @file    sonoff_private_item.c
 * @brief   私有NVDM处理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include "stdint.h"
#include "string.h"

#include "sonoff_private_item.h"
#include "sonoff_nvdm.h"
#include "sonoff_log.h"

#define NVDM_TEST_ITEM_LEN              15


int snfNvdmPlugOnOffGet(void)
{
    return snfNvdmReadInt(NVDM_USER_GROUP, NVDM_PLUG_ONOFF_ITEM);
}

int snfNvdmPlugOnOffSet(int onoff)
{
    return snfNvdmWriteInt(NVDM_USER_GROUP, NVDM_PLUG_ONOFF_ITEM, onoff);
}
