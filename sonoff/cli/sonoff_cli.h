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

/**
 * @brief 初始化Sonoff串口工具箱命令.
 *
 * @return 0表示成功, 非0表示失败.
 */
int snfCliInit(void);

#endif /* __SONOFF_CLI_H__ */
