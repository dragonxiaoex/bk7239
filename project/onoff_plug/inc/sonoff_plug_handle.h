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
