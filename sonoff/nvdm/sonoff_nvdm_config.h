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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief Matter配置项键名. */
#define NVDM_MATTER_ITEM_DISCRIMINATOR      "discriminator"
#define NVDM_MATTER_ITEM_ITERATION_COUNT    "iteration.count"
#define NVDM_MATTER_ITEM_SALT               "salt"
#define NVDM_MATTER_ITEM_VERIFIER           "verifier"
#define NVDM_MATTER_ITEM_VENDOR_ID          "vendor.id"
#define NVDM_MATTER_ITEM_VENDOR_NAME        "vendor.name"
#define NVDM_MATTER_ITEM_PRODUCT_ID         "product.id"
#define NVDM_MATTER_ITEM_PRODUCT_NAME       "product.name"
#define NVDM_MATTER_ITEM_RD_ID_UID          "rd.id.uid"
#define NVDM_MATTER_ITEM_PASSCODE           "passcode"
#define NVDM_MATTER_ITEM_CD                 "CD"
#define NVDM_MATTER_ITEM_DAC_CERT           "DAC.CERT"
#define NVDM_MATTER_ITEM_DAC_KEY            "DAC.KEY"
#define NVDM_MATTER_ITEM_PAI_KEY            "PAI.KEY"

/** @brief 工厂配置项键名. */
#define NVDM_FACTORY_ITEM_SERIAL_NUMBER     "serial.number"
#define NVDM_FACTORY_ITEM_ACTIVE_CODE       "active.code"
#define NVDM_FACTORY_ITEM_DEVICE_ID         "device.id"
#define NVDM_FACTORY_ITEM_FACTORY_APIKEY    "factory.apikey"
#define NVDM_FACTORY_ITEM_DEVICE_MODEL      "device.model"
#define NVDM_FACTORY_ITEM_DEVICE_UIID       "device.uiid"
#define NVDM_FACTORY_ITEM_BASE_MAC          "base.mac"

/** @brief 工厂配置项长度. */
#define NVDM_FACTORY_SERIAL_NUMBER_LEN      14      /* 产品识别码十进制数字长度 */
#define NVDM_FACTORY_ACTIVE_CODE_LEN        16      /* 授权码二进制长度 */
#define NVDM_FACTORY_ACTIVE_CODE_HEX_LEN    32      /* 授权码十六进制字符串长度 */
#define NVDM_FACTORY_BASE_MAC_LEN           6       /* BASE MAC二进制长度 */
#define NVDM_FACTORY_BASE_MAC_STR_LEN       17      /* BASE MAC字符串, AA:BB:CC:DD:EE:FF */
#define NVDM_FACTORY_DEVICE_ID_LEN          10      /* 设备ID字符串长度 */
#define NVDM_FACTORY_APIKEY_LEN             36      /* factory.apikey字符串长度 */
#define NVDM_FACTORY_UIID_STR_MAX_LEN       10      /* uiid十进制字符串最大长度 */
#define NVDM_FACTORY_SHA256_HEX_LEN         64      /* License SHA256十六进制字符串长度 */

/** @brief Matter配置项长度与范围. */
#define NVDM_MATTER_DISCRIMINATOR_MAX           4095    /* 鉴别器最大值, 12-bit */
#define NVDM_MATTER_DISCRIMINATOR_STR_MAX_LEN   4       /* 鉴别器十进制字符串最大长度 */
#define NVDM_MATTER_DEC_U32_STR_MAX_LEN         10      /* 无符号32位十进制字符串最大长度 */
#define NVDM_MATTER_HEX_ID_MAX_LEN              4       /* 厂商ID与产品ID十六进制最大长度 */
#define NVDM_MATTER_RD_ID_UID_HEX_LEN           32      /* Rotating ID十六进制字符长度 */
#define NVDM_MATTER_NAME_MAX_LEN                32      /* 厂商名称与产品名称最大长度 */
#define NVDM_MATTER_SALT_STR_MAX_LEN            44      /* Salt的Base64字符串最大长度 */
#define NVDM_MATTER_VERIFIER_STR_MAX_LEN        132     /* Verifier的Base64字符串最大长度 */
#define NVDM_MATTER_CD_BIN_MAX_LEN              512     /* CD二进制最大长度 */
#define NVDM_MATTER_CD_B64_MAX_LEN              512     /* CD Base64最大长度 */

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
 * @brief 读取设备授权码.
 *
 * @param [out] active_code - 授权码缓冲区, 需能容纳32位十六进制和结束符.
 * @param [in] active_code_size - 缓冲区长度.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfActiveCodeGet(char *active_code, uint16_t active_code_size);

/**
 * @brief 写入设备授权码.
 *
 * 写入前用本机ChipID重新计算授权码, 与入参一致才允许写入.
 *
 * @param [in] active_code - 32位十六进制字符串.
 * @return 0表示写入成功, 负数表示参数非法、与本机不匹配或写入失败.
 */
int snfActiveCodeSet(const char *active_code);

/**
 * @brief 检查当前芯片是否已授权.
 *
 * 明文为芯片唯一ID的16字节AES块: 当前使用6字节MAC, 其余字节补0.
 *
 * @return 0表示已授权, 负数表示未授权或校验失败.
 */
int snfActiveCodeIsAuthorized(void);

/**
 * @brief 读取自定义BASE MAC.
 *
 * @param [out] base_mac - MAC缓冲区, 需能容纳AA:BB:CC:DD:EE:FF和结束符.
 * @param [in] base_mac_size - 缓冲区长度.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfBaseMacGet(char *base_mac, uint16_t base_mac_size);

/**
 * @brief 写入自定义BASE MAC.
 *
 * 格式为AA:BB:CC:DD:EE:FF, 拒绝全0和组播地址. 写入后同步到SDK RAM, 不写OTP和RF Flash.
 * STA等于BASE, AP由SDK从BASE派生.
 *
 * @param [in] base_mac - MAC字符串, 格式为AA:BB:CC:DD:EE:FF.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfBaseMacSet(const char *base_mac);

/**
 * @brief 将NVDM中的自定义BASE MAC应用到SDK RAM.
 *
 * 未设置或为空时不覆盖SDK MAC. 只改内存, 不写OTP和RF Flash.
 *
 * @return 0表示成功或无需覆盖, 负数表示应用失败.
 */
int snfBaseMacApply(void);

/**
 * @brief License写入结果.
 */
typedef enum
{
    SNF_LICENSE_OK = 0,             /**< 写入成功 */
    SNF_LICENSE_ERR_FRAME_LEN = -1, /**< license_frame长度不匹配 */
    SNF_LICENSE_ERR_SHA256 = -2,    /**< SHA256校验失败 */
    SNF_LICENSE_ERR_RULES = -3,     /**< 字段格式不符合规则 */
    SNF_LICENSE_ERR_MODEL = -4,     /**< device_model与固件不一致 */
    SNF_LICENSE_ERR_STORAGE = -5,   /**< 存储失败 */
} SnfLicenseResult;

/**
 * @brief 检查设备是否已烧录License.
 *
 * device.id非空视为已烧录.
 *
 * @return 0表示已烧录, 负数表示未烧录或读取失败.
 */
int snfLicenseIsBurned(void);

/**
 * @brief 清空License相关NVDM项.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfLicenseClear(void);

/**
 * @brief 解析License JSON并写入NVDM.
 *
 * 不立即应用BASE MAC, 下次上电由snfBaseMacApply生效.
 *
 * @param [in] json - 一行License JSON, 需以结束符结尾.
 * @param [out] reply_sha256 - C5应答SHA256缓冲区, 小写64位十六进制.
 * @param [in] reply_sha256_size - 缓冲区长度, 需大于NVDM_FACTORY_SHA256_HEX_LEN.
 * @return SnfLicenseResult.
 */
int snfLicenseWrite(const char *json, char *reply_sha256, uint16_t reply_sha256_size);

/**
 * @brief 读取已写入的License摘要信息.
 *
 * 五项均存在且格式合法时计算C5 SHA256. 未写入或校验异常返回失败.
 *
 * @param [out] uiid - uiid十进制字符串缓冲区.
 * @param [in] uiid_size - 缓冲区长度, 需大于NVDM_FACTORY_UIID_STR_MAX_LEN.
 * @param [out] device_model - device_model缓冲区.
 * @param [in] device_model_size - 缓冲区长度, 需大于NVDM_MATTER_NAME_MAX_LEN.
 * @param [out] sha256 - C5 SHA256缓冲区, 小写64位十六进制.
 * @param [in] sha256_size - 缓冲区长度, 需大于NVDM_FACTORY_SHA256_HEX_LEN.
 * @return 0表示成功, 负数表示未写入、校验异常或读取失败.
 */
int snfLicenseRead(char *uiid, uint16_t uiid_size, char *device_model, uint16_t device_model_size,
                   char *sha256, uint16_t sha256_size);

/**
 * @brief 读取Matter鉴别器.
 *
 * @param [out] discriminator - 鉴别器, 取值0到4095.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterDiscriminatorGet(uint16_t *discriminator);

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
 * @param [out] iteration_count - 迭代计数.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterIterationCountGet(uint32_t *iteration_count);

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
 * @param [out] vendor_id - 厂商ID, 由十六进制字符串解析.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterVendorIdGet(uint16_t *vendor_id);

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
 * @param [out] product_id - 产品ID, 由十六进制字符串解析.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterProductIdGet(uint16_t *product_id);

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
 * @param [out] passcode - 配对码.
 * @return 0表示读取成功, 负数表示未设置或读取失败.
 */
int snfMatterPasscodeGet(uint32_t *passcode);

/**
 * @brief 写入Matter配对码.
 *
 * @param [in] passcode - 十进制字符串.
 * @return 0表示写入成功, 负数表示参数非法或写入失败.
 */
int snfMatterPasscodeSet(const char *passcode);

/**
 * @brief 写入Matter CD.
 *
 * <len>为Base64文本长度. SHA256只对解码后的二进制CD计算. NVDM存储Base64文本.
 *
 * @param [in] data_len - Base64文本长度的十进制字符串.
 * @param [in] base64 - CD的Base64字符串.
 * @param [in] sha256_hex - 二进制CD的64位十六进制SHA256.
 * @return 0表示写入成功, 负数表示参数非法、校验失败或写入失败.
 */
int snfMatterCdWrite(const char *data_len, const char *base64, const char *sha256_hex);

/**
 * @brief 读取Matter CD.
 *
 * 未写入或数据非法时失败.
 *
 * @param [out] data_len - Base64文本长度.
 * @param [out] base64 - Base64缓冲区.
 * @param [in] base64_size - 缓冲区长度, 需大于NVDM_MATTER_CD_B64_MAX_LEN.
 * @param [out] sha256_hex - 二进制CD的SHA256缓冲区, 小写64位十六进制.
 * @param [in] sha256_size - 缓冲区长度, 需大于NVDM_FACTORY_SHA256_HEX_LEN.
 * @return 0表示读取成功, 负数表示未写入或读取失败.
 */
int snfMatterCdRead(uint16_t *data_len, char *base64, uint16_t base64_size,
                    char *sha256_hex, uint16_t sha256_size);

/**
 * @brief 清空Matter CD.
 *
 * 原本未写入时仍写成空值.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfMatterCdClear(void);

/**
 * @brief 读取Matter CD二进制.
 *
 * @param [out] cd - 二进制缓冲区.
 * @param [in] cd_size - 缓冲区长度.
 * @param [out] cd_len - 实际二进制长度.
 * @return 0表示读取成功, 负数表示未写入或读取失败.
 */
int snfMatterCdGet(uint8_t *cd, uint16_t cd_size, uint16_t *cd_len);

#ifdef __cplusplus
}
#endif
#endif  /* __SONOFF_NVDM_CONFIG_H__ */
