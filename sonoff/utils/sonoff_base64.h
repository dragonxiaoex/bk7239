/**
 * @file    sonoff_base64.h
 * @brief   Base64编解码适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_BASE64_H__
#define __SONOFF_BASE64_H__

#include <stdint.h>

/** @brief Base64接口返回值. */
#define SNF_BASE64_OK                   (0)     /* 成功 */
#define SNF_BASE64_ERR_INVALID_PARAM    (-1)    /* 参数错误 */
#define SNF_BASE64_ERR_BUFFER_TOO_SMALL (-2)    /* 输出缓冲区不足 */
#define SNF_BASE64_ERR_INVALID_DATA     (-3)    /* 输入数据非法 */
#define SNF_BASE64_ERR_INTERNAL         (-4)    /* 底层计算失败 */

/**
 * @brief 将二进制数据编码为Base64.
 *
 * out为NULL或out_size为0时只计算所需缓冲区长度, *out_len含结束符.
 * 编码成功时写入结束符, *out_len为不含结束符的字符数.
 * data_len为0时允许data为NULL.
 *
 * @param [out] out - 编码输出缓冲区, 查询长度时可为NULL.
 * @param [in] out_size - 输出缓冲区长度.
 * @param [out] out_len - 编码结果长度.
 * @param [in] data - 待编码数据.
 * @param [in] data_len - 待编码数据长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfBase64Encode(uint8_t *out, uint32_t out_size, uint32_t *out_len,
                    const uint8_t *data, uint32_t data_len);

/**
 * @brief 将Base64数据解码为二进制.
 *
 * out为NULL或out_size为0时只计算所需缓冲区长度.
 * data_len为0时允许data为NULL.
 *
 * @param [out] out - 解码输出缓冲区, 查询长度时可为NULL.
 * @param [in] out_size - 输出缓冲区长度.
 * @param [out] out_len - 解码结果长度.
 * @param [in] data - 待解码数据.
 * @param [in] data_len - 待解码数据长度.
 * @return 0表示成功, 负数表示失败.
 */
int snfBase64Decode(uint8_t *out, uint32_t out_size, uint32_t *out_len,
                    const uint8_t *data, uint32_t data_len);

#endif /* #ifndef __SONOFF_BASE64_H__ */
