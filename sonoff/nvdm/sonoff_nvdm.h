/**
 * @file    sonoff_nvdm.h
 * @brief   NVDM模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_NVDM_SONOFF_NVDM_H__
#define __SONOFF_NVDM_SONOFF_NVDM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NVDM工厂配置组. */
#define NVDM_FAC_GROUP                  (0x0001)

/** @brief NVDM普通配置组. */
#define NVDM_NORMAL_GROUP               (0x0002)

/** @brief 设备MAC地址配置键. */
#define NVDM_DEV_MAC_KEY                (0x0001)

/** @brief 定义NVDM普通配置项. */
#define NVDM_NORMAL_ITEM(x, n, y)       {NVDM_NORMAL_GROUP, (x), (n), (y), ((sizeof(y) + 3) / 4)}

/** @brief 定义NVDM工厂配置项. */
#define NVDM_FAC_ITEM(x, n, y)          {NVDM_FAC_GROUP, (x), (n), (y), ((sizeof(y) + 3) / 4)}

/**
 * @brief 显示全部NVDM配置项.
 *
 * @return 0表示执行成功.
 */
int snfNvdmShow(void);

/**
 * @brief 通过命令行写入NVDM配置项.
 *
 * @param [in] key_name - 配置项名称.
 * @param [in] value - 待写入的字符串值.
 */
void snfNvdmCliWriteItem(const char *key_name, const char *value);

/**
 * @brief 通过命令行读取NVDM配置项.
 *
 * @param [in] key_name - 配置项名称.
 */
void snfNvdmCliReadItem(const char *key_name);

/**
 * @brief 初始化NVDM模块.
 *
 * @return 0表示初始化成功.
 */
int snfNvdmInit(void);

/**
 * @brief 读取NVDM字符串配置项.
 *
 * @param [in] group_id - 配置组标识.
 * @param [in] key_num - 配置键标识.
 * @param [out] buff - 读取缓冲区.
 * @param [in] len - 缓冲区长度.
 * @return 0表示读取成功, 其他值表示读取失败.
 */
int snfNvdmReadStr(int group_id, int key_num, uint8_t *buff, int len);

/**
 * @brief 写入NVDM字符串配置项.
 *
 * @param [in] group_id - 配置组标识.
 * @param [in] key_num - 配置键标识.
 * @param [in] value - 待写入的字符串值.
 * @param [in] len - 字符串长度.
 * @return 0表示写入成功, 其他值表示写入失败.
 */
int snfNvdmWriteStr(int group_id, int key_num, const uint8_t *value, int len);

/**
 * @brief 读取NVDM整数配置项.
 *
 * @param [in] group_id - 配置组标识.
 * @param [in] key_num - 配置键标识.
 * @return 配置项整数值或底层读取错误码.
 */
int snfNvdmReadInt(int group_id, int key_num);

/**
 * @brief 写入NVDM整数配置项.
 *
 * @param [in] group_id - 配置组标识.
 * @param [in] key_num - 配置键标识.
 * @param [in] value - 待写入的整数值.
 * @return 0表示写入成功, 其他值表示写入失败.
 */
int snfNvdmWriteInt(int group_id, int key_num, int value);

#ifdef __cplusplus
}
#endif

#endif /* __SONOFF_NVDM_SONOFF_NVDM_H__ */
