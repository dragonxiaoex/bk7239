/**
 * @file    sonoff_nvdm_config.c
 * @brief   NVDM条目配置
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>
#include <string.h>

#include "sonoff_log.h"
#include "sonoff_nvdm.h"

#include "sonoff_nvdm_config.h"

static const char *tag = "SNF-NVDM-CFG";

/**
 * @brief 检查产品识别码是否为14位十进制数字.
 *
 * @param [in] serial_number - 待检查的识别码字符串.
 * @return 1表示格式合法, 0表示非法.
 */
static int serialNumberIsValid(const char *serial_number)
{
    uint16_t i;

    if (serial_number == NULL)
    {
        return 0;
    }

    if (strlen(serial_number) != NVDM_FACTORY_SERIAL_NUMBER_LEN)
    {
        return 0;
    }

    for (i = 0; i < NVDM_FACTORY_SERIAL_NUMBER_LEN; i++)
    {
        if ((serial_number[i] < '0') || (serial_number[i] > '9'))
        {
            return 0;
        }
    }

    return 1;
}

int snfSerialNumberGet(char *serial_number, uint16_t serial_number_size)
{
    int ret;

    if ((serial_number == NULL) || (serial_number_size <= NVDM_FACTORY_SERIAL_NUMBER_LEN))
    {
        return -1;
    }

    memset(serial_number, 0, serial_number_size);
    ret = snfNvdmReadStr(NVDM_FAC_GROUP,
                         NVDM_FACTORY_ITEM_SERIAL_NUMBER,
                         (uint8_t *)serial_number,
                         (int)serial_number_size);
    if (ret != 0)
    {
        return -1;
    }

    if (serialNumberIsValid(serial_number) == 0)
    {
        return -1;
    }

    return 0;
}

int snfSerialNumberSet(const char *serial_number)
{
    int len;
    int ret;

    if (serialNumberIsValid(serial_number) == 0)
    {
        return -1;
    }

    len = (int)strlen(serial_number) + 1;
    ret = snfNvdmWriteStr(NVDM_FAC_GROUP,
                          NVDM_FACTORY_ITEM_SERIAL_NUMBER,
                          (const uint8_t *)serial_number,
                          len);
    if (ret != 0)
    {
        LOG_E(tag, "serial number write failed");
        return -1;
    }

    return 0;
}
