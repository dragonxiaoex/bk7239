/**
 * @file    sonoff_plug_handle.c
 * @brief   插座开关控制
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>

#include <driver/gpio.h>

#include "sonoff_log.h"
#include "sonoff_plug_handle.h"
#include "sonoff_private_item.h"
#include "sonoff_rpc.h"

static const char *tag = "SNF-PLUG";

#define SNF_PLUG_CONTROL_GPIO GPIO_18

/**
 * @brief 构造开关RPC的结果或错误包装对象.
 * @param [in] code - SNF_RPC_OK表示成功，其他值为错误码.
 * @return 独立JSON对象，分配失败返回NULL.
 */
static cJSON *plugRpcCreateResponse(int32_t code)
{
    cJSON *response = cJSON_CreateObject();
    cJSON *payload = cJSON_CreateObject();

    if ((response == NULL) || (payload == NULL))
    {
        cJSON_Delete(response);
        cJSON_Delete(payload);
        return NULL;
    }

    if (code == SNF_RPC_OK)
    {
        cJSON_AddItemToObjectCS(response, "result", payload);
    }
    else
    {
        cJSON_AddNumberToObject(payload, "code", code);
        cJSON_AddStringToObject(payload, "message", code == SNF_RPC_ERR_INVALID_PARAMS
                               ? "Expected params.id=0 and boolean params.on" : "Failed to set switch");
        cJSON_AddItemToObjectCS(response, "error", payload);
    }

    return response;
}

cJSON *snfPlugRpcSwitchSet(const char *method, cJSON *params, void *user_ctx)
{
    cJSON *id;
    cJSON *on;
    cJSON *response;
    uint32_t on_type;

    if (!JSON_IS_OBJECT(params))
    {
        return plugRpcCreateResponse(SNF_RPC_ERR_INVALID_PARAMS);
    }

    id = cJSON_GetObjectItem(params, "id");
    on = cJSON_GetObjectItem(params, "on");
    if (!JSON_IS_NUMBER(id) || (id->valuedouble != 0) || (on == NULL))
    {
        return plugRpcCreateResponse(SNF_RPC_ERR_INVALID_PARAMS);
    }

    on_type = (uint32_t)on->type & JSON_TYPE_MASK;
    if ((on_type != cJSON_True) && (on_type != cJSON_False))
    {
        return plugRpcCreateResponse(SNF_RPC_ERR_INVALID_PARAMS);
    }

    /* 先准备成功响应，分配失败时不改变开关；返回对象的所有权交给RPC。 */
    response = plugRpcCreateResponse(SNF_RPC_OK);
    if (response == NULL)
    {
        return NULL;
    }

    if (snfPlugOnOffSet(on_type == cJSON_True ? 1 : 0) != 0)
    {
        cJSON_Delete(response);
        return plugRpcCreateResponse(SNF_RPC_ERR_INTERNAL);
    }

    return response;
}

int snfPlugOnOffRawSet(uint8_t onoff)
{
    bk_err_t ret;

    if (onoff != 0)
    {
        ret = bk_gpio_set_output_high(SNF_PLUG_CONTROL_GPIO);
    }
    else
    {
        ret = bk_gpio_set_output_low(SNF_PLUG_CONTROL_GPIO);
    }

    if (ret != BK_OK)
    {
        LOG_E(tag, "set %d failed, ret=%d", onoff, ret);
        return -1;
    }

    snfNvdmPlugOnOffSet(onoff);

    return 0;
}

int snfPlugOnOffSet(uint8_t onoff)
{
    if (snfPlugOnOffRawSet(onoff) != 0)
    {
        return -1;
    }

    LOG_I(tag, "set plug onoff to %d", onoff);

    /* if (snfMatterOnOffReport(onoff) != 0)
    {
        return -1;
    } */

    return 0;
}

int snfPlugOnOffGet(void)
{
    if (bk_gpio_get_output(SNF_PLUG_CONTROL_GPIO) != 0)
    {
        return 1;
    }

    return 0;
}

void snfPlugHandleInit(void)
{
    int onoff = 0;
    if (bk_gpio_disable_input(SNF_PLUG_CONTROL_GPIO) != BK_OK)
    {
        LOG_E(tag, "disable input failed");
        return;
    }

    if (bk_gpio_enable_output(SNF_PLUG_CONTROL_GPIO) != BK_OK)
    {
        LOG_E(tag, "enable output failed");
        return;
    }

    onoff = snfNvdmPlugOnOffGet();
    if(onoff == 1)
    {
        bk_gpio_set_output_high(SNF_PLUG_CONTROL_GPIO);
    }
    else
    {
        bk_gpio_set_output_low(SNF_PLUG_CONTROL_GPIO);
    }

    snfRpcMethodRegister("Switch.Set", snfPlugRpcSwitchSet, NULL);

    return;
}

void snfPlugHandleDeinit(void)
{
    if (bk_gpio_disable_output(SNF_PLUG_CONTROL_GPIO) != BK_OK)
    {
        LOG_E(tag, "disable output failed");
    }

    return;
}
