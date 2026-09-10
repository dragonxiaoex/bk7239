/**
 * @file    sonoff_base64.c
 * @brief   Base64编解码适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/base64.h"

#include "sonoff_base64.h"

/**
 * @brief 将mbedtls返回值转换为项目错误码.
 *
 * @param [in] mbedtls_ret - mbedtls接口返回值.
 * @return 归一化后的项目错误码.
 */
static int base64MapError(int mbedtls_ret)
{
    if (mbedtls_ret == 0)
    {
        return SNF_BASE64_OK;
    }

    if (mbedtls_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    {
        return SNF_BASE64_ERR_BUFFER_TOO_SMALL;
    }

    if (mbedtls_ret == MBEDTLS_ERR_BASE64_INVALID_CHARACTER)
    {
        return SNF_BASE64_ERR_INVALID_DATA;
    }

    return SNF_BASE64_ERR_INTERNAL;
}

int snfBase64Encode(uint8_t *out, uint32_t out_size, uint32_t *out_len,
                    const uint8_t *data, uint32_t data_len)
{
    size_t olen = 0;
    int ret;

    if (out_len == NULL)
    {
        return SNF_BASE64_ERR_INVALID_PARAM;
    }

    if ((data_len != 0) && (data == NULL))
    {
        return SNF_BASE64_ERR_INVALID_PARAM;
    }

    if ((out == NULL) || (out_size == 0))
    {
        ret = mbedtls_base64_encode(NULL, 0, &olen, data, (size_t)data_len);
        if ((ret == 0) || (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL))
        {
            *out_len = (uint32_t)olen;

            return SNF_BASE64_OK;
        }

        return base64MapError(ret);
    }

    ret = mbedtls_base64_encode(out, (size_t)out_size, &olen, data, (size_t)data_len);
    *out_len = (uint32_t)olen;

    return base64MapError(ret);
}

int snfBase64Decode(uint8_t *out, uint32_t out_size, uint32_t *out_len,
                    const uint8_t *data, uint32_t data_len)
{
    size_t olen = 0;
    int ret;

    if (out_len == NULL)
    {
        return SNF_BASE64_ERR_INVALID_PARAM;
    }

    if ((data_len != 0) && (data == NULL))
    {
        return SNF_BASE64_ERR_INVALID_PARAM;
    }

    if ((out == NULL) || (out_size == 0))
    {
        ret = mbedtls_base64_decode(NULL, 0, &olen, data, (size_t)data_len);
        if ((ret == 0) || (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL))
        {
            *out_len = (uint32_t)olen;

            return SNF_BASE64_OK;
        }

        return base64MapError(ret);
    }

    ret = mbedtls_base64_decode(out, (size_t)out_size, &olen, data, (size_t)data_len);
    *out_len = (uint32_t)olen;

    return base64MapError(ret);
}
