/**
 * @file    sonoff_ota_adapter.h
 * @brief   OTA升级模块适配层
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-27
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_OTA_SONOFF_OTA_ADAPTER_H__
#define __SONOFF_OTA_SONOFF_OTA_ADAPTER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 擦除OTA目标分区中的数据.
 *
 * @param [in] offset - 分区内偏移量.
 * @param [in] size - 擦除长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfOtaAdapterErase(uint32_t offset, uint32_t size);

/**
 * @brief 向OTA目标分区写入数据.
 *
 * @param [in] offset - 分区内偏移量.
 * @param [in] data - 待写入数据.
 * @param [in] size - 数据长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfOtaAdapterWrite(uint32_t offset, const void *data, uint32_t size);

/**
 * @brief 从OTA目标分区读取数据.
 *
 * @param [in] offset - 分区内偏移量.
 * @param [out] data - 数据输出缓冲区.
 * @param [in] size - 数据长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfOtaAdapterRead(uint32_t offset, void *data, uint32_t size);

/**
 * @brief 设置bootloader下一次启动的目标分区.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfOtaAdapterSetBootPartition(void);

/**
 * @brief 重启设备进入bootloader.
 */
void snfOtaAdapterReboot(void);

#ifdef __cplusplus
}
#endif

#endif /* __SONOFF_OTA_SONOFF_OTA_ADAPTER_H__ */
