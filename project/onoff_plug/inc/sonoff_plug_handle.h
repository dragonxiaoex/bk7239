/**
 * @file    sonoff_plug_handle.h
 * @brief   插座开关控制
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_PLUG_HANDLE_H__
#define __SONOFF_PLUG_HANDLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "cJSON.h"

/**
 * @brief 处理Switch.Set请求，params必须包含id=0和布尔类型的on.
 * @param [in] method - RPC方法名，由注册关系确定.
 * @param [in] params - RPC参数对象，仅借用.
 * @param [in] user_ctx - 注册上下文，当前不使用.
 * @return 独立的result/error包装对象，所有权交给RPC；分配失败返回NULL.
 */
cJSON *snfPlugRpcSwitchSet(const char *method, cJSON *params, void *user_ctx);

/**
 * @brief 设置插座开关(不向Matter上报).
 *
 * @param [in] onoff - 非0表示开, 0表示关.
 * @return 0表示成功, 负数表示失败.
 */
int snfPlugOnOffRawSet(uint8_t onoff);

/**
 * @brief 设置插座开关(向Matter上报).
 *
 * @param [in] onoff - 非0表示开, 0表示关.
 * @return 0表示成功, 负数表示失败.
 */
int snfPlugOnOffSet(uint8_t onoff);

/**
 * @brief 读取插座开关状态.
 *
 * @return 非0表示开, 0表示关.
 */
int snfPlugOnOffGet(void);

/**
 * @brief 上报插座开关状态到Matter.(C++)
 *
 * @param [in] onoff - 非0表示开, 0表示关.
 * @return 0表示成功, 负数表示失败.
 */
int snfMatterOnOffReport(uint8_t onoff);

/**
 * @brief 初始化插座控制GPIO.
 */
void snfPlugHandleInit(void);

/**
 * @brief 反初始化插座控制GPIO.
 */
void snfPlugHandleDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __SONOFF_PLUG_HANDLE_H__ */
