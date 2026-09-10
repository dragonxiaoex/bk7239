/**
 * @file    sonoff_sha256.h
 * @brief   SHA256流式校验适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_SHA256_H__
#define __SONOFF_SHA256_H__

#include <stdint.h>

/** @brief SHA256接口返回值. */
#define SNF_SHA256_OK                   (0)     /* 成功 */
#define SNF_SHA256_ERR_INVALID_PARAM    (-1)    /* 参数错误 */
#define SNF_SHA256_ERR_INTERNAL         (-2)    /* 底层计算失败 */

/** @brief SHA256摘要长度, 单位为字节. */
#define SNF_SHA256_DIGEST_SIZE          (32)

/** @brief SHA256上下文不透明存储长度, 单位为字节. */
#define SNF_SHA256_CTX_SIZE             (256)

/**
 * @brief SHA256流式计算上下文.
 *
 * 内部存储为不透明缓冲区, 调用者不得直接访问成员.
 */
typedef struct
{
    uint64_t storage[SNF_SHA256_CTX_SIZE / sizeof(uint64_t)];
} SnfSha256Ctx;

/**
 * @brief 开始一次SHA256流式计算.
 *
 * 可在栈上分配上下文. 成功后必须调用snfSha256Finish或snfSha256Free结束本次计算.
 *
 * @param [in,out] ctx - SHA256上下文.
 * @return 0表示成功, 负数表示失败.
 */
int snfSha256Init(SnfSha256Ctx *ctx);

/**
 * @brief 追加待计算数据.
 *
 * 可多次调用. length为0时允许data为NULL.
 *
 * @param [in,out] ctx - 已初始化的SHA256上下文.
 * @param [in] data - 待追加数据.
 * @param [in] length - 数据长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfSha256Update(SnfSha256Ctx *ctx, const uint8_t *data, uint32_t length);

/**
 * @brief 结束计算并输出SHA256摘要.
 *
 * 成功或底层计算失败后上下文均被释放, 再次使用前需重新调用snfSha256Init.
 *
 * @param [in,out] ctx - 已初始化的SHA256上下文.
 * @param [out] digest - 摘要输出缓冲区.
 * @param [in] digest_size - 摘要缓冲区长度, 不得小于SNF_SHA256_DIGEST_SIZE.
 * @return 0表示成功, 负数表示失败.
 */
int snfSha256Finish(SnfSha256Ctx *ctx, uint8_t *digest, uint32_t digest_size);

/**
 * @brief 释放SHA256上下文.
 *
 * 用于中止未完成的流式计算. 传入NULL时直接返回.
 *
 * @param [in,out] ctx - SHA256上下文.
 */
void snfSha256Free(SnfSha256Ctx *ctx);

#endif /* #ifndef __SONOFF_SHA256_H__ */
