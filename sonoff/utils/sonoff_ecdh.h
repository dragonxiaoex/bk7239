/**
 * @file    sonoff_ecdh.h
 * @brief   ECDH密钥协商适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-07
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_ECDH_H__
#define __SONOFF_ECDH_H__

#include <stdint.h>

/** @brief ECDH接口返回值. */
#define SNF_ECDH_OK                     (0)     /* 成功 */
#define SNF_ECDH_ERR_INVALID_PARAM      (-1)    /* 参数错误 */
#define SNF_ECDH_ERR_BUFFER_TOO_SMALL   (-2)    /* 输出缓冲区不足 */
#define SNF_ECDH_ERR_INVALID_DATA       (-3)    /* 对端公钥非法 */
#define SNF_ECDH_ERR_STATE              (-4)    /* 未生成本地密钥 */
#define SNF_ECDH_ERR_INTERNAL           (-5)    /* 底层计算失败 */

/** @brief ECDH缓冲区长度, 单位为字节. */
#define SNF_ECDH_PUBLIC_SIZE            (65)    /* P-256未压缩公钥 */
#define SNF_ECDH_SECRET_SIZE            (32)    /* 共享密钥 */
#define SNF_ECDH_CTX_SIZE               (1024)  /* 不透明上下文 */

/**
 * @brief ECDH协商上下文.
 *
 * 内部存储为不透明缓冲区, 调用者不得直接访问成员.
 */
typedef struct
{
    uint64_t storage[SNF_ECDH_CTX_SIZE / sizeof(uint64_t)];
} SnfEcdhCtx;

/**
 * @brief 生成本地ECDH密钥对并导出公钥.
 *
 * 曲线为secp256r1, 公钥为未压缩格式. 成功后必须调用snfEcdhFree释放.
 * 重复调用会丢弃上一组本地密钥.
 *
 * @param [in,out] ctx - ECDH上下文.
 * @param [out] pub - 公钥输出缓冲区.
 * @param [in] pub_size - 公钥缓冲区长度, 不得小于SNF_ECDH_PUBLIC_SIZE.
 * @param [out] pub_len - 实际公钥长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfEcdhGenerate(SnfEcdhCtx *ctx, uint8_t *pub, uint32_t pub_size, uint32_t *pub_len);

/**
 * @brief 使用对端公钥计算共享密钥.
 *
 * 必须先成功调用snfEcdhGenerate. 计算完成后仍需调用snfEcdhFree.
 *
 * @param [in] ctx - 已生成本地密钥的ECDH上下文.
 * @param [in] peer_pub - 对端公钥.
 * @param [in] peer_pub_len - 对端公钥长度.
 * @param [out] secret - 共享密钥输出缓冲区.
 * @param [in] secret_size - 共享密钥缓冲区长度, 不得小于SNF_ECDH_SECRET_SIZE.
 * @param [out] secret_len - 实际共享密钥长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfEcdhCompute(SnfEcdhCtx *ctx, const uint8_t *peer_pub, uint32_t peer_pub_len,
                   uint8_t *secret, uint32_t secret_size, uint32_t *secret_len);

/**
 * @brief 释放ECDH上下文.
 *
 * 传入NULL时直接返回.
 *
 * @param [in,out] ctx - ECDH上下文.
 */
void snfEcdhFree(SnfEcdhCtx *ctx);

#endif /* #ifndef __SONOFF_ECDH_H__ */
