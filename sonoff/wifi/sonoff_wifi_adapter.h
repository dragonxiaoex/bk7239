/**
 * @file    sonoff_wifi_adapter.h
 * @brief   SDK的WIFI驱动适配模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-24
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_WIFI_ADAPTER_H__
#define __SONOFF_WIFI_ADAPTER_H__

#include <stdint.h>

#define SNF_WIFI_SSID_MAX_LEN               32
#define SNF_WIFI_PASSWORD_MAX_LEN           64

typedef enum {
    SNF_WIFI_ADAPTER_OK = 0,
    SNF_WIFI_ADAPTER_ERR_INVALID_PARAM,
    SNF_WIFI_ADAPTER_ERR_NOT_INIT,
    SNF_WIFI_ADAPTER_ERR_BUSY,
    SNF_WIFI_ADAPTER_ERR_TIMEOUT,
    SNF_WIFI_ADAPTER_ERR_NOT_SUPPORTED,
    SNF_WIFI_ADAPTER_ERR_INTERNAL,
} SnfWifiAdapterErr;

typedef enum {
    SNF_WIFI_MODE_NONE = 0,
    SNF_WIFI_MODE_STA,
    SNF_WIFI_MODE_AP,
    SNF_WIFI_MODE_AP_STA,
} SnfWifiAdapterMode;

typedef enum {
    SNF_WIFI_ADP_EVT_CONNECTED = 0,
    SNF_WIFI_ADP_EVT_DISCONNECTED,
    SNF_WIFI_ADP_EVT_SCAN_DONE,
    SNF_WIFI_ADP_EVT_AP_STARTED,
    SNF_WIFI_ADP_EVT_AP_STOPPED,
    SNF_WIFI_ADP_EVT_AP_CLIENT_CONNECTED,
    SNF_WIFI_ADP_EVT_AP_CLIENT_DISCONNECTED,
} SnfWifiAdapterEvt;

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
    uint8_t bssid[6];
    int rssi;
    uint8_t channel;
    SnfWifiSecurity security;
} SnfWifiLinkInfo;

typedef struct {
    char ssid[SNF_WIFI_SSID_MAX_LEN + 1];
    char password[SNF_WIFI_PASSWORD_MAX_LEN + 1];
    uint8_t channel;                                /* AP工作信道 */
    uint8_t max_connections;                        /* AP最大客户端数量 */
    SnfWifiSecurity security;                       /* AP安全类型 */
} SnfWifiApConfig;

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

/**
 * @brief WIFI适配模块事件回调
 *
 * @param [in] event - 事件类型.
 * @param [in] event_data - 事件数据或 NULL.
 * @param [in] user_data - 用户数据, 在注册时提供.
 */
typedef void (*SnfWifiAdapterEventCB)(SnfWifiAdapterEvt event, const void *event_data, void *user_data);

int snfWifiAdapterInit(void);
int snfWifiAdapterDeinit(void);
int snfWifiAdapterSetMode(SnfWifiAdapterMode mode);
int snfWifiAdapterGetMode(void);

int snfWifiAdapterStaSetConfig(const SnfWifiStaConfig *config);
int snfWifiAdapterStaGetConfig(SnfWifiStaConfig *config);
int snfWifiAdapterStaConnect(void);
int snfWifiAdapterStaDisconnect(void);
int snfWifiAdapterStaGetLinkInfo(SnfWifiLinkInfo *info);
int snfWifiAdapterStaGetRssi(int *rssi);

int snfWifiAdapterScan(void);
int snfWifiAdapterScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count, uint16_t *result_count);
int snfWifiAdapterApSetConfig(const SnfWifiApConfig *config);

int snfWifiAdapterRegisterEventCallback(SnfWifiAdapterEventCB callback, void *user_data);
#endif /* __SONOFF_WIFI_ADAPTER_H__ */
