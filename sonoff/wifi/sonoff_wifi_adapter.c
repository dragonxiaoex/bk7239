/**
 * @file    sonoff_wifi_adapter.c
 * @brief   SDK的WIFI驱动适配模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-24
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <components/netif_types.h>
#include <components/netif.h>
#include <common/bk_err.h>
#include <components/event.h>
#include <generated/lmac_wifi_adapter.h>
#include <modules/wifi.h>

#include "sonoff_wifi_adapter.h"

#define SNF_DEFAULT_AP_IP_ADDR     "192.168.100.1"
#define SNF_DEFAULT_AP_NETMASK     "255.255.255.0"
#define SNF_DEFAULT_AP_GATEWAY     "192.168.100.1"
#define SNF_DEFAULT_AP_DNS         "192.168.100.1"

extern bool bk_get_wifi_is_inited(void);

typedef struct {
    int init;                              /* 适配器初始化状态 */
    int sta_started;                       /* STA启动状态 */
    int ap_started;                        /* AP启动状态 */
    int scan_started;                      /* 扫描启动状态 */
    SnfWifiAdapterMode mode;               /* WIFI工作模式 */
    SnfWifiAdapterEventCB event_callback;  /* 适配器事件回调 */
    void *event_user_data;                 /* 适配器事件回调用户数据 */
} SnfWifiAdapterState;

static SnfWifiAdapterState adapter_state = {
    .init = 0,
    .sta_started = 0,
    .ap_started = 0,
    .scan_started = 0,
    .mode = SNF_WIFI_MODE_NONE,
    .event_callback = NULL,
    .event_user_data = NULL,
};

static int snfValidateString(const char *value, size_t value_size, size_t max_length, size_t *length)
{
    size_t value_length;

    if ((value == NULL) || (length == NULL))
    {
        return -1;
    }

    value_length = strnlen(value, value_size);
    if (value_length > max_length)
    {
        return -1;
    }

    *length = value_length;

    return 0;
}

static int snfMapSdkError(bk_err_t sdk_error)
{
    int adapter_error = SNF_WIFI_ADAPTER_ERR_INTERNAL;

    switch (sdk_error)
    {
        case BK_OK:
            adapter_error = SNF_WIFI_ADAPTER_OK;
            break;
        case BK_ERR_NULL_PARAM:
        case BK_ERR_PARAM:
        case BK_ERR_WIFI_CHAN_RANGE:
        case BK_ERR_WIFI_CHAN_NUMBER:
        case BK_ERR_WIFI_COUNTRY_POLICY:
        case BK_ERR_WIFI_RESERVED_FIELD:
        case BK_ERR_WIFI_STA_NOT_CONFIG:
        case BK_ERR_WIFI_AP_NOT_CONFIG:
            adapter_error = SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
            break;
        case BK_ERR_WIFI_NOT_INIT:
        case BK_ERR_WIFI_STA_NOT_STARTED:
        case BK_ERR_WIFI_AP_NOT_STARTED:
        case BK_ERR_NOT_INIT:
            adapter_error = SNF_WIFI_ADAPTER_ERR_NOT_INIT;
            break;
        case BK_ERR_BUSY:
        case BK_ERR_IN_PROGRESS:
            adapter_error = SNF_WIFI_ADAPTER_ERR_BUSY;
            break;
        case BK_ERR_TIMEOUT:
            adapter_error = SNF_WIFI_ADAPTER_ERR_TIMEOUT;
            break;
        case BK_ERR_NOT_SUPPORT:
            adapter_error = SNF_WIFI_ADAPTER_ERR_NOT_SUPPORTED;
            break;
        default:
            adapter_error = SNF_WIFI_ADAPTER_ERR_INTERNAL;
            break;
    }

    return adapter_error;
}

static int snfInitStateGet(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;

    if (adp_state->init == 0)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    return SNF_WIFI_ADAPTER_OK;
}

static SnfWifiSecurity snfSecurityFromSdk(wifi_security_t security)
{
    SnfWifiSecurity adapter_security = SNF_WIFI_SECURITY_UNKNOWN;

    switch (security)
    {
        case WIFI_SECURITY_NONE:
            adapter_security = SNF_WIFI_SECURITY_OPEN;
            break;
        case WIFI_SECURITY_WEP:
            adapter_security = SNF_WIFI_SECURITY_WEP;
            break;
        case WIFI_SECURITY_WPA_TKIP:
        case WIFI_SECURITY_WPA_AES:
        case WIFI_SECURITY_WPA_MIXED:
            adapter_security = SNF_WIFI_SECURITY_WPA;
            break;
        case WIFI_SECURITY_WPA2_TKIP:
        case WIFI_SECURITY_WPA2_AES:
        case WIFI_SECURITY_WPA2_MIXED:
            adapter_security = SNF_WIFI_SECURITY_WPA2;
            break;
        case WIFI_SECURITY_WPA3_SAE:
        case WIFI_SECURITY_WPA3_WPA2_MIXED:
            adapter_security = SNF_WIFI_SECURITY_WPA3;
            break;
        default:
            adapter_security = SNF_WIFI_SECURITY_UNKNOWN;
            break;
    }

    return adapter_security;
}

static int snfSecurityToSdk(SnfWifiSecurity security, wifi_security_t *sdk_security)
{
    if (sdk_security == NULL)
    {
        return -1;
    }

    switch (security)
    {
        case SNF_WIFI_SECURITY_OPEN:
            *sdk_security = WIFI_SECURITY_NONE;
            break;
        case SNF_WIFI_SECURITY_WEP:
            *sdk_security = WIFI_SECURITY_WEP;
            break;
        case SNF_WIFI_SECURITY_WPA:
            *sdk_security = WIFI_SECURITY_WPA_MIXED;
            break;
        case SNF_WIFI_SECURITY_WPA2:
            *sdk_security = WIFI_SECURITY_WPA2_MIXED;
            break;
        case SNF_WIFI_SECURITY_WPA3:
            *sdk_security = WIFI_SECURITY_WPA3_WPA2_MIXED;
            break;
        default:
            return -1;
    }

    return 0;
}

static SnfWifiDisconnectReason snfDisconnectReasonFromSdk(const wifi_event_sta_disconnected_t *event_data)
{
    SnfWifiDisconnectReason reason = SNF_WIFI_DISCONNECT_REASON_UNKNOWN;

    if (event_data == NULL)
    {
        return reason;
    }

    if (event_data->local_generated)
    {
        reason = SNF_WIFI_DISCONNECT_REASON_USER;
    }
    else
    {
        switch (event_data->disconnect_reason)
        {
            case WIFI_REASON_WRONG_PASSWORD:
            case WIFI_REASON_SECURITY_LEVEL_NOT_MATCH:
            case WIFI_REASON_IEEE_802_1X_AUTH_FAILED:
            case WIFI_REASON_BAD_CIPHER_OR_AKM:
                reason = SNF_WIFI_DISCONNECT_REASON_AUTH_FAILED;
                break;
            case WIFI_REASON_NO_AP_FOUND:
                reason = SNF_WIFI_DISCONNECT_REASON_AP_NOT_FOUND;
                break;
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                reason = SNF_WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT;
                break;
            case WIFI_REASON_BEACON_LOST:
                reason = SNF_WIFI_DISCONNECT_REASON_BEACON_LOST;
                break;
            case WIFI_REASON_DISASSOC_AP_BUSY:
            case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:
            case WIFI_REASON_DISASSOC_LOW_ACK:
                reason = SNF_WIFI_DISCONNECT_REASON_AP_DISCONNECTED;
                break;
            default:
                reason = SNF_WIFI_DISCONNECT_REASON_ASSOC_FAILED;
                break;
        }
    }

    return reason;
}

static void snfNotifyEvent(SnfWifiAdapterEvt event, const void *event_data)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    SnfWifiAdapterEventCB callback = adp_state->event_callback;
    void *user_data = adp_state->event_user_data;

    if (callback != NULL)
    {
        callback(event, event_data, user_data);
    }
}

static bk_err_t snfSdkEventHandler(void *arg, event_module_t event_module,
                                   int event_id, void *event_data)
{
    SnfWifiAdapterState *adp_state = &adapter_state;

    (void)arg;
    (void)event_module;

    switch (event_id)
    {
        case EVENT_WIFI_SCAN_DONE:
            adp_state->scan_started = 0;
            snfNotifyEvent(SNF_WIFI_ADP_EVT_SCAN_DONE, NULL);
            break;
        case EVENT_WIFI_STA_CONNECTED:
        {
            SnfWifiLinkInfo link_info = {0};

            if (snfWifiAdapterStaGetLinkInfo(&link_info) == SNF_WIFI_ADAPTER_OK)
            {
                snfNotifyEvent(SNF_WIFI_ADP_EVT_CONNECTED, &link_info);
            }
            else
            {
                snfNotifyEvent(SNF_WIFI_ADP_EVT_CONNECTED, NULL);
            }
            break;
        }
        case EVENT_WIFI_STA_DISCONNECTED:
        {
            SnfWifiStaDisconnectedEvent disconnect_event = {0};

            disconnect_event.reason = snfDisconnectReasonFromSdk(
                (const wifi_event_sta_disconnected_t *)event_data);
            snfNotifyEvent(SNF_WIFI_ADP_EVT_DISCONNECTED, &disconnect_event);
            break;
        }
        case EVENT_WIFI_AP_START:
            snfNotifyEvent(SNF_WIFI_ADP_EVT_AP_STARTED, NULL);
            break;
        case EVENT_WIFI_AP_STOP:
            snfNotifyEvent(SNF_WIFI_ADP_EVT_AP_STOPPED, NULL);
            break;
        case EVENT_WIFI_AP_CONNECTED:
        {
            SnfWifiApClientEvent client_event = {0};
            const wifi_event_ap_connected_t *sdk_event_data =
                (const wifi_event_ap_connected_t *)event_data;

            if (sdk_event_data != NULL)
            {
                memcpy(client_event.mac, sdk_event_data->mac, sizeof(client_event.mac));
                client_event.ip_addr = sdk_event_data->ipaddr;
            }
            snfNotifyEvent(SNF_WIFI_ADP_EVT_AP_CLIENT_CONNECTED, &client_event);
            break;
        }
        case EVENT_WIFI_AP_DISCONNECTED:
        {
            SnfWifiApClientEvent client_event = {0};
            const wifi_event_ap_disconnected_t *sdk_event_data =
                (const wifi_event_ap_disconnected_t *)event_data;

            if (sdk_event_data != NULL)
            {
                memcpy(client_event.mac, sdk_event_data->mac, sizeof(client_event.mac));
            }
            snfNotifyEvent(SNF_WIFI_ADP_EVT_AP_CLIENT_DISCONNECTED, &client_event);
            break;
        }
        default:
            break;
    }

    return BK_OK;
}

static int snfStartSta(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    bk_err_t sdk_error;

    sdk_error = bk_wifi_sta_split_init();
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    sdk_error = bk_wifi_sta_split_start();
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    adp_state->sta_started = 1;

    return SNF_WIFI_ADAPTER_OK;
}

static int snfStopSta(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    bk_err_t sdk_error;

    if (adp_state->sta_started == 0)
    {
        return SNF_WIFI_ADAPTER_OK;
    }

    (void)bk_wifi_sta_disconnect();
    sdk_error = bk_wifi_sta_stop();
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    adp_state->sta_started = 0;

    return SNF_WIFI_ADAPTER_OK;
}

static int snfStartAp(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    bk_err_t sdk_error;

    sdk_error = bk_wifi_ap_start();
    if(sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    adp_state->ap_started = 1;

    return SNF_WIFI_ADAPTER_OK;
}

static int snfStopAp(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    bk_err_t sdk_error;

    if (adp_state->ap_started == 0)
    {
        return SNF_WIFI_ADAPTER_OK;
    }

    sdk_error = bk_wifi_ap_stop();
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    adp_state->ap_started = 0;

    return snfMapSdkError(sdk_error);
}

/**
 * @brief 初始化WIFI适配模块及SDK事件桥接
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterInit(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    bk_err_t sdk_error;
    wifi_init_config_t init_config;
    int adapter_error;

    if (adp_state->init != 0)
    {
        return SNF_WIFI_ADAPTER_OK;
    }

    sdk_error = bk_event_init();
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    if (!bk_get_wifi_is_inited())
    {
        init_config = (wifi_init_config_t)WIFI_DEFAULT_INIT_CONFIG();
        sdk_error = bk_wifi_init(&init_config);
        if (sdk_error != BK_OK)
        {
            return snfMapSdkError(sdk_error);
        }
    }

    sdk_error = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_ID_ALL,
                                      snfSdkEventHandler, NULL);
    if ((sdk_error != BK_OK) && (sdk_error != BK_ERR_EVENT_CB_EXIST))
    {
        adapter_error = snfMapSdkError(sdk_error);
        return adapter_error;
    }

    adp_state->init = 1;
    adp_state->sta_started = 0;
    adp_state->ap_started = 0;
    adp_state->scan_started = 0;
    adp_state->mode = SNF_WIFI_MODE_NONE;

    return SNF_WIFI_ADAPTER_OK;
}

/**
 * @brief 反初始化WIFI适配模块及SDK事件桥接，当前SDK不支持
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterDeinit(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    int adapter_error;
    int current_error;
    bk_err_t sdk_error;

    if (adp_state->init == 0)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    if (adp_state->scan_started != 0)
    {
        bk_wifi_scan_stop();
        adp_state->scan_started = 0;
    }

    adapter_error = snfWifiAdapterSetMode(SNF_WIFI_MODE_NONE);

    sdk_error = bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_ID_ALL,
                                        snfSdkEventHandler);
    current_error = snfMapSdkError(sdk_error);
    if ((current_error != SNF_WIFI_ADAPTER_OK)
        && (sdk_error != BK_ERR_EVENT_NO_CB))
    {
        if (adapter_error == SNF_WIFI_ADAPTER_OK)
        {
            adapter_error = current_error;
        }
    }

    bk_wifi_deinit();
    adp_state->init = 0;
    adp_state->sta_started = 0;
    adp_state->ap_started = 0;
    adp_state->scan_started = 0;
    adp_state->mode = SNF_WIFI_MODE_NONE;
    adp_state->event_callback = NULL;
    adp_state->event_user_data = NULL;

    return adapter_error;
}

/**
 * @brief 设置WIFI工作模式并立即启停对应接口
 *
 * AP 须先调用 snfWifiAdapterApSetConfig；STA 仅启动接口, 不会自动连接.
 * 目标模式与当前一致且接口已就绪时直接返回成功.
 *
 * @param [in] mode - WIFI工作模式.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterSetMode(SnfWifiAdapterMode mode)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    int adapter_error;
    int sta_need_start = 0, ap_need_start = 0;
    int sta_here_started = 0, ap_here_started = 0;

    adapter_error = snfInitStateGet();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        return adapter_error;
    }

    if ((mode < SNF_WIFI_MODE_NONE) || (mode > SNF_WIFI_MODE_AP_STA))
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    if (mode == adp_state->mode)
    {
        return SNF_WIFI_ADAPTER_OK;
    }

    sta_need_start = ((mode == SNF_WIFI_MODE_STA) || (mode == SNF_WIFI_MODE_AP_STA));
    ap_need_start = ((mode == SNF_WIFI_MODE_AP) || (mode == SNF_WIFI_MODE_AP_STA));

    if (sta_need_start && (adp_state->sta_started == 0))
    {
        adapter_error = snfStartSta();
        if (adapter_error != SNF_WIFI_ADAPTER_OK)
        {
            return adapter_error;
        }
        sta_here_started = 1;
    }

    if (ap_need_start && (adp_state->ap_started == 0))
    {
        adapter_error = snfStartAp();
        if (adapter_error != SNF_WIFI_ADAPTER_OK)
        {
            if (sta_here_started)
            {
                (void)snfStopSta();
            }

            return adapter_error;
        }
        ap_here_started = 1;
    }

    if ((sta_need_start == 0) && (adp_state->sta_started != 0))
    {
        adapter_error = snfStopSta();
        if (adapter_error != SNF_WIFI_ADAPTER_OK)
        {
            if (ap_here_started)
            {
                (void)snfStopAp();
            }

            return adapter_error;
        }
    }

    if ((ap_need_start == 0) && (adp_state->ap_started != 0))
    {
        adapter_error = snfStopAp();
        if (adapter_error != SNF_WIFI_ADAPTER_OK)
        {
            if (sta_here_started)
            {
                (void)snfStopSta();
            }

            return adapter_error;
        }
    }

    adp_state->mode = mode;

    return SNF_WIFI_ADAPTER_OK;
}

/**
 * @brief 获取当前WIFI工作模式
 *
 * @return 当前WIFI工作模式, 未初始化时返回 SNF_WIFI_MODE_NONE.
 */
int snfWifiAdapterGetMode(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;

    return adp_state->mode;
}

/**
 * @brief 配置STA连接参数
 *
 * @param [in] config - STA配置参数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaSetConfig(const SnfWifiStaConfig *config)
{
    wifi_sta_config_t sdk_config = {0};
    size_t ssid_length;
    size_t password_length;
    bk_err_t sdk_error;
    int adapter_error;

    adapter_error = snfInitStateGet();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        return adapter_error;
    }

    if ((config == NULL)
        || (snfValidateString(config->ssid, sizeof(config->ssid),
                              SNF_WIFI_SSID_MAX_LEN, &ssid_length) != 0)
        || (snfValidateString(config->password, sizeof(config->password),
                              SNF_WIFI_PASSWORD_MAX_LEN, &password_length) != 0)
        || (ssid_length == 0U))
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    memcpy(sdk_config.ssid, config->ssid, ssid_length + 1U);
    memcpy(sdk_config.password, config->password, password_length + 1U);
    sdk_config.security = WIFI_SECURITY_AUTO;
    sdk_error = bk_wifi_sta_set_config(&sdk_config);

    return snfMapSdkError(sdk_error);
}

/**
 * @brief 获取STA连接参数
 *
 * @param [out] config - STA配置参数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaGetConfig(SnfWifiStaConfig *config)
{
    wifi_sta_config_t sdk_config = {0};
    bk_err_t sdk_error;

    if (snfInitStateGet() != SNF_WIFI_ADAPTER_OK)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    if (config == NULL)
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    sdk_error = bk_wifi_sta_get_config(&sdk_config);
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    memset(config, 0, sizeof(*config));
    memcpy(config->ssid, sdk_config.ssid, sizeof(config->ssid) - 1U);
    memcpy(config->password, sdk_config.password, sizeof(config->password) - 1U);
    config->ssid[sizeof(config->ssid) - 1U] = '\0';
    config->password[sizeof(config->password) - 1U] = '\0';

    return SNF_WIFI_ADAPTER_OK;
}

/**
 * @brief 使用已配置参数连接STA
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaConnect(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    bk_err_t sdk_error;
    int adapter_error;

    adapter_error = snfInitStateGet();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        return adapter_error;
    }

    if (adp_state->sta_started == 0)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    sdk_error = bk_wifi_sta_split_connect();

    return snfMapSdkError(sdk_error);
}

/**
 * @brief 断开STA与当前AP的连接
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaDisconnect(void)
{
    bk_err_t sdk_error;
    int adapter_error;

    adapter_error = snfInitStateGet();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        return adapter_error;
    }

    sdk_error = bk_wifi_sta_disconnect();

    return snfMapSdkError(sdk_error);
}

/**
 * @brief 获取当前STA连接信息
 *
 * @param [out] info - STA连接信息.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterStaGetLinkInfo(SnfWifiLinkInfo *info)
{
    wifi_link_status_t sdk_status = {0};
    bk_err_t sdk_error;

    if (snfInitStateGet() != SNF_WIFI_ADAPTER_OK)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    if (info == NULL)
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    sdk_error = bk_wifi_sta_get_link_status(&sdk_status);
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    memset(info, 0, sizeof(*info));
    memcpy(info->ssid, sdk_status.ssid, sizeof(info->ssid) - 1U);
    info->ssid[sizeof(info->ssid) - 1U] = '\0';
    memcpy(info->bssid, sdk_status.bssid, sizeof(info->bssid));
    info->rssi = sdk_status.rssi;
    info->channel = sdk_status.channel;
    info->security = snfSecurityFromSdk(sdk_status.security);

    return SNF_WIFI_ADAPTER_OK;
}

/**
 * @brief 获取当前STA信号强度
 *
 * @return RSSI值, 单位dBm; 失败时返回负数错误码.
 */
int snfWifiAdapterStaGetRssi(int *rssi)
{
    SnfWifiLinkInfo info = {0};
    int adapter_error;

    if(rssi == NULL)
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    adapter_error = snfWifiAdapterStaGetLinkInfo(&info);
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        return adapter_error;
    }

    *rssi = info.rssi;

    return SNF_WIFI_ADAPTER_OK;
}

/**
 * @brief 启动异步WIFI扫描
 *
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterScan(void)
{
    SnfWifiAdapterState *adp_state = &adapter_state;
    bk_err_t sdk_error;

    if (snfInitStateGet() != SNF_WIFI_ADAPTER_OK)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    if (adp_state->scan_started != 0)
    {
        return SNF_WIFI_ADAPTER_ERR_BUSY;
    }

    sdk_error = bk_wifi_scan_start(NULL);
    if (sdk_error == BK_OK)
    {
        adp_state->scan_started = 1;
    }

    return snfMapSdkError(sdk_error);
}

/**
 * @brief 获取并转换最近一次WIFI扫描结果
 *
 * @param [out] results - 扫描结果数组, max_count 为0时可传入 NULL.
 * @param [in] max_count - results 可容纳的最大结果数.
 * @param [out] result_count - 实际写入 results 的结果数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterScanGetResults(SnfWifiLinkInfo *results, uint16_t max_count,
                                 uint16_t *result_count)
{
    wifi_scan_result_t sdk_result = {0};
    uint16_t copy_count = 0U;
    int index;
    bk_err_t sdk_error;

    if (results == NULL || max_count == 0 || result_count == NULL)
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }
    *result_count = 0;

    if (snfInitStateGet() != SNF_WIFI_ADAPTER_OK)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    sdk_error = bk_wifi_scan_get_result(&sdk_result);
    if (sdk_error != BK_OK)
    {
        return snfMapSdkError(sdk_error);
    }

    if (sdk_result.ap_num < 0 || sdk_result.aps == NULL)
    {
        bk_wifi_scan_free_result(&sdk_result);
        return SNF_WIFI_ADAPTER_ERR_INTERNAL;
    }

    if (sdk_result.ap_num < (int)max_count)
    {
        copy_count = (uint16_t)sdk_result.ap_num;
    }
    else
    {
        copy_count = max_count;
    }

    for (index = 0; index < copy_count; index++)
    {
        const wifi_scan_ap_info_t *sdk_ap = &sdk_result.aps[index];

        memset(&results[index], 0, sizeof(results[index]));
        memcpy(results[index].ssid, sdk_ap->ssid, sizeof(results[index].ssid) - 1U);
        results[index].ssid[sizeof(results[index].ssid) - 1U] = '\0';
        memcpy(results[index].bssid, sdk_ap->bssid, sizeof(results[index].bssid));
        results[index].rssi = sdk_ap->rssi;
        results[index].channel = sdk_ap->channel;
        results[index].security = snfSecurityFromSdk(sdk_ap->security);
    }

    *result_count = copy_count;
    bk_wifi_scan_free_result(&sdk_result);

    return SNF_WIFI_ADAPTER_OK;
}

/**
 * @brief 配置软件AP
 *
 * @param [in] config - AP配置参数.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterApSetConfig(const SnfWifiApConfig *config)
{
    netif_ip4_config_t ip_config = {0};
    wifi_ap_config_t sdk_config = {0};
    size_t ssid_length;
    size_t password_length;
    bk_err_t sdk_error;
    int adapter_error;

    adapter_error = snfInitStateGet();
    if (adapter_error != SNF_WIFI_ADAPTER_OK)
    {
        return adapter_error;
    }

    if ((config == NULL)
        || (snfValidateString(config->ssid, sizeof(config->ssid),
                              SNF_WIFI_SSID_MAX_LEN, &ssid_length) != 0)
        || (snfValidateString(config->password, sizeof(config->password),
                              SNF_WIFI_PASSWORD_MAX_LEN, &password_length) != 0)
        || (ssid_length == 0U)
        || (snfSecurityToSdk(config->security, &sdk_config.security) != 0))
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    if ((config->security == SNF_WIFI_SECURITY_OPEN) && (password_length != 0U))
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    if ((config->security != SNF_WIFI_SECURITY_OPEN)
        && ((password_length < 8U) || (password_length > SNF_WIFI_PASSWORD_MAX_LEN)))
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    strcpy(ip_config.ip,      SNF_DEFAULT_AP_IP_ADDR);
    strcpy(ip_config.mask,    SNF_DEFAULT_AP_NETMASK);
    strcpy(ip_config.gateway, SNF_DEFAULT_AP_GATEWAY);
    strcpy(ip_config.dns,     SNF_DEFAULT_AP_DNS);

    bk_netif_set_ip4_config(NETIF_IF_AP, &ip_config);

    memcpy(sdk_config.ssid, config->ssid, ssid_length + 1U);
    memcpy(sdk_config.password, config->password, password_length + 1U);
    sdk_config.channel = config->channel;
    sdk_config.max_con = config->max_connections;
    sdk_error = bk_wifi_ap_set_config(&sdk_config);

    return snfMapSdkError(sdk_error);
}

/**
 * @brief 注册应用事件回调
 *
 * @param [in] callback - WIFI适配模块事件回调.
 * @param [in] user_data - 传递给回调函数的用户数据.
 * @return WIFI适配模块状态码.
 */
int snfWifiAdapterRegisterEventCallback(SnfWifiAdapterEventCB callback, void *user_data)
{
    SnfWifiAdapterState *adp_state = &adapter_state;

    if (snfInitStateGet() != SNF_WIFI_ADAPTER_OK)
    {
        return SNF_WIFI_ADAPTER_ERR_NOT_INIT;
    }

    if (callback == NULL)
    {
        return SNF_WIFI_ADAPTER_ERR_INVALID_PARAM;
    }

    adp_state->event_callback = callback;
    adp_state->event_user_data = user_data;

    return SNF_WIFI_ADAPTER_OK;
}
