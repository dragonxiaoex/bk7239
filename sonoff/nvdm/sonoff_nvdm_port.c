/**
 * @file    sonoff_nvdm_port.c
 * @brief   NVDM平台适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk_ef.h"

#include "sonoff_nvdm_port.h"

/**
 * @brief 将group与key拼接为EasyFlash键名.
 *
 * @param [in] group - 配置组名称.
 * @param [in] key - 配置键名称.
 * @param [out] out - 拼接后的键名缓冲区.
 * @param [in] out_size - 键名缓冲区长度.
 * @return 0表示拼接成功, 负数表示参数非法或键名超长.
 */
static int nvdmPortMakeKey(const char *group, const char *key, char *out, size_t out_size)
{
    int written;

    if ((group == NULL) || (group[0] == '\0')
        || (key == NULL) || (key[0] == '\0')
        || (out == NULL) || (out_size == 0))
    {
        return -1;
    }

    written = snprintf(out, out_size, "%s.%s", group, key);
    if ((written <= 0) || ((size_t)written >= out_size))
    {
        return -1;
    }

    return 0;
}

int snfNvdmPortExistStatus(const char *group, const char *key)
{
    char flash_key[EF_ENV_NAME_MAX + 1];
    struct env_node_obj env;

    if (nvdmPortMakeKey(group, key, flash_key, sizeof(flash_key)) != 0)
    {
        return -1;
    }

    memset(&env, 0, sizeof(env));
    if (!ef_get_env_obj(flash_key, &env))
    {
        return -1;
    }

    return 0;
}

int snfNvdmPortReadStr(const char *group, const char *key, uint8_t *buff, int len)
{
    char flash_key[EF_ENV_NAME_MAX + 1];
    int read_len;

    if ((buff == NULL) || (len <= 0))
    {
        return -1;
    }

    if (nvdmPortMakeKey(group, key, flash_key, sizeof(flash_key)) != 0)
    {
        return -1;
    }

    memset(buff, 0, (size_t)len);
    read_len = bk_get_env_enhance(flash_key, buff, len);
    if (read_len <= 0)
    {
        return -1;
    }

    if (read_len < len)
    {
        buff[read_len] = '\0';
    }
    else
    {
        buff[len - 1] = '\0';
    }

    return 0;
}

int snfNvdmPortWriteStr(const char *group, const char *key, const uint8_t *value, int len)
{
    char flash_key[EF_ENV_NAME_MAX + 1];

    if ((value == NULL) || (len <= 0))
    {
        return -1;
    }

    if (nvdmPortMakeKey(group, key, flash_key, sizeof(flash_key)) != 0)
    {
        return -1;
    }

    if (bk_set_env_enhance(flash_key, value, len) != EF_NO_ERR)
    {
        return -1;
    }

    return 0;
}

int snfNvdmPortDelete(const char *group, const char *key)
{
    char flash_key[EF_ENV_NAME_MAX + 1];
    EfErrCode result;

    if (nvdmPortMakeKey(group, key, flash_key, sizeof(flash_key)) != 0)
    {
        return -1;
    }

    result = ef_del_env(flash_key);
    if ((result != EF_NO_ERR) && (result != EF_ENV_NAME_ERR))
    {
        return -1;
    }

    return 0;
}
