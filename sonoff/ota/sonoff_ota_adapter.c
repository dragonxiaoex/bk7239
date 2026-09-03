/**
 * @file    sonoff_ota_adapter.c
 * @brief   Beken平台OTA适配层
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-27
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>

#include <common/bk_err.h>
#include <components/system.h>
#include <driver/flash_partition.h>

#if defined(CONFIG_TASK_WDT) && CONFIG_TASK_WDT
#include <bk_wdt.h>
#endif

#if defined(CONFIG_BK_OTA) && CONFIG_BK_OTA
#include <modules/bk_ota.h>
#endif

#include "sonoff_ota_adapter.h"

/** @brief OTA目标分区标识. */
#define SNF_OTA_TARGET_PARTITION (BK_PARTITION_OTA)

/**
 * @brief 获取OTA目标分区信息.
 *
 * @return OTA目标分区信息, 获取失败时返回NULL.
 */
static const bk_logic_partition_t *otaAdapterGetPartition(void)
{
    return bk_flash_partition_get_info(SNF_OTA_TARGET_PARTITION);
}

/**
 * @brief 检查OTA目标分区范围.
 *
 * @param [in] offset - 分区内偏移量.
 * @param [in] size - 数据长度.
 * @return 0表示合法, 负数表示非法.
 */
static int otaAdapterCheckRange(uint32_t offset, uint32_t size)
{
    const bk_logic_partition_t *partition = otaAdapterGetPartition();

    if (partition == NULL)
    {
        return -1;
    }

    if ((offset > partition->partition_length)
        || (size > (partition->partition_length - offset)))
    {
        return -1;
    }

    return 0;
}

int snfOtaAdapterErase(uint32_t offset, uint32_t size)
{
    if ((size == 0) || (otaAdapterCheckRange(offset, size) != 0))
    {
        return -1;
    }

    if (bk_flash_partition_erase(SNF_OTA_TARGET_PARTITION, offset, size) != BK_OK)
    {
        return -1;
    }

    return 0;
}

int snfOtaAdapterWrite(uint32_t offset, const void *data, uint32_t size)
{
    int ret = -1;

    if ((data == NULL) || (size == 0) || (otaAdapterCheckRange(offset, size) != 0))
    {
        return -1;
    }

    if (bk_flash_partition_write(SNF_OTA_TARGET_PARTITION,
                                 (const uint8_t *)data,
                                 offset,
                                 size) == BK_OK)
    {
        ret = 0;
    }

#if defined(CONFIG_TASK_WDT) && CONFIG_TASK_WDT
    bk_task_wdt_feed();
#endif

    return ret;
}

int snfOtaAdapterRead(uint32_t offset, void *data, uint32_t size)
{
    if ((data == NULL) || (size == 0) || (otaAdapterCheckRange(offset, size) != 0))
    {
        return -1;
    }

    if (bk_flash_partition_read(SNF_OTA_TARGET_PARTITION,
                                (uint8_t *)data,
                                offset,
                                size) != BK_OK)
    {
        return -1;
    }

    return 0;
}

int snfOtaAdapterSetBootPartition(void)
{
#if defined(CONFIG_BK_OTA) && CONFIG_BK_OTA
#if defined(CONFIG_OTA_CONFIRM_UPDATE) && CONFIG_OTA_CONFIRM_UPDATE
    return (bk_ota_confirm_update() == BK_OK) ? 0 : -1;
#else
    /* BK OTA未启用启动分区确认接口时，无法安全设置下一次启动目标。 */

    return -1;
#endif
#else
    /*
     * 当前适配使用BK_PARTITION_OTA作为bootloader的暂存镜像分区。
     * 若平台使用独立的启动控制分区，应在本函数中写入对应启动标志。
     */

    return 0;
#endif
}

void snfOtaAdapterReboot(void)
{
    bk_reboot();
}
