/**
 * @file    sonoff_crc32.h
 * @brief   CRC32增量校验
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_CRC32_H__
#define __SONOFF_CRC32_H__

#include <stdint.h>

/**
 * @brief 追加数据并返回未最终异或的CRC32
 *
 * 使用反射多项式0xEDB88320，分块时将上次返回值作为crc继续计算。
 * 全部数据处理后异或UINT32_MAX得到标准CRC32，需要原始累计值时直接使用返回值。
 *
 * @param [in] crc - 累计CRC，首次传入UINT32_MAX.
 * @param [in] data - 待校验数据，size为0时允许NULL.
 * @param [in] size - 数据字节数，不得超过INT32_MAX.
 * @return 未最终异或的累计CRC32，size为0时保持crc不变.
 */
uint32_t snfCrc32(uint32_t crc, const uint8_t *data, uint32_t size);

#endif /* #ifndef __SONOFF_CRC32_H__ */
