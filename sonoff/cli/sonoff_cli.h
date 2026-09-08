/**
 * @file    sonoff_cli.h
 * @brief   Sonoff串口工具箱命令模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-28
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_CLI_H__
#define __SONOFF_CLI_H__

#include <stdint.h>

/**
 * @brief 等待串口产测进入命令factory!.
 *
 * 仅供启动线程在snfCliInit成功后调用, 不支持并发等待.
 * 命令结束符沿用SDK CLI规则, 支持factory!\r\n.
 *
 * @param [in] timeout_ms - 等待超时时间, 单位毫秒.
 * @return 0表示收到命令, 负数表示超时或等待失败.
 */
int snfCliWaitFactoryReply(uint32_t timeout_ms);

/**
 * @brief 初始化Sonoff串口工具箱命令.
 *
 * @return 0表示成功, 非0表示失败.
 */
int snfCliInit(void);

#endif /* __SONOFF_CLI_H__ */
