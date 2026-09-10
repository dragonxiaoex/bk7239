/**
 * @file    sonoff_nvdm_port.h
 * @brief   NVDM平台适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
 #ifndef __SONOFF_NVDM_PORT_H__
 #define __SONOFF_NVDM_PORT_H__

#include <stdint.h>

/**
 * @brief 查询NVDM键值对是否存在.
 *
 * group与key拼接为EasyFlash键名, 例如group为"user"、key为"wifi.ssid"时实际键为"user.wifi.ssid".
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @return 0表示存在, 负数表示不存在或查询失败.
 */
int snfNvdmPortExistStatus(const char *group, const char *key);

/**
 * @brief 读取NVDM字符串键值对.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @param [out] buff - 读取缓冲区.
 * @param [in] len - 缓冲区长度.
 * @return 0表示读取成功, 负数表示读取失败.
 */
int snfNvdmPortReadStr(const char *group, const char *key, uint8_t *buff, int len);

/**
 * @brief 写入NVDM字符串键值对.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @param [in] value - 待写入数据.
 * @param [in] len - 待写入长度.
 * @return 0表示写入成功, 负数表示写入失败.
 */
int snfNvdmPortWriteStr(const char *group, const char *key, const uint8_t *value, int len);

/**
 * @brief 删除NVDM键值对.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @return 0表示删除成功或不存在, 负数表示删除失败.
 */
int snfNvdmPortDelete(const char *group, const char *key);

#endif /* __SONOFF_NVDM_PORT_H__ */
