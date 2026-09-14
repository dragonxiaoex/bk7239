/**
 * @file    sonoff_http.h
 * @brief   HTTP RPC与WebSocket服务端
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-14
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_HTTP_H__
#define __SONOFF_HTTP_H__

/**
 * @brief 启动80端口的HTTP RPC与WebSocket服务，重复调用不创建新任务.
 * @note 应用先完成RPC初始化和业务方法注册；未初始化时请求返回503.
 * @note POST /rpc使用短连接；GET /rpc可升级为WebSocket。服务不返回网页.
 * @return 0表示成功，负数表示失败.
 */
int snfHttpServerStart(void);

/**
 * @brief 设置HTTP摘要认证凭据；初始未配置时不启用HTTP认证.
 * @param [in] username - 用户名，与password同时为NULL时清除认证配置.
 * @param [in] password - 密码，允许空字符串，接口返回后无需保留.
 * @param [in] realm - 认证域，NULL使用默认值sonoff.
 * @note 仅保存计算后的HA1；更新配置使已有HTTP质询失效。不修改WebSocket的RPC认证配置.
 * @return 0表示成功，负数表示参数或计算失败.
 */
int snfHttpSetAuthInfo(const char *username, const char *password, const char *realm);

/**
 * @brief 设置需要HTTP摘要认证的资源路径，默认为/rpc.
 * @param [in] uri - 精确匹配的路径，例如/rpc；不是登录入口，不创建新路由.
 * @note 仅影响普通HTTP请求，WebSocket使用RPC消息中的auth字段.
 * @return 0表示成功，负数表示参数无效.
 */
int snfHttpSetAuthUri(const char *uri);

#endif /* __SONOFF_HTTP_H__ */
