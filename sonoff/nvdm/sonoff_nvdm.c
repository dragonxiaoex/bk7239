/**
 * @file    sonoff_nvdm.c
 * @brief   NVDM模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sonoff_log.h"
#include "sonoff_nvdm.h"
#include "sonoff_nvdm_port.h"

static const char *tag = "SNF-NVDM";

typedef struct
{
    int group;
    int key;
    const char *key_name;
    const char *data;
    size_t size;
} NvdmItem;

static const NvdmItem nvdm_item_array[] = {
    NVDM_NORMAL_ITEM(NVDM_DEV_MAC_KEY, "dev.ble.mac", "11:22:33:44:55:66"),
};

static const size_t nvdm_item_num = sizeof(nvdm_item_array) / sizeof(NvdmItem);

/** @brief 检查并恢复缺失配置项的默认值. */
static void nvdmCheckDefaultValue(void)
{
    size_t i = 0;
    int ret = 0;
    int value_len = 0;

    for (i = 0; i < nvdm_item_num; i++)
    {
        ret = snfNvdmPortExistStatus(nvdm_item_array[i].group, nvdm_item_array[i].key);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s not exist, reset to default value", nvdm_item_array[i].key_name);
            value_len = (int)strlen(nvdm_item_array[i].data) + 1;
            snfNvdmWriteStr(nvdm_item_array[i].group,
                            nvdm_item_array[i].key,
                            (const uint8_t *)nvdm_item_array[i].data,
                            value_len);
        }
    }
}

int snfNvdmShow(void)
{
    uint8_t buff[BUFF_SIZE_512];
    size_t i = 0;
    int ret = 0;

    for (i = 0; i < nvdm_item_num; i++)
    {
        memset(buff, 0, sizeof(buff));
        ret = snfNvdmReadStr(nvdm_item_array[i].group,
                             nvdm_item_array[i].key,
                             buff,
                             BUFF_SIZE_512);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s read error!", nvdm_item_array[i].key_name);
        }
        else
        {
            LOG_I(tag, "nvdm item %s: %s", nvdm_item_array[i].key_name, (char *)buff);
        }
    }

    return 0;
}

void snfNvdmCliWriteItem(const char *key_name, const char *value)
{
    size_t i = 0;
    int value_len = 0;

    for (i = 0; i < nvdm_item_num; i++)
    {
        if (strncmp(nvdm_item_array[i].key_name,
                    key_name,
                    strlen(nvdm_item_array[i].key_name)) == 0)
        {
            LOG_I(tag, "nvdm item %s write value: %s", nvdm_item_array[i].key_name, value);
            value_len = (int)strlen(value) + 1;
            snfNvdmWriteStr(nvdm_item_array[i].group,
                            nvdm_item_array[i].key,
                            (const uint8_t *)value,
                            value_len);
        }
    }
}

void snfNvdmCliReadItem(const char *key_name)
{
    uint8_t buff[BUFF_SIZE_512];
    size_t i = 0;

    memset(buff, 0, sizeof(buff));

    for (i = 0; i < nvdm_item_num; i++)
    {
        if (strncmp(nvdm_item_array[i].key_name,
                    key_name,
                    strlen(nvdm_item_array[i].key_name)) == 0)
        {
            snfNvdmReadStr(nvdm_item_array[i].group,
                           nvdm_item_array[i].key,
                           buff,
                           BUFF_SIZE_512);
            LOG_I(tag, "nvdm item %s value: %s", nvdm_item_array[i].key_name, (char *)buff);
        }
    }
}

int snfNvdmInit(void)
{
    snfNvdmPortInit();
    nvdmCheckDefaultValue();

    return 0;
}

int snfNvdmReadStr(int group_id, int key_num, uint8_t *buff, int len)
{
    return snfNvdmPortReadStr(group_id, key_num, buff, len);
}

int snfNvdmWriteStr(int group_id, int key_num, const uint8_t *value, int len)
{
    return snfNvdmPortWriteStr(group_id, key_num, value, len);
}

int snfNvdmReadInt(int group_id, int key_num)
{
    uint8_t buff[BUFF_SIZE_32];
    int ret = 0;

    ret = snfNvdmPortReadStr(group_id, key_num, buff, BUFF_SIZE_32);
    if (ret != 0)
    {
        return ret;
    }

    return atoi((const char *)buff);
}

int snfNvdmWriteInt(int group_id, int key_num, int value)
{
    uint8_t buff[BUFF_SIZE_32];
    int len = 0;

    snprintf((char *)buff, sizeof(buff) - 1, "%d", value);
    len = (int)strlen((const char *)buff) + 1;

    return snfNvdmPortWriteStr(group_id, key_num, buff, len);
}
