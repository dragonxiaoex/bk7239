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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <components/system.h>

#include "cJSON.h"
#include "mbedtls/aes.h"

#include "sonoff_base64.h"
#include "sonoff_log.h"
#include "sonoff_nvdm.h"
#include "sonoff_nvdm_config.h"
#include "sonoff_project_config.h"
#include "sonoff_sha256.h"

static const char *tag = "SNF-NVDM-CFG";

#define ACTIVE_CODE_AES_KEY             "soNoFF22soNoFF22"
#define ACTIVE_CODE_AES_KEYBITS         128

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
 * @brief 解析并校验BASE MAC字符串.
 *
 * @param [in] str - MAC字符串, 格式为AA:BB:CC:DD:EE:FF.
 * @param [out] mac - 6字节MAC缓冲区.
 * @return 0表示合法, 负数表示非法.
 */
static int baseMacParse(const char *str, uint8_t *mac)
{
    uint16_t i;
    uint8_t high;
    uint8_t low;

    if ((str == NULL) || (mac == NULL))
    {
        return -1;
    }

    if (strlen(str) != NVDM_FACTORY_BASE_MAC_STR_LEN)
    {
        return -1;
    }

    for (i = 0; i < NVDM_FACTORY_BASE_MAC_LEN; i++)
    {
        if (hexNibble(str[i * 3], &high) == 0)
        {
            return -1;
        }

        if (hexNibble(str[(i * 3) + 1], &low) == 0)
        {
            return -1;
        }

        if ((i < (NVDM_FACTORY_BASE_MAC_LEN - 1)) && (str[(i * 3) + 2] != ':'))
        {
            return -1;
        }

        mac[i] = (uint8_t)((high << 4) + low);
    }

    if (BK_IS_ZERO_MAC(mac) || BK_IS_GROUP_MAC(mac))
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 检查factory.apikey是否为UUID格式.
 *
 * @param [in] apikey - API密钥.
 * @return 1表示格式合法, 0表示非法.
 */
static int factoryApikeyIsValid(const char *apikey)
{
    uint16_t i;
    uint8_t nibble;

    if ((apikey == NULL) || (strlen(apikey) != NVDM_FACTORY_APIKEY_LEN))
    {
        return 0;
    }

    for (i = 0; i < NVDM_FACTORY_APIKEY_LEN; i++)
    {
        if ((i == 8) || (i == 13) || (i == 18) || (i == 23))
        {
            if (apikey[i] != '-')
            {
                return 0;
            }
        }
        else
        {
            if (hexNibble(apikey[i], &nibble) == 0)
            {
                return 0;
            }
        }
    }

    return 1;
}

/**
 * @brief 写入工厂组字符串配置项.
 *
 * @param [in] key - 配置键名称.
 * @param [in] value - 配置值.
 * @return 0表示成功, 负数表示失败.
 */
static int factoryStrWrite(const char *key, const char *value)
{
    int len;

    if ((key == NULL) || (value == NULL))
    {
        return -1;
    }

    len = (int)strlen(value) + 1;
    if (snfNvdmWriteStr(NVDM_FAC_GROUP, key, (const uint8_t *)value, len) != 0)
    {
        LOG_E(tag, "factory item %s write failed", key);
        return -1;
    }

    return 0;
}

/**
 * @brief 读取工厂组字符串配置项.
 *
 * @param [in] key - 配置键名称.
 * @param [out] buff - 读取缓冲区.
 * @param [in] buff_size - 缓冲区长度.
 * @return 0表示成功, 负数表示失败.
 */
static int factoryStrRead(const char *key, char *buff, uint16_t buff_size)
{
    if ((key == NULL) || (buff == NULL) || (buff_size == 0))
    {
        return -1;
    }

    memset(buff, 0, buff_size);
    if (snfNvdmReadStr(NVDM_FAC_GROUP, key, (uint8_t *)buff, (int)buff_size) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 将SHA256摘要编码为小写十六进制.
 *
 * @param [in] digest - 32字节摘要.
 * @param [out] hex - 十六进制输出缓冲区.
 * @param [in] hex_size - 缓冲区长度.
 * @return 0表示成功, 负数表示失败.
 */
static int sha256DigestToHex(const uint8_t *digest, char *hex, uint16_t hex_size)
{
    uint16_t i;

    if ((digest == NULL) || (hex == NULL) || (hex_size <= NVDM_FACTORY_SHA256_HEX_LEN))
    {
        return -1;
    }

    for (i = 0; i < SNF_SHA256_DIGEST_SIZE; i++)
    {
        snprintf(&hex[i * 2], 3, "%02x", (unsigned int)digest[i]);
    }

    hex[NVDM_FACTORY_SHA256_HEX_LEN] = '\0';

    return 0;
}

/**
 * @brief 按字段顺序拼接并计算SHA256小写十六进制.
 *
 * @param [in] parts - 字段值数组.
 * @param [in] part_num - 字段数量.
 * @param [out] hex - 64位十六进制输出.
 * @param [in] hex_size - 缓冲区长度.
 * @return 0表示成功, 负数表示失败.
 */
static int licenseSha256Hex(const char * const *parts,
                            uint16_t part_num,
                            char *hex,
                            uint16_t hex_size)
{
    SnfSha256Ctx ctx;
    uint8_t digest[SNF_SHA256_DIGEST_SIZE];
    uint16_t i;
    int ret;

    if ((parts == NULL) || (part_num == 0) || (hex == NULL))
    {
        return -1;
    }

    ret = snfSha256Init(&ctx);
    if (ret != SNF_SHA256_OK)
    {
        return -1;
    }

    for (i = 0; i < part_num; i++)
    {
        if (parts[i] == NULL)
        {
            snfSha256Free(&ctx);
            return -1;
        }

        ret = snfSha256Update(&ctx, (const uint8_t *)parts[i], (uint32_t)strlen(parts[i]));
        if (ret != SNF_SHA256_OK)
        {
            snfSha256Free(&ctx);
            return -1;
        }
    }

    ret = snfSha256Finish(&ctx, digest, sizeof(digest));
    if (ret != SNF_SHA256_OK)
    {
        return -1;
    }

    return sha256DigestToHex(digest, hex, hex_size);
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
 * @return 0表示读取成功, 负数表示失败.
 */
static int matterItemGet(const char *key,
                         char *buff,
                         uint16_t buff_size,
                         uint16_t max_len)
{
    int ret;

    if ((key == NULL) || (buff == NULL) || (buff_size <= max_len))
    {
        return -1;
    }

    memset(buff, 0, buff_size);
    ret = snfNvdmReadStr(NVDM_MATTER_GROUP, key, (uint8_t *)buff, (int)buff_size);
    if (ret != 0)
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
 * @return 0表示写入成功, 负数表示失败.
 */
static int matterItemSet(const char *key, const char *value)
{
    int len;
    int ret;

    if ((key == NULL) || (value == NULL))
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
 * @return 0表示读取成功, 负数表示失败.
 */
static int matterItemGetDec(const char *key, uint32_t *value, uint16_t max_len)
{
    char buff[NVDM_MATTER_DEC_U32_STR_MAX_LEN + 1];

    if (value == NULL)
    {
        return -1;
    }

    if (matterItemGet(key, buff, sizeof(buff), max_len) != 0)
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

    if (matterItemGet(key, buff, sizeof(buff), NVDM_MATTER_HEX_ID_MAX_LEN) != 0)
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

    if (hexStringIsValid(active_code,
                         NVDM_FACTORY_ACTIVE_CODE_HEX_LEN,
                         NVDM_FACTORY_ACTIVE_CODE_HEX_LEN) == 0)
    {
        return -1;
    }

    return 0;
}

int snfActiveCodeSet(const char *active_code)
{
    int len;
    int ret;

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

int snfBaseMacGet(char *base_mac, uint16_t base_mac_size)
{
    uint8_t mac[NVDM_FACTORY_BASE_MAC_LEN];
    int ret;

    if ((base_mac == NULL) || (base_mac_size <= NVDM_FACTORY_BASE_MAC_STR_LEN))
    {
        return -1;
    }

    memset(base_mac, 0, base_mac_size);
    ret = snfNvdmReadStr(NVDM_FAC_GROUP,
                         NVDM_FACTORY_ITEM_BASE_MAC,
                         (uint8_t *)base_mac,
                         (int)base_mac_size);
    if (ret != 0)
    {
        return -1;
    }

    if (baseMacParse(base_mac, mac) != 0)
    {
        return -1;
    }

    return 0;
}

extern bk_err_t bk_set_base_mac_ram(const uint8_t *mac);
int snfBaseMacSet(const char *base_mac)
{
    uint8_t mac[NVDM_FACTORY_BASE_MAC_LEN];
    int len;
    int ret;

    if (baseMacParse(base_mac, mac) != 0)
    {
        return -1;
    }

    len = (int)strlen(base_mac) + 1;
    ret = snfNvdmWriteStr(NVDM_FAC_GROUP,
                          NVDM_FACTORY_ITEM_BASE_MAC,
                          (const uint8_t *)base_mac,
                          len);
    if (ret != 0)
    {
        LOG_E(tag, "base mac write failed");
        return -1;
    }

    if (bk_set_base_mac_ram(mac) != BK_OK)
    {
        LOG_E(tag, "base mac ram set failed");
        return -1;
    }

    return 0;
}

int snfBaseMacApply(void)
{
    char str[NVDM_FACTORY_BASE_MAC_STR_LEN + 1];
    uint8_t mac[NVDM_FACTORY_BASE_MAC_LEN];
    int ret;

    memset(str, 0, sizeof(str));
    ret = snfNvdmReadStr(NVDM_FAC_GROUP,
                         NVDM_FACTORY_ITEM_BASE_MAC,
                         (uint8_t *)str,
                         (int)sizeof(str));
    if ((ret != 0) || (str[0] == '\0'))
    {
        return 0;
    }

    if (baseMacParse(str, mac) != 0)
    {
        LOG_E(tag, "base mac invalid, keep sdk mac");
        return 0;
    }

    if (bk_set_base_mac_ram(mac) != BK_OK)
    {
        LOG_E(tag, "base mac ram set failed");
        return -1;
    }

    return 0;
}

int snfLicenseIsBurned(void)
{
    char device_id[NVDM_FACTORY_DEVICE_ID_LEN + 1];

    if (factoryStrRead(NVDM_FACTORY_ITEM_DEVICE_ID, device_id, sizeof(device_id)) != 0)
    {
        return -1;
    }

    if (device_id[0] == '\0')
    {
        return -1;
    }

    return 0;
}

int snfLicenseClear(void)
{
    if (factoryStrWrite(NVDM_FACTORY_ITEM_DEVICE_ID, "") != 0)
    {
        return -1;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_FACTORY_APIKEY, "") != 0)
    {
        return -1;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_BASE_MAC, "") != 0)
    {
        return -1;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_DEVICE_MODEL, "") != 0)
    {
        return -1;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_DEVICE_UIID, "") != 0)
    {
        return -1;
    }

    return 0;
}

int snfLicenseWrite(const char *json, char *reply_sha256, uint16_t reply_sha256_size)
{
    cJSON *root;
    cJSON *frame;
    cJSON *item;
    char *frame_text;
    const char *device_id;
    const char *apikey;
    const char *base_mac;
    const char *device_model;
    const char *c1_parts[5];
    const char *c5_parts[5];
    const char *read_parts[5];
    char uiid_str[NVDM_FACTORY_UIID_STR_MAX_LEN + 1];
    char c1_hex[NVDM_FACTORY_SHA256_HEX_LEN + 1];
    char read_hex[NVDM_FACTORY_SHA256_HEX_LEN + 1];
    char read_device_id[NVDM_FACTORY_DEVICE_ID_LEN + 1];
    char read_apikey[NVDM_FACTORY_APIKEY_LEN + 1];
    char read_mac[NVDM_FACTORY_BASE_MAC_STR_LEN + 1];
    char read_model[NVDM_MATTER_NAME_MAX_LEN + 1];
    char read_uiid[NVDM_FACTORY_UIID_STR_MAX_LEN + 1];
    uint8_t mac[NVDM_FACTORY_BASE_MAC_LEN];
    uint8_t expect_digest[SNF_SHA256_DIGEST_SIZE];
    uint8_t input_digest[SNF_SHA256_DIGEST_SIZE];
    int ret;

    if ((json == NULL) || (json[0] == '\0')
        || (reply_sha256 == NULL) || (reply_sha256_size <= NVDM_FACTORY_SHA256_HEX_LEN))
    {
        return SNF_LICENSE_ERR_RULES;
    }

    root = cJSON_Parse(json);
    if (root == NULL)
    {
        return SNF_LICENSE_ERR_RULES;
    }

    frame = cJSON_GetObjectItem(root, "license_frame");
    item = cJSON_GetObjectItem(root, "license_frame_len");
    if ((frame == NULL) || (frame->type != cJSON_Object) || (item == NULL)
        || (item->type != cJSON_Number))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    frame_text = cJSON_PrintUnformatted(frame);
    if (frame_text == NULL)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    if (item->valueint != (int)strlen(frame_text))
    {
        free(frame_text);
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_FRAME_LEN;
    }

    free(frame_text);

    item = cJSON_GetObjectItem(frame, "deviceid");
    if ((item == NULL) || (item->type != cJSON_String) || (item->valuestring == NULL)
        || (strlen(item->valuestring) != NVDM_FACTORY_DEVICE_ID_LEN))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    device_id = item->valuestring;

    item = cJSON_GetObjectItem(frame, "factory_apikey");
    if ((item == NULL) || (item->type != cJSON_String)
        || (factoryApikeyIsValid(item->valuestring) == 0))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    apikey = item->valuestring;

    item = cJSON_GetObjectItem(frame, "base_mac");
    if ((item == NULL) || (item->type != cJSON_String)
        || (baseMacParse(item->valuestring, mac) != 0))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    base_mac = item->valuestring;

    item = cJSON_GetObjectItem(frame, "device_model");
    if ((item == NULL) || (item->type != cJSON_String) || (item->valuestring == NULL)
        || (item->valuestring[0] == '\0'))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    device_model = item->valuestring;

    item = cJSON_GetObjectItem(frame, "uiid");
    if ((item == NULL) || (item->type != cJSON_Number) || (item->valueint < 0))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    ret = snprintf(uiid_str, sizeof(uiid_str), "%d", item->valueint);
    if ((ret <= 0) || (ret >= (int)sizeof(uiid_str)))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    item = cJSON_GetObjectItem(root, "sha256_check");
    if ((item == NULL) || (item->type != cJSON_String)
        || (hexBytesDecode(item->valuestring, input_digest, SNF_SHA256_DIGEST_SIZE) != 0))
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_RULES;
    }

    c1_parts[0] = apikey;
    c1_parts[1] = base_mac;
    c1_parts[2] = device_model;
    c1_parts[3] = uiid_str;
    c1_parts[4] = device_id;
    if (licenseSha256Hex(c1_parts, 5, c1_hex, sizeof(c1_hex)) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (hexBytesDecode(c1_hex, expect_digest, SNF_SHA256_DIGEST_SIZE) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (memcmp(input_digest, expect_digest, SNF_SHA256_DIGEST_SIZE) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_SHA256;
    }

    if (strcmp(device_model, SONOFF_DEVICE_MODEL) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_MODEL;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_DEVICE_ID, device_id) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_FACTORY_APIKEY, apikey) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_BASE_MAC, base_mac) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_DEVICE_MODEL, device_model) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrWrite(NVDM_FACTORY_ITEM_DEVICE_UIID, uiid_str) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrRead(NVDM_FACTORY_ITEM_DEVICE_ID, read_device_id, sizeof(read_device_id)) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrRead(NVDM_FACTORY_ITEM_FACTORY_APIKEY, read_apikey, sizeof(read_apikey)) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrRead(NVDM_FACTORY_ITEM_BASE_MAC, read_mac, sizeof(read_mac)) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrRead(NVDM_FACTORY_ITEM_DEVICE_MODEL, read_model, sizeof(read_model)) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (factoryStrRead(NVDM_FACTORY_ITEM_DEVICE_UIID, read_uiid, sizeof(read_uiid)) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    read_parts[0] = read_apikey;
    read_parts[1] = read_mac;
    read_parts[2] = read_model;
    read_parts[3] = read_uiid;
    read_parts[4] = read_device_id;
    if (licenseSha256Hex(read_parts, 5, read_hex, sizeof(read_hex)) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    if (strcmp(c1_hex, read_hex) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_SHA256;
    }

    c5_parts[0] = device_id;
    c5_parts[1] = apikey;
    c5_parts[2] = base_mac;
    c5_parts[3] = device_model;
    c5_parts[4] = uiid_str;
    if (licenseSha256Hex(c5_parts, 5, reply_sha256, reply_sha256_size) != 0)
    {
        cJSON_Delete(root);
        return SNF_LICENSE_ERR_STORAGE;
    }

    cJSON_Delete(root);

    return SNF_LICENSE_OK;
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
                         NVDM_MATTER_DISCRIMINATOR_STR_MAX_LEN) != 0)
    {
        return -1;
    }

    if (value > NVDM_MATTER_DISCRIMINATOR_MAX)
    {
        return -1;
    }

    *discriminator = (uint16_t)value;

    return 0;
}

int snfMatterDiscriminatorSet(const char *discriminator)
{
    if (discriminatorIsValid(discriminator) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_DISCRIMINATOR, discriminator);
}

int snfMatterIterationCountGet(uint32_t *iteration_count)
{
    return matterItemGetDec(NVDM_MATTER_ITEM_ITERATION_COUNT,
                            iteration_count,
                            NVDM_MATTER_DEC_U32_STR_MAX_LEN);
}

int snfMatterIterationCountSet(const char *iteration_count)
{
    if (parseDecU32(iteration_count, NULL) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_ITERATION_COUNT, iteration_count);
}

int snfMatterSaltGet(char *salt, uint16_t salt_size)
{
    if (matterItemGet(NVDM_MATTER_ITEM_SALT,
                      salt,
                      salt_size,
                      NVDM_MATTER_SALT_STR_MAX_LEN) != 0)
    {
        return -1;
    }

    if (base64IsValid(salt, NVDM_MATTER_SALT_STR_MAX_LEN) == 0)
    {
        return -1;
    }

    return 0;
}

int snfMatterSaltSet(const char *salt)
{
    if (base64IsValid(salt, NVDM_MATTER_SALT_STR_MAX_LEN) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_SALT, salt);
}

int snfMatterVerifierGet(char *verifier, uint16_t verifier_size)
{
    if (matterItemGet(NVDM_MATTER_ITEM_VERIFIER,
                      verifier,
                      verifier_size,
                      NVDM_MATTER_VERIFIER_STR_MAX_LEN) != 0)
    {
        return -1;
    }

    if (base64IsValid(verifier, NVDM_MATTER_VERIFIER_STR_MAX_LEN) == 0)
    {
        return -1;
    }

    return 0;
}

int snfMatterVerifierSet(const char *verifier)
{
    if (base64IsValid(verifier, NVDM_MATTER_VERIFIER_STR_MAX_LEN) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_VERIFIER, verifier);
}

int snfMatterVendorIdGet(uint16_t *vendor_id)
{
    return matterItemGetHexU16(NVDM_MATTER_ITEM_VENDOR_ID, vendor_id);
}

int snfMatterVendorIdSet(const char *vendor_id)
{
    if (hexStringIsValid(vendor_id, 1, NVDM_MATTER_HEX_ID_MAX_LEN) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_VENDOR_ID, vendor_id);
}

int snfMatterVendorNameGet(char *vendor_name, uint16_t vendor_name_size)
{
    if (matterItemGet(NVDM_MATTER_ITEM_VENDOR_NAME,
                      vendor_name,
                      vendor_name_size,
                      NVDM_MATTER_NAME_MAX_LEN) != 0)
    {
        return -1;
    }

    if (nameIsValid(vendor_name) == 0)
    {
        return -1;
    }

    return 0;
}

int snfMatterVendorNameSet(const char *vendor_name)
{
    if (nameIsValid(vendor_name) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_VENDOR_NAME, vendor_name);
}

int snfMatterProductIdGet(uint16_t *product_id)
{
    return matterItemGetHexU16(NVDM_MATTER_ITEM_PRODUCT_ID, product_id);
}

int snfMatterProductIdSet(const char *product_id)
{
    if (hexStringIsValid(product_id, 1, NVDM_MATTER_HEX_ID_MAX_LEN) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_PRODUCT_ID, product_id);
}

int snfMatterProductNameGet(char *product_name, uint16_t product_name_size)
{
    if (matterItemGet(NVDM_MATTER_ITEM_PRODUCT_NAME,
                      product_name,
                      product_name_size,
                      NVDM_MATTER_NAME_MAX_LEN) != 0)
    {
        return -1;
    }

    if (nameIsValid(product_name) == 0)
    {
        return -1;
    }

    return 0;
}

int snfMatterProductNameSet(const char *product_name)
{
    if (nameIsValid(product_name) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_PRODUCT_NAME, product_name);
}

int snfMatterRdIdUidGet(char *rd_id_uid, uint16_t rd_id_uid_size)
{
    if (matterItemGet(NVDM_MATTER_ITEM_RD_ID_UID,
                      rd_id_uid,
                      rd_id_uid_size,
                      NVDM_MATTER_RD_ID_UID_HEX_LEN) != 0)
    {
        return -1;
    }

    if (hexStringIsValid(rd_id_uid,
                         NVDM_MATTER_RD_ID_UID_HEX_LEN,
                         NVDM_MATTER_RD_ID_UID_HEX_LEN) == 0)
    {
        return -1;
    }

    return 0;
}

int snfMatterRdIdUidSet(const char *rd_id_uid)
{
    if (hexStringIsValid(rd_id_uid,
                         NVDM_MATTER_RD_ID_UID_HEX_LEN,
                         NVDM_MATTER_RD_ID_UID_HEX_LEN) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_RD_ID_UID, rd_id_uid);
}

int snfMatterPasscodeGet(uint32_t *passcode)
{
    return matterItemGetDec(NVDM_MATTER_ITEM_PASSCODE,
                            passcode,
                            NVDM_MATTER_DEC_U32_STR_MAX_LEN);
}

int snfMatterPasscodeSet(const char *passcode)
{
    if (parseDecU32(passcode, NULL) == 0)
    {
        return -1;
    }

    return matterItemSet(NVDM_MATTER_ITEM_PASSCODE, passcode);
}
