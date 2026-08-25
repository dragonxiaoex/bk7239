#ifndef __SONOFF_WIFI_H__
#define __SONOFF_WIFI_H__

#include "sonoff_wifi_adapter.h"

typedef enum {
    SNF_WIFI_IDLE,
    SNF_WIFI_SCANNING,
    SNF_WIFI_CONNECTING,
    SNF_WIFI_CONNECTED,
    SNF_WIFI_DISCONNECTED,
} SnfWifiLinkState;

typedef enum {
    SNF_WIFI_EVT_SET_TO_AP = 0,
    SNF_WIFI_EVT_SET_TO_STA,
    SNF_WIFI_EVT_SET_TO_IDLE,
    SNF_WIFI_EVT_DISCONNECT,
    SNF_WIFI_EVT_SCAN,
} SnfWifiEventId;

void snfWifiTest(void);
int snfWifiEventSend(int id, void *data);
int snfWifiInit(void);

#endif /* __SONOFF_WIFI_H__ */
