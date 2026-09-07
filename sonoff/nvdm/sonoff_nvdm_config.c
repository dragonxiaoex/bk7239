/**
 * @file    sonoff_nvdm_config.c
 * @brief   NVDM条目配置
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>
#include <string.h>

#include <components/system.h>

#include "mbedtls/aes.h"

#include "sonoff_base64.h"
#include "sonoff_log.h"
#include "sonoff_nvdm.h"

#include "sonoff_nvdm_config.h"

static const char *tag = "SNF-NVDM-CFG";

#define ACTIVE_CODE_AES_KEY             "soNoFF22soNoFF22"
#define ACTIVE_CODE_AES_KEYBITS         128

/**
 * @brief Matter配置项合法性检查函数.
 */
typedef int (*MatterItemIsValid)(const char *value);

/**
 * @brief 检查产品识别码是否为14位十进制数字.
 *
 * @param [in] serial_number - 待检查的识别码字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int serialNumberIsValid(const char *serial_number)
{
    uint16_t i;

    if (serial_number == NULL)
    {
        return 0;
    }

    if (strlen(serial_number) != NVDM_FACTORY_SERIAL_NUMBER_LEN)
    {
        return 0;
    }

    for (i = 0; i < NVDM_FACTORY_SERIAL_NUMBER_LEN; i++)
    {
        if ((serial_number[i] < '0') || (serial_number[i] > '9'))
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 将十六进制字符转换为半字节.
 *
 * @param [in] ch - 十六进制字符.
 * @param [out] nibble - 半字节值.
 * @return 1表示转换成功, 0表示字符非法.
 */
static int hexNibble(char ch, uint8_t *nibble)
{
    if (nibble == NULL)
    {
        return 0;
    }

    if ((ch >= '0') && (ch <= '9'))
    {
        *nibble = (uint8_t)(ch - '0');
        return 1;
    }

    if ((ch >= 'A') && (ch <= 'F'))
    {
        *nibble = (uint8_t)((ch - 'A') + 10);
        return 1;
    }

    if ((ch >= 'a') && (ch <= 'f'))
    {
        *nibble = (uint8_t)((ch - 'a') + 10);
        return 1;
    }

    return 0;
}

/**
 * @brief 解析十进制无符号32位整数.
 *
 * @param [in] str - 十进制字符串.
 * @param [out] value - 解析结果, 不需要结果时可为NULL.
 * @return 1表示解析成功, 0表示非法.
 */
static int parseDecU32(const char *str, uint32_t *value)
{
    uint32_t result = 0;
    uint16_t i;
    uint8_t digit;

    if ((str == NULL) || (str[0] == '\0'))
    {
        return 0;
    }

    for (i = 0; str[i] != '\0'; i++)
    {
        if ((str[i] < '0') || (str[i] > '9'))
        {
            return 0;
        }

        digit = (uint8_t)(str[i] - '0');
        if (result > ((UINT32_MAX - (uint32_t)digit) / 10))
        {
            return 0;
        }

        result = (result * 10) + digit;
    }

    if (value != NULL)
    {
        *value = result;
    }

    return 1;
}

/**
 * @brief 解析十六进制无符号16位整数.
 *
 * @param [in] str - 十六进制字符串, 不含0x.
 * @param [out] value - 解析结果.
 * @return 1表示解析成功, 0表示非法.
 */
static int parseHexU16(const char *str, uint16_t *value)
{
    uint16_t result = 0;
    uint16_t i;
    uint8_t nibble;

    if ((str == NULL) || (str[0] == '\0') || (value == NULL))
    {
        return 0;
    }

    for (i = 0; str[i] != '\0'; i++)
    {
        if (i >= NVDM_MATTER_HEX_ID_MAX_LEN)
        {
            return 0;
        }

        if (hexNibble(str[i], &nibble) == 0)
        {
            return 0;
        }

        result = (uint16_t)((result << 4) + nibble);
    }

    *value = result;

    return 1;
}

/**
 * @brief 检查十六进制字符串长度与字符是否合法.
 *
 * @param [in] str - 十六进制字符串.
 * @param [in] min_len - 最小长度.
 * @param [in] max_len - 最大长度.
 * @return 1表示格式合法, 0表示非法.
 */
static int hexStringIsValid(const char *str, uint16_t min_len, uint16_t max_len)
{
    uint16_t i;
    uint8_t nibble;

    if ((str == NULL) || (str[0] == '\0') || (min_len == 0) || (min_len > max_len))
    {
        return 0;
    }

    for (i = 0; str[i] != '\0'; i++)
    {
        if (i >= max_len)
        {
            return 0;
        }

        if (hexNibble(str[i], &nibble) == 0)
        {
            return 0;
        }
    }

    if (i < min_len)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 将十六进制字符串解码为字节.
 *
 * @param [in] hex - 十六进制字符串, 长度必须为out_len的2倍.
 * @param [out] out - 解码缓冲区.
 * @param [in] out_len - 目标字节长度.
 * @return 0表示解码成功, 负数表示失败.
 */
static int hexBytesDecode(const char *hex, uint8_t *out, uint16_t out_len)
{
    uint16_t i;
    uint8_t high;
    uint8_t low;

    if ((hex == NULL) || (out == NULL) || (out_len == 0))
    {
        return -1;
    }

    if (hexStringIsValid(hex, (uint16_t)(out_len * 2), (uint16_t)(out_len * 2)) == 0)
    {
        return -1;
    }

    for (i = 0; i < out_len; i++)
    {
        if (hexNibble(hex[i * 2], &high) == 0)
        {
            return -1;
        }

        if (hexNibble(hex[(i * 2) + 1], &low) == 0)
        {
            return -1;
        }

        out[i] = (uint8_t)((high << 4) + low);
    }

    return 0;
}

/**
 * @brief 检查名称是否为非空可打印ASCII且不含逗号.
 *
 * @param [in] name - 名称字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int nameIsValid(const char *name)
{
    uint16_t i;

    if ((name == NULL) || (name[0] == '\0'))
    {
        return 0;
    }

    for (i = 0; name[i] != '\0'; i++)
    {
        if (i >= NVDM_MATTER_NAME_MAX_LEN)
        {
            return 0;
        }

        if ((name[i] < ' ') || (name[i] > '~') || (name[i] == ','))
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 检查Base64字符串是否可解码、非空且不超过最大长度.
 *
 * @param [in] str - Base64字符串.
 * @param [in] max_len - 编码字符串最大长度.
 * @return 1表示格式合法, 0表示非法.
 */
static int base64IsValid(const char *str, uint16_t max_len)
{
    uint32_t out_len;
    int ret;

    if ((str == NULL) || (str[0] == '\0'))
    {
        return 0;
    }

    if (strlen(str) > max_len)
    {
        return 0;
    }

    out_len = 0;
    ret = snfBase64Decode(NULL, 0, &out_len, (const uint8_t *)str, (uint32_t)strlen(str));
    if ((ret != SNF_BASE64_OK) || (out_len == 0))
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 检查鉴别器格式.
 *
 * @param [in] value - 鉴别器字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int discriminatorIsValid(const char *value)
{
    uint32_t discriminator;

    if (parseDecU32(value, &discriminator) == 0)
    {
        return 0;
    }

    if (discriminator > NVDM_MATTER_DISCRIMINATOR_MAX)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 检查无符号32位十进制字符串格式.
 *
 * @param [in] value - 十进制字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int decU32IsValid(const char *value)
{
    return parseDecU32(value, NULL);
}

/**
 * @brief 检查Salt格式.
 *
 * @param [in] value - Salt字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int saltIsValid(const char *value)
{
    return base64IsValid(value, NVDM_MATTER_SALT_STR_MAX_LEN);
}

/**
 * @brief 检查Verifier格式.
 *
 * @param [in] value - Verifier字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int verifierIsValid(const char *value)
{
    return base64IsValid(value, NVDM_MATTER_VERIFIER_STR_MAX_LEN);
}

/**
 * @brief 检查厂商ID或产品ID格式.
 *
 * @param [in] value - 十六进制ID字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int hexIdIsValid(const char *value)
{
    return hexStringIsValid(value, 1, NVDM_MATTER_HEX_ID_MAX_LEN);
}

/**
 * @brief 检查Rotating ID格式.
 *
 * @param [in] value - Rotating ID字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int rdIdUidIsValid(const char *value)
{
    return hexStringIsValid(value, NVDM_MATTER_RD_ID_UID_HEX_LEN, NVDM_MATTER_RD_ID_UID_HEX_LEN);
}

/**
 * @brief 检查授权码是否为32位十六进制字符串.
 *
 * @param [in] active_code - 待检查的授权码字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int activeCodeIsValid(const char *active_code)
{
    return hexStringIsValid(active_code,
                            NVDM_FACTORY_ACTIVE_CODE_HEX_LEN,
                            NVDM_FACTORY_ACTIVE_CODE_HEX_LEN);
}

/**
 * @brief 读取用于授权计算的16字节芯片明文.
 *
 * @param [out] plain - 16字节明文缓冲区, 前6字节为MAC, 其余补0.
 * @param [in] plain_size - 缓冲区长度.
 * @return 0表示成功, 负数表示失败.
 */
static int activeCodeGetChipPlain(uint8_t *plain, uint16_t plain_size)
{
    if ((plain == NULL) || (plain_size < NVDM_FACTORY_ACTIVE_CODE_LEN))
    {
        return -1;
    }

    memset(plain, 0, NVDM_FACTORY_ACTIVE_CODE_LEN);
    if (bk_get_mac(plain, MAC_TYPE_BASE) != BK_OK)
    {
        return -1;
    }

    if (BK_IS_ZERO_MAC(plain))
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 使用固定密钥对16字节明文做AES-ECB加密.
 *
 * @param [in] plain - 16字节明文.
 * @param [out] cipher - 16字节密文.
 * @return 0表示成功, 负数表示失败.
 */
static int activeCodeEncrypt(const uint8_t *plain, uint8_t *cipher)
{
    mbedtls_aes_context aes;
    int ret;

    if ((plain == NULL) || (cipher == NULL))
    {
        return -1;
    }

    mbedtls_aes_init(&aes);
    ret = mbedtls_aes_setkey_enc(&aes,
                                 (const unsigned char *)ACTIVE_CODE_AES_KEY,
                                 ACTIVE_CODE_AES_KEYBITS);
    if (ret != 0)
    {
        mbedtls_aes_free(&aes);
        return -1;
    }

    ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plain, cipher);
    mbedtls_aes_free(&aes);
    if (ret != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 检查授权码是否与本机ChipID计算出的结果一致.
 *
 * @param [in] active_code - 32位十六进制授权码.
 * @return 0表示一致, 负数表示不一致或计算失败.
 */
static int activeCodeMatchesChip(const char *active_code)
{
    uint8_t input[NVDM_FACTORY_ACTIVE_CODE_LEN];
    uint8_t plain[NVDM_FACTORY_ACTIVE_CODE_LEN];
    uint8_t expect[NVDM_FACTORY_ACTIVE_CODE_LEN];

    if (hexBytesDecode(active_code, input, NVDM_FACTORY_ACTIVE_CODE_LEN) != 0)
    {
        return -1;
    }

    if (activeCodeGetChipPlain(plain, sizeof(plain)) != 0)
    {
        return -1;
    }

    if (activeCodeEncrypt(plain, expect) != 0)
    {
        return -1;
    }

    if (memcmp(input, expect, NVDM_FACTORY_ACTIVE_CODE_LEN) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 读取Matter配置项.
 *
 * @param [in] key - 配置键名称.
 * @param [out] buff - 读取缓冲区.
 * @param [in] buff_size - 缓冲区长度.
 * @param [in] max_len - 配置项最大字符串长度, 不含结束符.
 * @param [in] is_valid - 合法性检查函数.
 * @return 0表示读取成功, 负数表示失败.
 */
static int matterItemGet(const char *key,
                         char *buff,
                         uint16_t buff_size,
                         uint16_t max_len,
                         MatterItemIsValid is_valid)
{
    int ret;

    if ((key == NULL) || (buff == NULL) || (is_valid == NULL) || (buff_size <= max_len))
    {
        return -1;
    }

    memset(buff, 0, buff_size);
    ret = snfNvdmReadStr(NVDM_MATTER_GROUP, key, (uint8_t *)buff, (int)buff_size);
    if (ret != 0)
    {
        return -1;
    }

    if (is_valid(buff) == 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 写入Matter配置项.
 *
 * @param [in] key - 配置键名称.
 * @param [in] value - 配置值.
 * @param [in] is_valid - 合法性检查函数.
 * @return 0表示写入成功, 负数表示失败.
 */
static int matterItemSet(const char *key, const char *value, MatterItemIsValid is_valid)
{
    int len;
    int ret;

    if ((key == NULL) || (is_valid == NULL) || (is_valid(value) == 0))
    {
        return -1;
    }

    len = (int)strlen(value) + 1;
    ret = snfNvdmWriteStr(NVDM_MATTER_GROUP, key, (const uint8_t *)value, len);
    if (ret != 0)
    {
        LOG_E(tag, "matter item %s write failed", key);
        return -1;
    }

    return 0;
}

/**
 * @brief 读取Matter十进制配置项并解析为整数.
 *
 * @param [in] key - 配置键名称.
 * @param [out] value - 解析后的整数值.
 * @param [in] max_len - 配置项最大字符串长度, 不含结束符.
 * @param [in] is_valid - 合法性检查函数.
 * @return 0表示读取成功, 负数表示失败.
 */
static int matterItemGetDec(const char *key,
                            uint32_t *value,
                            uint16_t max_len,
                            MatterItemIsValid is_valid)
{
    char buff[NVDM_MATTER_DEC_U32_STR_MAX_LEN + 1];

    if (value == NULL)
    {
        return -1;
    }

    if (matterItemGet(key, buff, sizeof(buff), max_len, is_valid) != 0)
    {
        return -1;
    }

    if (parseDecU32(buff, value) == 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 读取Matter十六进制ID配置项并解析为整数.
 *
 * @param [in] key - 配置键名称.
 * @param [out] value - 解析后的ID.
 * @return 0表示读取成功, 负数表示失败.
 */
static int matterItemGetHexU16(const char *key, uint16_t *value)
{
    char buff[NVDM_MATTER_HEX_ID_MAX_LEN + 1];

    if (value == NULL)
    {
        return -1;
    }

    if (matterItemGet(key, buff, sizeof(buff), NVDM_MATTER_HEX_ID_MAX_LEN, hexIdIsValid) != 0)
    {
        return -1;
    }

    if (parseHexU16(buff, value) == 0)
    {
        return -1;
    }

    return 0;
}

int snfSerialNumberGet(char *serial_number, uint16_t serial_number_size)
{
    int ret;

    if ((serial_number == NULL) || (serial_number_size <= NVDM_FACTORY_SERIAL_NUMBER_LEN))
    {
        return -1;
    }

    memset(serial_number, 0, serial_number_size);
    ret = snfNvdmReadStr(NVDM_FAC_GROUP,
                         NVDM_FACTORY_ITEM_SERIAL_NUMBER,
                         (uint8_t *)serial_number,
                         (int)serial_number_size);
    if (ret != 0)
    {
        return -1;
    }

    if (serialNumberIsValid(serial_number) == 0)
    {
        return -1;
    }

    return 0;
}

int snfSerialNumberSet(const char *serial_number)
{
    int len;
    int ret;

    if (serialNumberIsValid(serial_number) == 0)
    {
        return -1;
    }

    len = (int)strlen(serial_number) + 1;
    ret = snfNvdmWriteStr(NVDM_FAC_GROUP,
                          NVDM_FACTORY_ITEM_SERIAL_NUMBER,
                          (const uint8_t *)serial_number,
                          len);
    if (ret != 0)
    {
        LOG_E(tag, "serial number write failed");
        return -1;
    }

    return 0;
}

int snfActiveCodeGet(char *active_code, uint16_t active_code_size)
{
    int ret;

    if ((active_code == NULL) || (active_code_size <= NVDM_FACTORY_ACTIVE_CODE_HEX_LEN))
    {
        return -1;
    }

    memset(active_code, 0, active_code_size);
    ret = snfNvdmReadStr(NVDM_FAC_GROUP,
                         NVDM_FACTORY_ITEM_ACTIVE_CODE,
                         (uint8_t *)active_code,
                         (int)active_code_size);
    if (ret != 0)
    {
        return -1;
    }

    if (activeCodeIsValid(active_code) == 0)
    {
        return -1;
    }

    return 0;
}

int snfActiveCodeSet(const char *active_code)
{
    int len;
    int ret;

    if (activeCodeIsValid(active_code) == 0)
    {
        return -1;
    }

    if (activeCodeMatchesChip(active_code) != 0)
    {
        return -1;
    }

    len = (int)strlen(active_code) + 1;
    ret = snfNvdmWriteStr(NVDM_FAC_GROUP,
                          NVDM_FACTORY_ITEM_ACTIVE_CODE,
                          (const uint8_t *)active_code,
                          len);
    if (ret != 0)
    {
        return -1;
    }

    return 0;
}

int snfActiveCodeIsAuthorized(void)
{
    char stored_hex[NVDM_FACTORY_ACTIVE_CODE_HEX_LEN + 1];

    if (snfActiveCodeGet(stored_hex, sizeof(stored_hex)) != 0)
    {
        return -1;
    }

    return activeCodeMatchesChip(stored_hex);
}

int snfMatterDiscriminatorGet(uint16_t *discriminator)
{
    uint32_t value;

    if (discriminator == NULL)
    {
        return -1;
    }

    if (matterItemGetDec(NVDM_MATTER_ITEM_DISCRIMINATOR,
                         &value,
                         NVDM_MATTER_DISCRIMINATOR_STR_MAX_LEN,
                         discriminatorIsValid) != 0)
    {
        return -1;
    }

    *discriminator = (uint16_t)value;

    return 0;
}

int snfMatterDiscriminatorSet(const char *discriminator)
{
    return matterItemSet(NVDM_MATTER_ITEM_DISCRIMINATOR, discriminator, discriminatorIsValid);
}

int snfMatterIterationCountGet(uint32_t *iteration_count)
{
    return matterItemGetDec(NVDM_MATTER_ITEM_ITERATION_COUNT,
                            iteration_count,
                            NVDM_MATTER_DEC_U32_STR_MAX_LEN,
                            decU32IsValid);
}

int snfMatterIterationCountSet(const char *iteration_count)
{
    return matterItemSet(NVDM_MATTER_ITEM_ITERATION_COUNT, iteration_count, decU32IsValid);
}

int snfMatterSaltGet(char *salt, uint16_t salt_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_SALT,
                         salt,
                         salt_size,
                         NVDM_MATTER_SALT_STR_MAX_LEN,
                         saltIsValid);
}

int snfMatterSaltSet(const char *salt)
{
    return matterItemSet(NVDM_MATTER_ITEM_SALT, salt, saltIsValid);
}

int snfMatterVerifierGet(char *verifier, uint16_t verifier_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_VERIFIER,
                         verifier,
                         verifier_size,
                         NVDM_MATTER_VERIFIER_STR_MAX_LEN,
                         verifierIsValid);
}

int snfMatterVerifierSet(const char *verifier)
{
    return matterItemSet(NVDM_MATTER_ITEM_VERIFIER, verifier, verifierIsValid);
}

int snfMatterVendorIdGet(uint16_t *vendor_id)
{
    return matterItemGetHexU16(NVDM_MATTER_ITEM_VENDOR_ID, vendor_id);
}

int snfMatterVendorIdSet(const char *vendor_id)
{
    return matterItemSet(NVDM_MATTER_ITEM_VENDOR_ID, vendor_id, hexIdIsValid);
}

int snfMatterVendorNameGet(char *vendor_name, uint16_t vendor_name_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_VENDOR_NAME,
                         vendor_name,
                         vendor_name_size,
                         NVDM_MATTER_NAME_MAX_LEN,
                         nameIsValid);
}

int snfMatterVendorNameSet(const char *vendor_name)
{
    return matterItemSet(NVDM_MATTER_ITEM_VENDOR_NAME, vendor_name, nameIsValid);
}

int snfMatterProductIdGet(uint16_t *product_id)
{
    return matterItemGetHexU16(NVDM_MATTER_ITEM_PRODUCT_ID, product_id);
}

int snfMatterProductIdSet(const char *product_id)
{
    return matterItemSet(NVDM_MATTER_ITEM_PRODUCT_ID, product_id, hexIdIsValid);
}

int snfMatterProductNameGet(char *product_name, uint16_t product_name_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_PRODUCT_NAME,
                         product_name,
                         product_name_size,
                         NVDM_MATTER_NAME_MAX_LEN,
                         nameIsValid);
}

int snfMatterProductNameSet(const char *product_name)
{
    return matterItemSet(NVDM_MATTER_ITEM_PRODUCT_NAME, product_name, nameIsValid);
}

int snfMatterRdIdUidGet(char *rd_id_uid, uint16_t rd_id_uid_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_RD_ID_UID,
                         rd_id_uid,
                         rd_id_uid_size,
                         NVDM_MATTER_RD_ID_UID_HEX_LEN,
                         rdIdUidIsValid);
}

int snfMatterRdIdUidSet(const char *rd_id_uid)
{
    return matterItemSet(NVDM_MATTER_ITEM_RD_ID_UID, rd_id_uid, rdIdUidIsValid);
}

int snfMatterPasscodeGet(uint32_t *passcode)
{
    return matterItemGetDec(NVDM_MATTER_ITEM_PASSCODE,
                            passcode,
                            NVDM_MATTER_DEC_U32_STR_MAX_LEN,
                            decU32IsValid);
}

int snfMatterPasscodeSet(const char *passcode)
{
    return matterItemSet(NVDM_MATTER_ITEM_PASSCODE, passcode, decU32IsValid);
}
