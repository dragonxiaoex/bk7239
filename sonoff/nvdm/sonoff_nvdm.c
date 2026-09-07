/**
 * @file    sonoff_nvdm.c
 * @brief   NVDM核心模块
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

#include "sonoff_common.h"
#include "sonoff_log.h"
#include "sonoff_nvdm.h"
#include "sonoff_nvdm_port.h"
#include "sonoff_private_item.h"

static const char *tag = "SNF-NVDM";

/**
 * @brief NVDM配置项.
 */
typedef struct
{
    const char *group;
    const char *key;
    const char *data;
    size_t data_len;
} SnfNvdmItem;

/**
 * @brief NVDM配置项表.
 */
typedef struct
{
    const char *group;
    const SnfNvdmItem *items;
    size_t item_num;
} SnfNvdmItemTable;

static const SnfNvdmItem nvdm_user_item_array[] = {
    NVDM_USER_ITEM("wifi.ssid", "sonoff"),
    NVDM_USER_ITEM("wifi.password", "12345678"),
    SNF_PRIVATE_NVDM_USER_ITEM
};

static const SnfNvdmItem nvdm_factory_item_array[] = {
    NVDM_FAC_ITEM(NVDM_FACTORY_ITEM_SERIAL_NUMBER, ""),
    NVDM_FAC_ITEM(NVDM_FACTORY_ITEM_ACTIVE_CODE, ""),
    NVDM_FAC_ITEM(NVDM_FACTORY_ITEM_DEVICE_ID, ""),
    NVDM_FAC_ITEM(NVDM_FACTORY_ITEM_FACTORY_APIKEY, ""),
    NVDM_FAC_ITEM(NVDM_FACTORY_ITEM_BASE_MAC, ""),
    NVDM_FAC_ITEM(NVDM_FACTORY_ITEM_DEVICE_MODEL, ""),
    NVDM_FAC_ITEM(NVDM_FACTORY_ITEM_DEVICE_UUID, ""),
};

static const SnfNvdmItem nvdm_matter_item_array[] = {
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_DISCRIMINATOR, "0"),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_ITERATION_COUNT, "0"),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_SALT, ""),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_VERIFIER, ""),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_VENDOR_ID, "4742"),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_VENDOR_NAME, "Sonoff"),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_PRODUCT_ID, "0"),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_PRODUCT_NAME, ""),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_RD_ID_UID, ""),
    NVDM_MATTER_ITEM(NVDM_MATTER_ITEM_PASSCODE, "0"),
};

static const SnfNvdmItemTable nvdm_item_table_array[] = {
    {NVDM_USER_GROUP, nvdm_user_item_array, sizeof(nvdm_user_item_array) / sizeof(nvdm_user_item_array[0])},
    {NVDM_FAC_GROUP, nvdm_factory_item_array, sizeof(nvdm_factory_item_array) / sizeof(nvdm_factory_item_array[0])},
    {NVDM_MATTER_GROUP, nvdm_matter_item_array, sizeof(nvdm_matter_item_array) / sizeof(nvdm_matter_item_array[0])},
};

static const size_t nvdm_item_table_num = sizeof(nvdm_item_table_array) / sizeof(nvdm_item_table_array[0]);

/**
 * @brief 按组名查找配置项表.
 *
 * @param [in] group - 配置组名称.
 * @return 配置项表指针, 未找到时返回NULL.
 */
static const SnfNvdmItemTable *nvdmFindItemTable(const char *group)
{
    size_t i;

    if (group == NULL)
    {
        return NULL;
    }

    for (i = 0; i < nvdm_item_table_num; i++)
    {
        const SnfNvdmItemTable *table = &nvdm_item_table_array[i];

        if (strcmp(table->group, group) == 0)
        {
            return table;
        }
    }

    return NULL;
}

/**
 * @brief 按键名查找配置项.
 *
 * @param [in] table - 配置项表.
 * @param [in] key - 配置键名称.
 * @return 配置项指针, 未找到时返回NULL.
 */
static const SnfNvdmItem *nvdmFindItem(const SnfNvdmItemTable *table, const char *key)
{
    size_t i;

    if ((table == NULL) || (key == NULL))
    {
        return NULL;
    }

    for (i = 0; i < table->item_num; i++)
    {
        const SnfNvdmItem *item = &table->items[i];

        if (strcmp(item->key, key) == 0)
        {
            return item;
        }
    }

    return NULL;
}

/**
 * @brief 将配置项写入默认值.
 *
 * @param [in] item - 配置项.
 * @return 0表示写入成功, 负数表示写入失败.
 */
static int nvdmWriteDefaultItem(const SnfNvdmItem *item)
{
    if (item == NULL)
    {
        return -1;
    }

    return snfNvdmWriteStr(item->group,
                           item->key,
                           (const uint8_t *)item->data,
                           (int)item->data_len);
}

/**
 * @brief 检查并恢复缺失配置项的默认值.
 *
 * @param [in] table - 配置项表.
 * @return 0表示执行成功, 负数表示写入默认值失败.
 */
static int nvdmCheckItemTable(const SnfNvdmItemTable *table)
{
    size_t i;
    int ret;

    if (table == NULL)
    {
        return -1;
    }

    for (i = 0; i < table->item_num; i++)
    {
        const SnfNvdmItem *item = &table->items[i];

        ret = snfNvdmPortExistStatus(item->group, item->key);
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s.%s not exist, reset to default value",
                  item->group, item->key);
            if (nvdmWriteDefaultItem(item) != 0)
            {
                LOG_E(tag, "nvdm item %s.%s write default failed",
                      item->group, item->key);
                return -1;
            }
        }
    }

    return 0;
}

/**
 * @brief 显示配置项表中的全部配置项.
 *
 * @param [in] table - 配置项表.
 */
static void nvdmShowItemTable(const SnfNvdmItemTable *table)
{
    uint8_t buff[BUFF_SIZE_512];
    size_t i;
    int ret;

    if (table == NULL)
    {
        return;
    }

    for (i = 0; i < table->item_num; i++)
    {
        const SnfNvdmItem *item = &table->items[i];

        memset(buff, 0, sizeof(buff));
        ret = snfNvdmReadStr(item->group, item->key, buff, (int)sizeof(buff));
        if (ret != 0)
        {
            LOG_I(tag, "nvdm item %s.%s read error", item->group, item->key);
        }
        else
        {
            printf("%s.%s: %s\r\n", item->group, item->key, (char *)buff);
        }
    }
}

/**
 * @brief 检查并恢复全部缺失配置项的默认值.
 *
 * @return 0表示执行成功, 负数表示写入默认值失败.
 */
static int nvdmCheckDefaultValue(void)
{
    size_t i;

    for (i = 0; i < nvdm_item_table_num; i++)
    {
        const SnfNvdmItemTable *table = &nvdm_item_table_array[i];

        if (nvdmCheckItemTable(table) != 0)
        {
            return -1;
        }
    }

    return 0;
}

int snfNvdmShow(void)
{
    size_t i;

    for (i = 0; i < nvdm_item_table_num; i++)
    {
        const SnfNvdmItemTable *table = &nvdm_item_table_array[i];

        nvdmShowItemTable(table);
    }

    return 0;
}

void snfNvdmCliWriteItem(const char *group, const char *key, const char *value)
{
    const SnfNvdmItemTable *table;
    const SnfNvdmItem *item;
    int value_len;
    int ret;

    if ((group == NULL) || (key == NULL) || (value == NULL))
    {
        LOG_I(tag, "nvdm cli write param invalid");
        return;
    }

    table = nvdmFindItemTable(group);
    if (table == NULL)
    {
        LOG_I(tag, "nvdm group %s not support", group);
        return;
    }

    item = nvdmFindItem(table, key);
    if (item == NULL)
    {
        LOG_I(tag, "nvdm key %s not support", key);
        return;
    }

    LOG_I(tag, "nvdm item %s.%s write value: %s", item->group, item->key, value);
    value_len = (int)strlen(value) + 1;
    ret = snfNvdmWriteStr(item->group, item->key, (const uint8_t *)value, value_len);
    if (ret != 0)
    {
        LOG_E(tag, "nvdm item %s.%s write failed", item->group, item->key);
    }
}

void snfNvdmCliReadItem(const char *group, const char *key)
{
    uint8_t buff[BUFF_SIZE_512];
    const SnfNvdmItemTable *table;
    const SnfNvdmItem *item;
    int ret;

    if ((group == NULL) || (key == NULL))
    {
        LOG_I(tag, "nvdm cli read param invalid");
        return;
    }

    table = nvdmFindItemTable(group);
    if (table == NULL)
    {
        LOG_I(tag, "nvdm group %s not support", group);
        return;
    }

    item = nvdmFindItem(table, key);
    if (item == NULL)
    {
        LOG_I(tag, "nvdm key %s not support", key);
        return;
    }

    memset(buff, 0, sizeof(buff));
    ret = snfNvdmReadStr(item->group, item->key, buff, (int)sizeof(buff));
    if (ret != 0)
    {
        LOG_I(tag, "nvdm item %s.%s read error", item->group, item->key);
        return;
    }

    LOG_I(tag, "nvdm item %s.%s value: %s", item->group, item->key, (char *)buff);
}

int snfNvdmReadStr(const char *group, const char *key, uint8_t *buff, int len)
{
    if ((group == NULL) || (key == NULL) || (buff == NULL) || (len <= 0))
    {
        return -1;
    }

    return snfNvdmPortReadStr(group, key, buff, len);
}

int snfNvdmWriteStr(const char *group, const char *key, const uint8_t *value, int len)
{
    if ((group == NULL) || (key == NULL) || (value == NULL) || (len <= 0))
    {
        return -1;
    }

    return snfNvdmPortWriteStr(group, key, value, len);
}

int snfNvdmReadInt(const char *group, const char *key)
{
    uint8_t buff[BUFF_SIZE_32];
    int ret;

    memset(buff, 0, sizeof(buff));
    ret = snfNvdmReadStr(group, key, buff, (int)sizeof(buff));
    if (ret != 0)
    {
        return ret;
    }

    return atoi((const char *)buff);
}

int snfNvdmWriteInt(const char *group, const char *key, int value)
{
    uint8_t buff[BUFF_SIZE_32];
    int len;

    memset(buff, 0, sizeof(buff));
    snprintf((char *)buff, sizeof(buff), "%d", value);
    len = (int)strlen((const char *)buff) + 1;

    return snfNvdmWriteStr(group, key, buff, len);
}

int snfNvdmCleanUserGroup(void)
{
    const SnfNvdmItemTable *table;
    size_t i;

    table = nvdmFindItemTable(NVDM_USER_GROUP);
    if (table == NULL)
    {
        LOG_E(tag, "nvdm user group not found");
        return -1;
    }

    for (i = 0; i < table->item_num; i++)
    {
        const SnfNvdmItem *item = &table->items[i];

        if (snfNvdmPortDelete(item->group, item->key) != 0)
        {
            LOG_E(tag, "nvdm item %s.%s delete failed", item->group, item->key);
            return -1;
        }

        LOG_I(tag, "nvdm item %s.%s clean, reset to default value", item->group, item->key);
        if (nvdmWriteDefaultItem(item) != 0)
        {
            LOG_E(tag, "nvdm item %s.%s write default failed", item->group, item->key);
            return -1;
        }
    }

    return 0;
}

int snfNvdmInit(void)
{
    if (nvdmCheckDefaultValue() != 0)
    {
        LOG_E(tag, "nvdm check default value failed");
        return -1;
    }

    return 0;
}
