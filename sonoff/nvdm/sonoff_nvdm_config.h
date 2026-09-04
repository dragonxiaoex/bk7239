/**
 * @file    sonoff_nvdm_config.h
 * @brief   NVDM条目配置
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_NVDM_CONFIG_H__
#define __SONOFF_NVDM_CONFIG_H__

#include <stdint.h>

/** @brief Matter配置项键名. */
#define NVDM_MATTER_ITEM_DISCRIMINATOR      "discriminator"
#define NVDM_MATTER_ITEM_ITERATION_COUNT    "iteration-count"
#define NVDM_MATTER_ITEM_SALT               "salt"
#define NVDM_MATTER_ITEM_VERIFIER           "verifier"
#define NVDM_MATTER_ITEM_VENDOR_ID          "vendor.id"
#define NVDM_MATTER_ITEM_VENDOR_NAME        "vendor.name"
#define NVDM_MATTER_ITEM_PRODUCT_ID         "product.id"
#define NVDM_MATTER_ITEM_PRODUCT_NAME       "product.name"
#define NVDM_MATTER_ITEM_RD_ID_UID          "rd.id.uid"
#define NVDM_MATTER_ITEM_PASSCODE           "passcode"

/** @brief 工厂配置项键名. */
#define NVDM_FACTORY_ITEM_SERIAL_NUMBER     "serial-number"

/** @brief 产品识别码十进制数字长度. */
#define NVDM_FACTORY_SERIAL_NUMBER_LEN      14

/**
 * @brief 读取产品识别码.
 *
 * @param [out] serial_number - 识别码缓冲区, 需能容纳14位数字和结束符.
 * @param [in] serial_number_size - 缓冲区长度.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfSerialNumberGet(char *serial_number, uint16_t serial_number_size);

/**
 * @brief 写入产品识别码.
 *
 * @param [in] serial_number - 14位十进制数字字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfSerialNumberSet(const char *serial_number);

#endif  /* __SONOFF_NVDM_CONFIG_H__ */
