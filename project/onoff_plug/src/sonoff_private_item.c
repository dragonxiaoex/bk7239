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

static const char *tag = "SNF-PRI-ITEM";

#define NVDM_TEST_ITEM_LEN              15


int snfTestItemGet(char *item, uint16_t item_size)
{
    if ((item == NULL) || (item_size <= NVDM_TEST_ITEM_LEN))
    {
        return -1;
    }

    memset(item, 0, item_size);
    if (snfNvdmReadStr(NVDM_USER_GROUP, NVDM_TEST_ITEM, (uint8_t *)item, (int)item_size) != 0)
    {
        return -1;
    }

    return 0;
}

int snfTestItemSet(const char *item)
{
    int len;
    int ret;

    len = (int)strlen(item) + 1;
    ret = snfNvdmWriteStr(NVDM_USER_GROUP,
                          NVDM_TEST_ITEM,
                          (const uint8_t *)item,
                          len);
    if (ret != 0)
    {
        LOG_I(tag, "test item write failed");
        return -1;
    }

    return 0;
}