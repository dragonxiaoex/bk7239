/**
 * @file    sonoff_aes_gcm.h
 * @brief   AES-256-GCM加解密适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-07
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_AES_GCM_H__
#define __SONOFF_AES_GCM_H__

#include <stdint.h>

/** @brief AES-GCM接口返回值. */
#define SNF_AES_GCM_OK                   (0)     /* 成功 */
#define SNF_AES_GCM_ERR_INVALID_PARAM    (-1)    /* 参数错误 */
#define SNF_AES_GCM_ERR_BUFFER_TOO_SMALL (-2)    /* 输出缓冲区不足 */
#define SNF_AES_GCM_ERR_AUTH_FAILED      (-3)    /* 认证标签校验失败 */
#define SNF_AES_GCM_ERR_INTERNAL         (-4)    /* 底层计算失败 */

/** @brief AES-GCM缓冲区长度, 单位为字节. */
#define SNF_AES_GCM_KEY_SIZE            (32)    /* AES-256密钥 */
#define SNF_AES_GCM_IV_SIZE             (12)    /* 推荐IV长度 */
#define SNF_AES_GCM_TAG_SIZE            (16)    /* 认证标签 */
#define SNF_AES_GCM_CTX_SIZE            (512)   /* 流式上下文存储 */

/** @brief GCM流式解密上下文，首次使用前须零初始化，内部成员不可直接访问. */
typedef struct
{
    uint64_t storage[SNF_AES_GCM_CTX_SIZE / sizeof(uint64_t)];
} SnfAesGcmCtx;

/**
 * @brief 使用AES-256-GCM加密数据.
 *
 * 密文长度等于明文长度, 认证标签单独输出到tag. auth_data_len为0时允许auth_data为NULL.
 * plaintext_len为0时允许plaintext为NULL. 允许cipher与plaintext为同一缓冲区.
 *
 * @param [in] key - 32字节密钥.
 * @param [in] iv - 12字节IV.
 * @param [in] auth_data - 附加认证数据.
 * @param [in] auth_data_len - 附加认证数据长度.
 * @param [in] plaintext - 明文.
 * @param [in] plaintext_len - 明文长度.
 * @param [out] cipher - 密文输出缓冲区.
 * @param [in] cipher_size - 密文缓冲区长度, 不得小于plaintext_len.
 * @param [out] cipher_len - 实际密文长度.
 * @param [out] tag - 16字节认证标签输出缓冲区.
 * @return 0表示成功, 负数表示失败.
 */
int snfAesGcmEncrypt(const uint8_t *key, const uint8_t *iv,
                     const uint8_t *auth_data, uint32_t auth_data_len,
                     const uint8_t *plaintext, uint32_t plaintext_len,
                     uint8_t *cipher, uint32_t cipher_size, uint32_t *cipher_len,
                     uint8_t *tag);

/**
 * @brief 使用AES-256-GCM解密并校验认证标签.
 *
 * 明文长度等于密文长度. auth_data_len为0时允许auth_data为NULL. cipher_len为0时允许cipher为NULL.
 * 输出缓冲区不得与输入缓冲区重叠.
 *
 * @param [in] key - 32字节密钥.
 * @param [in] iv - 12字节IV.
 * @param [in] auth_data - 附加认证数据.
 * @param [in] auth_data_len - 附加认证数据长度.
 * @param [in] cipher - 密文.
 * @param [in] cipher_len - 密文长度.
 * @param [in] tag - 16字节认证标签.
 * @param [out] plaintext - 明文输出缓冲区.
 * @param [in] plaintext_size - 明文缓冲区长度, 不得小于cipher_len.
 * @param [out] plaintext_len - 实际明文长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfAesGcmDecrypt(const uint8_t *key, const uint8_t *iv,
                     const uint8_t *auth_data, uint32_t auth_data_len,
                     const uint8_t *cipher, uint32_t cipher_len, const uint8_t *tag,
                     uint8_t *plaintext, uint32_t plaintext_size, uint32_t *plaintext_len);

/**
 * @brief 开始AES-256-GCM流式解密
 *
 * 结束或取消时须调用snfAesGcmDecryptFinish或snfAesGcmFree，再次开始前须释放旧上下文。
 *
 * @param [in,out] ctx - 零初始化或已释放的上下文.
 * @param [in] key - 32字节密钥.
 * @param [in] iv - 12字节IV.
 * @param [in] auth_data - 附加认证数据，auth_data_len为0时可为NULL.
 * @param [in] auth_data_len - 附加认证数据长度.
 * @return 0表示成功，负数表示失败，失败时资源已释放.
 */
int32_t snfAesGcmDecryptStart(SnfAesGcmCtx *ctx, const uint8_t *key, const uint8_t *iv,
                             const uint8_t *auth_data, uint32_t auth_data_len);

/**
 * @brief 分块解密，使用当前SDK即时输出模式
 *
 * 明文长度等于输入长度，输入输出不得重叠。输出尚未通过认证，Finish成功前不得提交升级。
 *
 * @param [in,out] ctx - 已开始的上下文.
 * @param [in] cipher - 密文，长度非零.
 * @param [in] size - 密文长度.
 * @param [out] plaintext - 至少size字节的明文缓冲区.
 * @return 0表示成功，负数表示失败，失败后须释放上下文.
 */
int32_t snfAesGcmDecryptUpdate(SnfAesGcmCtx *ctx, const uint8_t *cipher, uint32_t size, uint8_t *plaintext);

/**
 * @brief 完成流式解密并以恒定时间比较认证标签
 *
 * @param [in,out] ctx - 已开始的上下文，调用后资源被释放.
 * @param [in] tag - 16字节预期认证标签.
 * @return 0表示认证通过，负数表示失败，此时此前输出的明文均不可使用.
 */
int32_t snfAesGcmDecryptFinish(SnfAesGcmCtx *ctx, const uint8_t *tag);

/**
 * @brief 释放流式解密上下文，可用于中止或重复清理
 *
 * @param [in,out] ctx - 已零初始化、已开始或已释放的上下文，允许NULL.
 */
void snfAesGcmFree(SnfAesGcmCtx *ctx);

#endif /* #ifndef __SONOFF_AES_GCM_H__ */
