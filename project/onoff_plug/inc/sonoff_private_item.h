/**
 * @file    sonoff_private_item.h
 * @brief   私有NVDM处理
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-05
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_PRIVATE_ITEM_H__
#define __SONOFF_PRIVATE_ITEM_H__

#include "sonoff_nvdm.h"

#define NVDM_PLUG_ONOFF_ITEM                  "plug.onoff"

#define SNF_PRIVATE_NVDM_USER_ITEM \
        NVDM_USER_ITEM(NVDM_PLUG_ONOFF_ITEM, "0"), 

#define SNF_PRIVATE_NVDM_FACTORY_ITEM

int snfNvdmPlugOnOffGet(void);
int snfNvdmPlugOnOffSet(int onoff);

#endif /* __SONOFF_PRIVATE_ITEM_H__ */
