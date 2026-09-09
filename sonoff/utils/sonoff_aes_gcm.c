/**
 * @file    sonoff_aes_gcm.c
 * @brief   AES-256-GCM加解密适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-07
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/gcm.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/platform_util.h"

#include "sonoff_aes_gcm.h"

_Static_assert(sizeof(mbedtls_gcm_context) <= SNF_AES_GCM_CTX_SIZE,
               "SnfAesGcmCtx storage is smaller than mbedtls_gcm_context");

/**
 * @brief 将mbedtls返回值转换为项目错误码.
 *
 * @param [in] mbedtls_ret - mbedtls接口返回值.
 * @return 归一化后的项目错误码.
 */
static int aesGcmMapError(int mbedtls_ret)
{
    if (mbedtls_ret == 0)
    {
        return SNF_AES_GCM_OK;
    }

    if (mbedtls_ret == MBEDTLS_ERR_GCM_AUTH_FAILED)
    {
        return SNF_AES_GCM_ERR_AUTH_FAILED;
    }

    if (mbedtls_ret == MBEDTLS_ERR_GCM_BUFFER_TOO_SMALL)
    {
        return SNF_AES_GCM_ERR_BUFFER_TOO_SMALL;
    }

    if (mbedtls_ret == MBEDTLS_ERR_GCM_BAD_INPUT)
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    return SNF_AES_GCM_ERR_INTERNAL;
}

/**
 * @brief 校验加解密公共参数.
 *
 * @param [in] key - 密钥.
 * @param [in] iv - IV.
 * @param [in] aad - 附加认证数据.
 * @param [in] aad_len - 附加认证数据长度.
 * @param [in] input - 输入数据.
 * @param [in] input_len - 输入数据长度.
 * @param [in] output - 输出缓冲区.
 * @param [in] output_size - 输出缓冲区长度.
 * @return 0表示成功, 负数表示失败.
 */
static int aesGcmCheckParam(const uint8_t *key, const uint8_t *iv,
                            const uint8_t *aad, uint32_t aad_len,
                            const uint8_t *input, uint32_t input_len,
                            const uint8_t *output, uint32_t output_size)
{
    if ((key == NULL) || (iv == NULL))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    if ((aad_len != 0) && (aad == NULL))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    if ((input_len != 0) && ((input == NULL) || (output == NULL)))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    if (output_size < input_len)
    {
        return SNF_AES_GCM_ERR_BUFFER_TOO_SMALL;
    }

    return SNF_AES_GCM_OK;
}

/**
 * @brief 初始化GCM上下文并设置AES-256密钥.
 *
 * @param [out] ctx - GCM上下文.
 * @param [in] key - 32字节密钥.
 * @return 0表示成功, 负数表示失败.
 */
static int aesGcmPrepare(mbedtls_gcm_context *ctx, const uint8_t *key)
{
    mbedtls_gcm_init(ctx);
    if (mbedtls_gcm_setkey(ctx, MBEDTLS_CIPHER_ID_AES, key, SNF_AES_GCM_KEY_SIZE * 8) != 0)
    {
        mbedtls_gcm_free(ctx);

        return SNF_AES_GCM_ERR_INTERNAL;
    }

    return SNF_AES_GCM_OK;
}

int snfAesGcmEncrypt(const uint8_t *key, const uint8_t *iv,
                     const uint8_t *aad, uint32_t aad_len,
                     const uint8_t *plain, uint32_t plain_len,
                     uint8_t *cipher, uint32_t cipher_size, uint32_t *cipher_len,
                     uint8_t *tag)
{
    mbedtls_gcm_context ctx;
    int ret;

    if ((cipher_len == NULL) || (tag == NULL))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    ret = aesGcmCheckParam(key, iv, aad, aad_len, plain, plain_len, cipher, cipher_size);
    if (ret != SNF_AES_GCM_OK)
    {
        return ret;
    }

    ret = aesGcmPrepare(&ctx, key);
    if (ret != SNF_AES_GCM_OK)
    {
        return ret;
    }

    ret = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, (size_t)plain_len,
                                    iv, SNF_AES_GCM_IV_SIZE, aad, (size_t)aad_len,
                                    plain, cipher, SNF_AES_GCM_TAG_SIZE, tag);
    mbedtls_gcm_free(&ctx);
    if (ret != 0)
    {
        return aesGcmMapError(ret);
    }

    *cipher_len = plain_len;

    return SNF_AES_GCM_OK;
}

int snfAesGcmDecrypt(const uint8_t *key, const uint8_t *iv,
                     const uint8_t *aad, uint32_t aad_len,
                     const uint8_t *cipher, uint32_t cipher_len, const uint8_t *tag,
                     uint8_t *plain, uint32_t plain_size, uint32_t *plain_len)
{
    mbedtls_gcm_context ctx;
    int ret;

    if ((plain_len == NULL) || (tag == NULL))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    ret = aesGcmCheckParam(key, iv, aad, aad_len, cipher, cipher_len, plain, plain_size);
    if (ret != SNF_AES_GCM_OK)
    {
        return ret;
    }

    ret = aesGcmPrepare(&ctx, key);
    if (ret != SNF_AES_GCM_OK)
    {
        return ret;
    }

    ret = mbedtls_gcm_auth_decrypt(&ctx, (size_t)cipher_len, iv, SNF_AES_GCM_IV_SIZE,
                                   aad, (size_t)aad_len, tag, SNF_AES_GCM_TAG_SIZE,
                                   cipher, plain);
    mbedtls_gcm_free(&ctx);
    if (ret != 0)
    {
        return aesGcmMapError(ret);
    }

    *plain_len = cipher_len;

    return SNF_AES_GCM_OK;
}

int32_t snfAesGcmDecryptStart(SnfAesGcmCtx *ctx, const uint8_t *key, const uint8_t *iv,
                             const uint8_t *aad, uint32_t aad_len)
{
    mbedtls_gcm_context *gcm;
    int32_t ret;

    if ((ctx == NULL) || (key == NULL) || (iv == NULL) || ((aad_len != 0) && (aad == NULL)))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    gcm = (mbedtls_gcm_context *)ctx->storage;
    ret = aesGcmPrepare(gcm, key);
    if (ret != SNF_AES_GCM_OK)
    {
        return ret;
    }

    ret = mbedtls_gcm_starts(gcm, MBEDTLS_GCM_DECRYPT, iv, SNF_AES_GCM_IV_SIZE);
    if (ret == 0)
    {
        ret = mbedtls_gcm_update_ad(gcm, aad, aad_len);
    }

    if (ret != 0)
    {
        snfAesGcmFree(ctx);
    }

    return aesGcmMapError(ret);
}

int32_t snfAesGcmDecryptUpdate(SnfAesGcmCtx *ctx, const uint8_t *cipher, uint32_t size, uint8_t *plain)
{
    size_t output_size = 0;
    int32_t ret;

    if ((ctx == NULL) || (cipher == NULL) || (plain == NULL) || (size == 0))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    ret = mbedtls_gcm_update((mbedtls_gcm_context *)ctx->storage, cipher, size, plain, size, &output_size);
    if (ret != 0)
    {
        return aesGcmMapError(ret);
    }

    if (output_size != size)
    {
        return SNF_AES_GCM_ERR_INTERNAL;
    }

    return SNF_AES_GCM_OK;
}

int32_t snfAesGcmDecryptFinish(SnfAesGcmCtx *ctx, const uint8_t *tag)
{
    uint8_t actual_tag[SNF_AES_GCM_TAG_SIZE] = {0};
    size_t output_size = 0;
    int32_t ret;

    if ((ctx == NULL) || (tag == NULL))
    {
        return SNF_AES_GCM_ERR_INVALID_PARAM;
    }

    ret = mbedtls_gcm_finish((mbedtls_gcm_context *)ctx->storage, NULL, 0, &output_size,
                             actual_tag, sizeof(actual_tag));
    snfAesGcmFree(ctx);
    if (ret == 0)
    {
        ret = (mbedtls_ct_memcmp(actual_tag, tag, sizeof(actual_tag)) == 0)
            ? SNF_AES_GCM_OK : SNF_AES_GCM_ERR_AUTH_FAILED;
    }
    else
    {
        ret = aesGcmMapError(ret);
    }

    mbedtls_platform_zeroize(actual_tag, sizeof(actual_tag));

    return ret;
}

void snfAesGcmFree(SnfAesGcmCtx *ctx)
{
    if (ctx != NULL)
    {
        mbedtls_gcm_free((mbedtls_gcm_context *)ctx->storage);
    }
}
