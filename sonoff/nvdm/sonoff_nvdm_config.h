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

/** @brief 鉴别器最大值, 12-bit. */
#define NVDM_MATTER_DISCRIMINATOR_MAX       4095

/** @brief 鉴别器十进制字符串最大长度. */
#define NVDM_MATTER_DISCRIMINATOR_STR_MAX_LEN   4

/** @brief 无符号32位十进制字符串最大长度. */
#define NVDM_MATTER_DEC_U32_STR_MAX_LEN     10

/** @brief 厂商ID与产品ID十六进制字符串最大长度. */
#define NVDM_MATTER_HEX_ID_MAX_LEN          4

/** @brief Rotating ID唯一编号十六进制字符长度. */
#define NVDM_MATTER_RD_ID_UID_HEX_LEN       32

/** @brief 厂商名称与产品名称最大长度. */
#define NVDM_MATTER_NAME_MAX_LEN            32

/** @brief Salt的Base64字符串最大长度. */
#define NVDM_MATTER_SALT_STR_MAX_LEN        44

/** @brief Verifier的Base64字符串最大长度. */
#define NVDM_MATTER_VERIFIER_STR_MAX_LEN    132

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

/**
 * @brief 读取Matter鉴别器.
 *
 * @param [out] discriminator - 鉴别器缓冲区.
 * @param [in] discriminator_size - 缓冲区长度, 需大于NVDM_MATTER_DISCRIMINATOR_STR_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterDiscriminatorGet(char *discriminator, uint16_t discriminator_size);

/**
 * @brief 写入Matter鉴别器.
 *
 * @param [in] discriminator - 十进制字符串, 取值0到4095.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterDiscriminatorSet(const char *discriminator);

/**
 * @brief 读取Matter迭代计数.
 *
 * @param [out] iteration_count - 迭代计数缓冲区.
 * @param [in] iteration_count_size - 缓冲区长度, 需大于NVDM_MATTER_DEC_U32_STR_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterIterationCountGet(char *iteration_count, uint16_t iteration_count_size);

/**
 * @brief 写入Matter迭代计数.
 *
 * @param [in] iteration_count - 十进制字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterIterationCountSet(const char *iteration_count);

/**
 * @brief 读取Matter Salt.
 *
 * @param [out] salt - Salt缓冲区.
 * @param [in] salt_size - 缓冲区长度, 需大于NVDM_MATTER_SALT_STR_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterSaltGet(char *salt, uint16_t salt_size);

/**
 * @brief 写入Matter Salt.
 *
 * @param [in] salt - Base64字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterSaltSet(const char *salt);

/**
 * @brief 读取Matter Verifier.
 *
 * @param [out] verifier - Verifier缓冲区.
 * @param [in] verifier_size - 缓冲区长度, 需大于NVDM_MATTER_VERIFIER_STR_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterVerifierGet(char *verifier, uint16_t verifier_size);

/**
 * @brief 写入Matter Verifier.
 *
 * @param [in] verifier - Base64字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterVerifierSet(const char *verifier);

/**
 * @brief 读取Matter厂商ID.
 *
 * @param [out] vendor_id - 厂商ID缓冲区.
 * @param [in] vendor_id_size - 缓冲区长度, 需大于NVDM_MATTER_HEX_ID_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterVendorIdGet(char *vendor_id, uint16_t vendor_id_size);

/**
 * @brief 写入Matter厂商ID.
 *
 * @param [in] vendor_id - 十六进制字符串, 不含0x.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterVendorIdSet(const char *vendor_id);

/**
 * @brief 读取Matter厂商名称.
 *
 * @param [out] vendor_name - 厂商名称缓冲区.
 * @param [in] vendor_name_size - 缓冲区长度, 需大于NVDM_MATTER_NAME_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterVendorNameGet(char *vendor_name, uint16_t vendor_name_size);

/**
 * @brief 写入Matter厂商名称.
 *
 * @param [in] vendor_name - 英文字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterVendorNameSet(const char *vendor_name);

/**
 * @brief 读取Matter产品ID.
 *
 * @param [out] product_id - 产品ID缓冲区.
 * @param [in] product_id_size - 缓冲区长度, 需大于NVDM_MATTER_HEX_ID_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterProductIdGet(char *product_id, uint16_t product_id_size);

/**
 * @brief 写入Matter产品ID.
 *
 * @param [in] product_id - 十六进制字符串, 不含0x.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterProductIdSet(const char *product_id);

/**
 * @brief 读取Matter产品名称.
 *
 * @param [out] product_name - 产品名称缓冲区.
 * @param [in] product_name_size - 缓冲区长度, 需大于NVDM_MATTER_NAME_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterProductNameGet(char *product_name, uint16_t product_name_size);

/**
 * @brief 写入Matter产品名称.
 *
 * @param [in] product_name - 英文字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterProductNameSet(const char *product_name);

/**
 * @brief 读取Matter Rotating ID.
 *
 * @param [out] rd_id_uid - Rotating ID缓冲区.
 * @param [in] rd_id_uid_size - 缓冲区长度, 需大于NVDM_MATTER_RD_ID_UID_HEX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterRdIdUidGet(char *rd_id_uid, uint16_t rd_id_uid_size);

/**
 * @brief 写入Matter Rotating ID.
 *
 * @param [in] rd_id_uid - 32位十六进制字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterRdIdUidSet(const char *rd_id_uid);

/**
 * @brief 读取Matter配对码.
 *
 * @param [out] passcode - 配对码缓冲区.
 * @param [in] passcode_size - 缓冲区长度, 需大于NVDM_MATTER_DEC_U32_STR_MAX_LEN.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterPasscodeGet(char *passcode, uint16_t passcode_size);

/**
 * @brief 写入Matter配对码.
 *
 * @param [in] passcode - 十进制字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterPasscodeSet(const char *passcode);

#endif  /* __SONOFF_NVDM_CONFIG_H__ */
