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

#include <stdint.h>

#define NVDM_USER_GROUP                 "user."      /* 用户配置组 */
#define NVDM_FAC_GROUP                  "factory."   /* 工厂配置组 */
#define NVDM_MATTER_GROUP               "matter."    /* Matter配置组 */

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
 * @param [in] key_name - 配置项名称.
 * @param [in] value - 待写入的字符串值.
 */
void snfNvdmCliWriteItem(const char *group, const char *key, const char *value);

/**
 * @brief 通过命令行读取NVDM配置项.
 *
 * @param [in] key_name - 配置项名称.
 */
void snfNvdmCliReadItem(const char *group, const char *key);

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

/**
 * @brief 清理NVDM配置项.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @return 0表示清理成功, 其他值表示清理失败.
 */
int snfNvdmCleanUserGroup(void);

/**
 * @brief 初始化NVDM模块.
 *
 * @return 0表示初始化成功.
 */
int snfNvdmInit(void);

#endif /* __SONOFF_NVDM_H__ */
