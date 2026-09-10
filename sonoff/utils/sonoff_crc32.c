/**
 * @file    sonoff_crc32.c
 * @brief   CRC32增量校验
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>

#include <bk_private/CheckSumUtils.h>

#include "sonoff_crc32.h"

uint32_t snfCrc32(uint32_t crc, const uint8_t *data, uint32_t size)
{
    CRC32_Context context;

    CRC32_Init(&context);
    context.crc = crc;
    CRC32_Update(&context, data, size);
    CRC32_Final(&context, &crc);

    return crc;
}
