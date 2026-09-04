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

#include "sonoff_base64.h"
#include "sonoff_log.h"
#include "sonoff_nvdm.h"

#include "sonoff_nvdm_config.h"

static const char *tag = "SNF-NVDM-CFG";

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

int snfMatterDiscriminatorGet(char *discriminator, uint16_t discriminator_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_DISCRIMINATOR,
                         discriminator,
                         discriminator_size,
                         NVDM_MATTER_DISCRIMINATOR_STR_MAX_LEN,
                         discriminatorIsValid);
}

int snfMatterDiscriminatorSet(const char *discriminator)
{
    return matterItemSet(NVDM_MATTER_ITEM_DISCRIMINATOR, discriminator, discriminatorIsValid);
}

int snfMatterIterationCountGet(char *iteration_count, uint16_t iteration_count_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_ITERATION_COUNT,
                         iteration_count,
                         iteration_count_size,
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

int snfMatterVendorIdGet(char *vendor_id, uint16_t vendor_id_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_VENDOR_ID,
                         vendor_id,
                         vendor_id_size,
                         NVDM_MATTER_HEX_ID_MAX_LEN,
                         hexIdIsValid);
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

int snfMatterProductIdGet(char *product_id, uint16_t product_id_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_PRODUCT_ID,
                         product_id,
                         product_id_size,
                         NVDM_MATTER_HEX_ID_MAX_LEN,
                         hexIdIsValid);
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

int snfMatterPasscodeGet(char *passcode, uint16_t passcode_size)
{
    return matterItemGet(NVDM_MATTER_ITEM_PASSCODE,
                         passcode,
                         passcode_size,
                         NVDM_MATTER_DEC_U32_STR_MAX_LEN,
                         decU32IsValid);
}

int snfMatterPasscodeSet(const char *passcode)
{
    return matterItemSet(NVDM_MATTER_ITEM_PASSCODE, passcode, decU32IsValid);
}
