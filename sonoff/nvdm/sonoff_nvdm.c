/**
 * @file    sonoff_nvdm_core.c
 * @brief   NVDM核心模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>
#include <stddef.h>

#include "sonoff_log.h"
#include "sonoff_nvdm.h"
#include "sonoff_nvdm_port.h"
#include "sonoff_common.h"

static const char *tag = "SNF-NVDM";

typedef struct
{
    const char *group;
    const char *key;
    const char *data;
    size_t data_len;
} SnfNvdmItem;

static const SnfNvdmItem nvdm_user_item_array[] = {
    NVDM_USER_ITEM("wifi.ssid", "sonoff"),
    NVDM_USER_ITEM("wifi.password", "12345678"),
};

static const SnfNvdmItem nvdm_factory_item_array[] = {
    NVDM_FAC_ITEM("wifi.mac", "11:22:33:44:55:66"),
};

static const SnfNvdmItem nvdm_matter_item_array[] = {
    NVDM_MATTER_ITEM("pincode", "123456"),
};

static const size_t nvdm_user_item_num = sizeof(nvdm_user_item_array) / sizeof(SnfNvdmItem);
static const size_t nvdm_factory_item_num = sizeof(nvdm_factory_item_array) / sizeof(SnfNvdmItem);
static const size_t nvdm_matter_item_num = sizeof(nvdm_matter_item_array) / sizeof(SnfNvdmItem);

/** @brief 检查并恢复缺失配置项的默认值. */
static void nvdmCheckDefaultValue(void)
{
    size_t i = 0;
    int ret = 0;
    int value_len = 0;

    for (i = 0; i < nvdm_user_item_num; i++)
    {
        ret = snfNvdmPortExistStatus(nvdm_user_item_array[i].group, nvdm_user_item_array[i].key);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s.%s not exist, reset to default value", 
                nvdm_user_item_array[i].group, nvdm_user_item_array[i].key);
            value_len = (int)strlen(nvdm_user_item_array[i].data) + 1;
            snfNvdmWriteStr(nvdm_user_item_array[i].group,
                            nvdm_user_item_array[i].key,
                            (const uint8_t *)nvdm_user_item_array[i].data,
                            value_len);
        }
    }

    for (i = 0; i < nvdm_factory_item_num; i++)
    {
        ret = snfNvdmPortExistStatus(nvdm_factory_item_array[i].group, nvdm_factory_item_array[i].key);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s.%s not exist, reset to default value",
                nvdm_factory_item_array[i].group, nvdm_factory_item_array[i].key);
            value_len = (int)strlen(nvdm_factory_item_array[i].data) + 1;
            snfNvdmWriteStr(nvdm_factory_item_array[i].group,
                            nvdm_factory_item_array[i].key,
                            (const uint8_t *)nvdm_factory_item_array[i].data,
                            value_len);
        }
    }

    for (i = 0; i < nvdm_matter_item_num; i++)
    {
        ret = snfNvdmPortExistStatus(nvdm_matter_item_array[i].group, nvdm_matter_item_array[i].key);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s.%s not exist, reset to default value", 
                nvdm_matter_item_array[i].group, nvdm_matter_item_array[i].key);
            value_len = (int)strlen(nvdm_matter_item_array[i].data) + 1;
            snfNvdmWriteStr(nvdm_matter_item_array[i].group,
                            nvdm_matter_item_array[i].key,
                            (const uint8_t *)nvdm_matter_item_array[i].data,
                            value_len);
        }
    }
}

int snfNvdmShow(void)
{
    uint8_t buff[BUFF_SIZE_512];
    size_t i = 0;
    int ret = 0;

    for (i = 0; i < nvdm_user_item_num; i++)
    {
        memset(buff, 0, sizeof(buff));
        ret = snfNvdmReadStr(nvdm_user_item_array[i].group,
                             nvdm_user_item_array[i].key,
                             buff,
                             BUFF_SIZE_512);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s read error!", nvdm_user_item_array[i].key);
        }
        else
        {
            LOG_I(tag, "nvdm item %s: %s", nvdm_user_item_array[i].key, (char *)buff);
        }
    }

    for (i = 0; i < nvdm_factory_item_num; i++)
    {
        memset(buff, 0, sizeof(buff));
        ret = snfNvdmReadStr(nvdm_factory_item_array[i].group,
                             nvdm_factory_item_array[i].key,
                             buff,
                             BUFF_SIZE_512);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s read error!", nvdm_factory_item_array[i].key);
        }
        else
        {
            LOG_I(tag, "nvdm item %s: %s", nvdm_factory_item_array[i].key, (char *)buff);
        }
    }

    for (i = 0; i < nvdm_matter_item_num; i++)
    {
        memset(buff, 0, sizeof(buff));
        ret = snfNvdmReadStr(nvdm_matter_item_array[i].group,
                             nvdm_matter_item_array[i].key,
                             buff,
                             BUFF_SIZE_512);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s read error!", nvdm_matter_item_array[i].key);
        }
        else
        {
            LOG_I(tag, "nvdm item %s: %s", nvdm_matter_item_array[i].key, (char *)buff);
        }
    }

    return 0;
}

void snfNvdmCliWriteItem(const char *group, const char *key, const char *value)
{
    size_t i = 0;
    int value_len = 0;
    SnfNvdmItem *item_array = NULL;
    size_t item_num = 0;

    if(strncmp(NVDM_USER_GROUP, group, strlen(NVDM_USER_GROUP)) == 0)
    {
        item_array = nvdm_user_item_array;
        item_num = nvdm_user_item_num;
    }
    else if(strncmp(NVDM_FAC_GROUP, group, strlen(NVDM_FAC_GROUP)) == 0)
    {
        item_array = nvdm_factory_item_array;
        item_num = nvdm_factory_item_num;
    }
    else if(strncmp(NVDM_MATTER_GROUP, group, strlen(NVDM_MATTER_GROUP)) == 0)
    {
        item_array = nvdm_matter_item_array;
        item_num = nvdm_matter_item_num;
    }
    else
    {
        LOG_I(tag, "nvdm group %s not support", group);
        return;
    }

    for (i = 0; i < item_num; i++)
    {
        if (strncmp(item_array[i].key,
                    key,
                    strlen(item_array[i].key)) == 0)
        {
            LOG_I(tag, "nvdm item %s write value: %s", item_array[i].key, value);
            value_len = (int)strlen(value) + 1;
            snfNvdmWriteStr(item_array[i].group,
                            item_array[i].key,
                            (const uint8_t *)value,
                            value_len);
        }
    }
}

void snfNvdmCliReadItem(const char *group, const char *key)
{
    uint8_t buff[BUFF_SIZE_512];
    size_t i = 0;
    SnfNvdmItem *item_array = NULL;
    size_t item_num = 0;

    if(strncmp(NVDM_USER_GROUP, group, strlen(NVDM_USER_GROUP)) == 0)
    {
        item_array = nvdm_user_item_array;
        item_num = nvdm_user_item_num;
    }
    else if(strncmp(NVDM_FAC_GROUP, group, strlen(NVDM_FAC_GROUP)) == 0)
    {
        item_array = nvdm_factory_item_array;
        item_num = nvdm_factory_item_num;
    }
    else if(strncmp(NVDM_MATTER_GROUP, group, strlen(NVDM_MATTER_GROUP)) == 0)
    {
        item_array = nvdm_matter_item_array;
        item_num = nvdm_matter_item_num;
    }
    else
    {
        LOG_I(tag, "nvdm group %s not support", group);
        return;
    }

    memset(buff, 0, sizeof(buff));

    for (i = 0; i < item_num; i++)
    {
        if (strncmp(item_array[i].key,
                    key,
                    strlen(item_array[i].key)) == 0)
        {
            snfNvdmReadStr(item_array[i].group,
                           item_array[i].key,
                           buff,
                           BUFF_SIZE_512);
            LOG_I(tag, "nvdm item %s value: %s", item_array[i].key, (char *)buff);
        }
    }
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

int snfNvdmCleanUserGroup(void)
{
    int ret = 0;
    size_t i = 0;
    int value_len = 0;

    if(snfNvdmPortDeleteGroup(NVDM_USER_GROUP) == 0)
    {
        for (i = 0; i < nvdm_user_item_num; i++)
        {
            LOG_I(tag, "nvdm item %s.%s clean, reset to default value", 
                nvdm_user_item_array[i].group, nvdm_user_item_array[i].key);
            value_len = (int)strlen(nvdm_user_item_array[i].data) + 1;
            snfNvdmWriteStr(nvdm_user_item_array[i].group,
                            nvdm_user_item_array[i].key,
                            (const uint8_t *)nvdm_user_item_array[i].data,
                            value_len);
        }
    }
    else
    {
        LOG_I(tag, "nvdm user group clean error");
        return -1;
    }

    return 0;
}

int snfNvdmInit(void)
{
    snfNvdmPortInit();
    nvdmCheckDefaultValue();

    return 0;
}
