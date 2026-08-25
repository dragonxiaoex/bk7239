#include "stdint.h"
#include <components/system.h>

#include "sonoff_log.h"
#include "sonoff_project_config.h"
#include "sonoff_wifi.h"

static const char *resetReasonToString(RESET_SOURCE_STATUS reason)
{
    switch (reason)
    {
        case RESET_SOURCE_POWERON: return "poweron";
        case RESET_SOURCE_REBOOT: return "reboot";
        case RESET_SOURCE_WATCHDOG: return "watchdog";
        case RESET_SOURCE_NMI_WDT: return "nmi_wdt";
        case RESET_SOURCE_HARD_FAULT: return "hard_fault";
        case RESET_SOURCE_MPU_FAULT: return "mpu_fault";
        case RESET_SOURCE_BUS_FAULT: return "bus_fault";
        case RESET_SOURCE_USAGE_FAULT: return "usage_fault";
        case RESET_SOURCE_SECURE_FAULT: return "secure_fault";
        case RESET_SOURCE_DEBUG_MONITOR_FAULT: return "debug_monitor_fault";
        case RESET_SOURCE_DEFAULT_EXCEPTION: return "default_exception";
        case RESET_SOURCE_OTA_REBOOT: return "ota_reboot";
        case RESET_SOURCE_BROWN_OUT: return "brown_out";
        case RESET_SOURCE_BOOTLOADER_REBOOT: return "bootloader_reboot";
        case RESET_SOURCE_BOOTLOADER_NMI_WDT: return "bootloader_nmi_wdt";
        case RESET_SOURCE_BOOTLOADER_MPU_FAULT: return "bootloader_mpu_fault";
        case RESET_SOURCE_BOOTLOADER_BUS_FAULT: return "bootloader_bus_fault";
        case RESET_SOURCE_BOOTLOADER_HARD_FAULT: return "bootloader_hard_fault";
        case RESET_SOURCE_BOOTLOADER_UNKNOWN: return "bootloader_unknown";
        default: return "unknown";
    }
}

static void showSystemInfo(void)
{
    uint8_t mac[6];
    bk_get_mac(mac, MAC_TYPE_BASE);

    printf("\r\n");
    printf("###############################\r\n");
    printf("####     Sonoff Thread     ####\r\n");
    printf("###############################\r\n");
    printf("reset reason : %s\r\n", resetReasonToString(bk_misc_get_reset_reason()));
    printf("       model : %s\r\n", SONOFF_DEVICE_MODEL);
    printf("         mac : %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("      hw_ver : ---\r\n");
    printf("      fw_ver : ---\r\n");
    printf("compile time : %s %s\r\n", __DATE__, __TIME__);
    printf("\r\n");
}

void sonoffEntry(void)
{
    showSystemInfo();
    //snfWifiInit();
    snfWifiTest();
}