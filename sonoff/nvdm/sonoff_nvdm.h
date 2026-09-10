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
#ifndef __SONOFF_NVDM_H__
#define __SONOFF_NVDM_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sonoff_nvdm_config.h"

/** @brief NVDM配置组名称. */
#define NVDM_USER_GROUP                 "user"      /* 用户配置组 */
#define NVDM_FAC_GROUP                  "factory"   /* 工厂配置组 */
#define NVDM_MATTER_GROUP               "matter"    /* Matter配置组 */

/** @brief 定义NVDM用户配置项. */
#define NVDM_USER_ITEM(x, y)        {NVDM_USER_GROUP, (x), (y), sizeof(y)}

/** @brief 定义NVDM工厂配置项. */
#define NVDM_FAC_ITEM(x, y)         {NVDM_FAC_GROUP, (x),  (y), sizeof(y)}

/** @brief 定义NVDM Matter配置项. */
#define NVDM_MATTER_ITEM(x, y)      {NVDM_MATTER_GROUP, (x), (y), sizeof(y)}

/**
 * @brief 显示全部NVDM配置项.
 *
 * @return 0表示执行成功.
 */
int snfNvdmShow(void);

/**
 * @brief 通过命令行写入NVDM配置项.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @param [in] value - 待写入的字符串值.
 */
void snfNvdmCliWriteItem(const char *group, const char *key, const char *value);

/**
 * @brief 通过命令行读取NVDM配置项.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 */
void snfNvdmCliReadItem(const char *group, const char *key);

/**
 * @brief 读取NVDM字符串配置项.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @param [out] buff - 读取缓冲区.
 * @param [in] len - 缓冲区长度.
 * @return 0表示读取成功, 其他值表示读取失败.
 */
int snfNvdmReadStr(const char *group, const char *key, uint8_t *buff, int len);

/**
 * @brief 写入NVDM字符串配置项.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @param [in] value - 待写入的字符串值.
 * @param [in] len - 字符串长度.
 * @return 0表示写入成功, 其他值表示写入失败.
 */
int snfNvdmWriteStr(const char *group, const char *key, const uint8_t *value, int len);

/**
 * @brief 读取NVDM整数配置项.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @return 配置项整数值或底层读取错误码.
 */
int snfNvdmReadInt(const char *group, const char *key);

/**
 * @brief 写入NVDM整数配置项.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @param [in] value - 待写入的整数值.
 * @return 0表示写入成功, 其他值表示写入失败.
 */
int snfNvdmWriteInt(const char *group, const char *key, int value);

/**
 * @brief 清理用户组NVDM配置项并恢复默认值.
 *
 * @return 0表示清理成功, 其他值表示清理失败.
 */
int snfNvdmCleanUserGroup(void);

/**
 * @brief 初始化NVDM模块.
 *
 * @return 0表示初始化成功, 其他值表示初始化失败.
 */
int snfNvdmInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __SONOFF_NVDM_H__ */
