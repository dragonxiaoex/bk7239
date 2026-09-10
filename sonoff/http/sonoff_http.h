/**
 * @file    sonoff_http.h
 * @brief   HTTP服务端
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_HTTP_H__
#define __SONOFF_HTTP_H__

/**
 * @brief 启动HTTP服务.
 *
 * 服务启动后监听80端口，客户端访问根路径时返回包含设备控制和OTA入口的网页.
 * 重复调用不会创建新的服务任务.
 *
 * @return 0表示成功, 负数表示失败.
 */
int snfHttpServerStart(void);

#endif /* __SONOFF_HTTP_H__ */
