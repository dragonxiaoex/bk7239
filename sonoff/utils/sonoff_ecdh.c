/**
 * @file    sonoff_ecdh.c
 * @brief   ECDH密钥协商适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-07
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>

#include <driver/trng.h>
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/platform_util.h"

#include "sonoff_ecdh.h"

#define ECDH_MAGIC                      (0x45434448)    /* 上下文有效标记 */

/**
 * @brief ECDH底层实现上下文.
 */
typedef struct
{
    uint32_t magic;
    uint8_t ready;
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point q;
} EcdhImpl;

_Static_assert(sizeof(EcdhImpl) <= SNF_ECDH_CTX_SIZE,
               "SnfEcdhCtx storage is smaller than EcdhImpl");

/**
 * @brief 为mbedtls提供随机数.
 *
 * @param [in] rng_state - 随机数上下文, 本实现未使用.
 * @param [out] output - 随机数输出缓冲区.
 * @param [in] len - 需要填充的长度.
 * @return 0表示成功.
 */
static int ecdhFillRandom(void *rng_state, unsigned char *output, size_t len)
{
    uint32_t rand_num = 0;
    size_t i;

    for (i = 0; i < len; i++)
    {
        if ((i % 4) == 0)
        {
            rand_num = (uint32_t)bk_rand();
        }

        output[i] = (unsigned char)((rand_num >> (8 * (i % 4))) & 0xff);
    }

    return 0;
}

/**
 * @brief 取底层ECDH实现上下文.
 *
 * @param [in] ctx - ECDH上下文.
 * @return 底层实现指针.
 */
static EcdhImpl *ecdhGetImpl(SnfEcdhCtx *ctx)
{
    return (EcdhImpl *)ctx->storage;
}

/**
 * @brief 释放底层密钥材料并清零上下文.
 *
 * @param [in,out] impl - 底层实现上下文.
 */
static void ecdhClear(EcdhImpl *impl)
{
    mbedtls_ecp_group_free(&impl->grp);
    mbedtls_mpi_free(&impl->d);
    mbedtls_ecp_point_free(&impl->q);
    mbedtls_platform_zeroize(impl, sizeof(*impl));
}

/**
 * @brief 准备secp256r1协商上下文.
 *
 * @param [in,out] impl - 底层实现上下文.
 * @return 0表示成功, 负数表示失败.
 */
static int ecdhPrepare(EcdhImpl *impl)
{
    if (impl->magic == ECDH_MAGIC)
    {
        ecdhClear(impl);
    }
    else
    {
        mbedtls_platform_zeroize(impl, sizeof(*impl));
    }

    mbedtls_ecp_group_init(&impl->grp);
    mbedtls_mpi_init(&impl->d);
    mbedtls_ecp_point_init(&impl->q);
    if (mbedtls_ecp_group_load(&impl->grp, MBEDTLS_ECP_DP_SECP256R1) != 0)
    {
        ecdhClear(impl);

        return SNF_ECDH_ERR_INTERNAL;
    }

    impl->magic = ECDH_MAGIC;

    return SNF_ECDH_OK;
}

int snfEcdhGenerate(SnfEcdhCtx *ctx, uint8_t *pub, uint32_t pub_size, uint32_t *pub_len)
{
    EcdhImpl *impl;
    size_t olen = 0;
    int ret;

    if ((ctx == NULL) || (pub == NULL) || (pub_len == NULL))
    {
        return SNF_ECDH_ERR_INVALID_PARAM;
    }

    if (pub_size < SNF_ECDH_PUBLIC_SIZE)
    {
        return SNF_ECDH_ERR_BUFFER_TOO_SMALL;
    }

    impl = ecdhGetImpl(ctx);
    ret = ecdhPrepare(impl);
    if (ret != SNF_ECDH_OK)
    {
        return ret;
    }

    if (mbedtls_ecdh_gen_public(&impl->grp, &impl->d, &impl->q, ecdhFillRandom, NULL) != 0)
    {
        ecdhClear(impl);

        return SNF_ECDH_ERR_INTERNAL;
    }

    if (mbedtls_ecp_point_write_binary(&impl->grp, &impl->q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                       &olen, pub, (size_t)pub_size) != 0)
    {
        ecdhClear(impl);

        return SNF_ECDH_ERR_INTERNAL;
    }

    impl->ready = 1;
    *pub_len = (uint32_t)olen;

    return SNF_ECDH_OK;
}

int snfEcdhCompute(SnfEcdhCtx *ctx, const uint8_t *peer_pub, uint32_t peer_pub_len,
                   uint8_t *secret, uint32_t secret_size, uint32_t *secret_len)
{
    EcdhImpl *impl;
    mbedtls_ecp_point peer;
    mbedtls_mpi z;

    if ((ctx == NULL) || (peer_pub == NULL) || (peer_pub_len == 0)
        || (secret == NULL) || (secret_len == NULL))
    {
        return SNF_ECDH_ERR_INVALID_PARAM;
    }

    if (secret_size < SNF_ECDH_SECRET_SIZE)
    {
        return SNF_ECDH_ERR_BUFFER_TOO_SMALL;
    }

    impl = ecdhGetImpl(ctx);
    if ((impl->magic != ECDH_MAGIC) || (impl->ready == 0))
    {
        return SNF_ECDH_ERR_STATE;
    }

    mbedtls_ecp_point_init(&peer);
    mbedtls_mpi_init(&z);
    if (mbedtls_ecp_point_read_binary(&impl->grp, &peer, peer_pub, (size_t)peer_pub_len) != 0)
    {
        mbedtls_ecp_point_free(&peer);
        mbedtls_mpi_free(&z);

        return SNF_ECDH_ERR_INVALID_DATA;
    }

    if (mbedtls_ecp_check_pubkey(&impl->grp, &peer) != 0)
    {
        mbedtls_ecp_point_free(&peer);
        mbedtls_mpi_free(&z);

        return SNF_ECDH_ERR_INVALID_DATA;
    }

    if (mbedtls_ecdh_compute_shared(&impl->grp, &z, &peer, &impl->d, ecdhFillRandom, NULL) != 0)
    {
        mbedtls_ecp_point_free(&peer);
        mbedtls_mpi_free(&z);

        return SNF_ECDH_ERR_INTERNAL;
    }

    if (mbedtls_mpi_write_binary(&z, secret, SNF_ECDH_SECRET_SIZE) != 0)
    {
        mbedtls_ecp_point_free(&peer);
        mbedtls_mpi_free(&z);

        return SNF_ECDH_ERR_INTERNAL;
    }

    *secret_len = SNF_ECDH_SECRET_SIZE;
    mbedtls_ecp_point_free(&peer);
    mbedtls_mpi_free(&z);

    return SNF_ECDH_OK;
}

void snfEcdhFree(SnfEcdhCtx *ctx)
{
    EcdhImpl *impl;

    if (ctx == NULL)
    {
        return;
    }

    impl = ecdhGetImpl(ctx);
    if (impl->magic != ECDH_MAGIC)
    {
        mbedtls_platform_zeroize(impl, sizeof(*impl));

        return;
    }

    ecdhClear(impl);
}
