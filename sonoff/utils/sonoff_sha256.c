/**
 * @file    sonoff_sha256.c
 * @brief   SHA256流式校验适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/sha256.h"

#include "sonoff_sha256.h"

_Static_assert(sizeof(mbedtls_sha256_context) <= SNF_SHA256_CTX_SIZE,
               "SnfSha256Ctx storage is smaller than mbedtls_sha256_context");

/**
 * @brief 取底层mbedtls SHA256上下文.
 *
 * @param [in] ctx - SHA256上下文.
 * @return 底层mbedtls上下文指针.
 */
static mbedtls_sha256_context *sha256GetImpl(SnfSha256Ctx *ctx)
{
    return (mbedtls_sha256_context *)ctx->storage;
}

int snfSha256Init(SnfSha256Ctx *ctx)
{
    mbedtls_sha256_context *sha_ctx;

    if (ctx == NULL)
    {
        return SNF_SHA256_ERR_INVALID_PARAM;
    }

    sha_ctx = sha256GetImpl(ctx);
    mbedtls_sha256_init(sha_ctx);
    if (mbedtls_sha256_starts(sha_ctx, 0) != 0)
    {
        mbedtls_sha256_free(sha_ctx);

        return SNF_SHA256_ERR_INTERNAL;
    }

    return SNF_SHA256_OK;
}

int snfSha256Update(SnfSha256Ctx *ctx, const uint8_t *data, uint32_t length)
{
    mbedtls_sha256_context *sha_ctx;

    if (ctx == NULL)
    {
        return SNF_SHA256_ERR_INVALID_PARAM;
    }

    if (length == 0)
    {
        return SNF_SHA256_OK;
    }

    if (data == NULL)
    {
        return SNF_SHA256_ERR_INVALID_PARAM;
    }

    sha_ctx = sha256GetImpl(ctx);
    if (mbedtls_sha256_update(sha_ctx, data, (size_t)length) != 0)
    {
        return SNF_SHA256_ERR_INTERNAL;
    }

    return SNF_SHA256_OK;
}

int snfSha256Finish(SnfSha256Ctx *ctx, uint8_t *digest, uint32_t digest_size)
{
    mbedtls_sha256_context *sha_ctx;
    int ret;

    if ((ctx == NULL) || (digest == NULL) || (digest_size < SNF_SHA256_DIGEST_SIZE))
    {
        return SNF_SHA256_ERR_INVALID_PARAM;
    }

    sha_ctx = sha256GetImpl(ctx);
    ret = mbedtls_sha256_finish(sha_ctx, digest);
    mbedtls_sha256_free(sha_ctx);

    if (ret != 0)
    {
        return SNF_SHA256_ERR_INTERNAL;
    }

    return SNF_SHA256_OK;
}

void snfSha256Free(SnfSha256Ctx *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    mbedtls_sha256_free(sha256GetImpl(ctx));
}
