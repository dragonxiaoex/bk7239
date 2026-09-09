/**
 * @file    sonoff_wifi_type.h
 * @brief   低耦合wifi结构体定义
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date:   2026-08-25
 * 
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 * 
 */

#ifndef __SONOFF_WIFI_TYPE_H__
#define __SONOFF_WIFI_TYPE_H__

#include <stdint.h>

/** @brief WIFI凭证长度. */
#define SNF_WIFI_SSID_MAX_LEN               (32)        /* SSID最大长度 */
#define SNF_WIFI_PASSWORD_MAX_LEN           (64)        /* 密码最大长度 */

/**
 * @brief WIFI模块通用事件回调
 *
 * @param [in] event - 事件类型.
 * @param [in] event_data - 事件数据或 NULL.
 * @param [in] user_data - 用户数据, 在注册时提供.
 */
typedef void (*SnfWifiEventCB)(int event, const void *event_data, void *user_data);

typedef enum {
    SNF_WIFI_MODE_NONE = 0,
    SNF_WIFI_MODE_STA,
    SNF_WIFI_MODE_AP,
    SNF_WIFI_MODE_AP_STA,
} SnfWifiMode;

typedef enum {
    SNF_WIFI_SECURITY_OPEN,
    SNF_WIFI_SECURITY_WEP,
    SNF_WIFI_SECURITY_WPA,
    SNF_WIFI_SECURITY_WPA2,
    SNF_WIFI_SECURITY_WPA3,
    SNF_WIFI_SECURITY_UNKNOWN,
} SnfWifiSecurity;

typedef struct {
    char ssid[SNF_WIFI_SSID_MAX_LEN + 1];
    char password[SNF_WIFI_PASSWORD_MAX_LEN + 1];
} SnfWifiStaConfig;

typedef struct {
    char ssid[SNF_WIFI_SSID_MAX_LEN + 1];
    char password[SNF_WIFI_PASSWORD_MAX_LEN + 1];
    uint8_t channel;                                /* AP工作信道 */
    uint8_t max_connections;                        /* AP最大客户端数量 */
    SnfWifiSecurity security;                       /* AP安全类型 */
} SnfWifiApConfig;

typedef struct {
    char ssid[SNF_WIFI_SSID_MAX_LEN + 1];
    uint8_t bssid[6];
    int rssi;
    uint8_t channel;
    SnfWifiSecurity security;
} SnfWifiLinkInfo;

typedef enum {
    SNF_WIFI_DISCONNECT_REASON_UNKNOWN = 0,
    SNF_WIFI_DISCONNECT_REASON_USER,
    SNF_WIFI_DISCONNECT_REASON_AUTH_FAILED,
    SNF_WIFI_DISCONNECT_REASON_AP_NOT_FOUND,
    SNF_WIFI_DISCONNECT_REASON_ASSOC_FAILED,
    SNF_WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT,
    SNF_WIFI_DISCONNECT_REASON_BEACON_LOST,
    SNF_WIFI_DISCONNECT_REASON_AP_DISCONNECTED,
} SnfWifiDisconnectReason;

typedef struct {
    SnfWifiDisconnectReason reason;
} SnfWifiStaDisconnectedEvent;

/**
 * @brief AP客户端事件信息
 */
typedef struct {
    uint8_t mac[6];
    uint32_t ip_addr;
} SnfWifiApClientEvent;

#endif /* __SONOFF_WIFI_TYPE_H__ */
