#include "stdint.h"
#include <components/system.h>
#include <driver/uart.h>

#include "bk_openthread.h"
#include "FreeRTOS.h"
#include "task.h"

#include "sonoff_log.h"
#include "sonoff_project_config.h"
#include "sonoff_main.h"
#include "sonoff_cli.h"
#include "sonoff_nvdm_config.h"
#include "sonoff_nvdm.h"
#include "sonoff_log.h"
#include "sonoff_private_factory.h"
#include "sonoff_private_device.h"

static const char *tag = "SNF-ENTRY";

#define SNF_FACTORY_REPLY_TIMEOUT_MS 3000

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
    uint32_t reset_reason = bk_misc_get_reset_reason();
    bk_get_mac(mac, MAC_TYPE_BASE);

    printf("\r\n");
    printf("###############################\r\n");
    printf("####     Sonoff Thread     ####\r\n");
    printf("###############################\r\n");
    printf("reset reason : %s\r\n", resetReasonToString(reset_reason));
    printf("       model : %s\r\n", SONOFF_DEVICE_MODEL);
    printf("         mac : %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("      hw_ver : %s\r\n", "1.1.1");
    printf("      fw_ver : %s\r\n", SONOFF_SOFTWARE_VERSION_STRING);
    printf("compile time : %s %s\r\n", __DATE__, __TIME__);
    printf("\r\n");
}

static void disableLocalConsoleRx(void)
{
    uart_id_t uart_id = (uart_id_t)bk_get_printf_port();
    bk_uart_disable_rx_interrupt(uart_id);
    bk_uart_disable_rx(uart_id);
}

void sonoffEntry(void)
{
    int ret = 0;
    int factory_mode_flag = 0;

    showSystemInfo();

    ret = snfNvdmInit();
    if (ret != 0)
    {
        LOG_I(tag, "nvdm init failed, ret=%d", ret);
    }

    ret = snfBaseMacApply();
    if (ret != 0)
    {
        LOG_I(tag, "base mac apply failed, ret=%d", ret);
    }

    ret = snfCliInit();
    if (ret != 0)
    {
        LOG_I(tag, "cli init failed, ret=%d", ret);
    }

    factory_mode_flag = (snfLicenseIsValid() != 0);

    if (factory_mode_flag == 0)
    {
        LOG_RAW("factory?\r\n");
        vTaskDelay(20 / portTICK_PERIOD_MS);
        LOG_RAW("factory?\r\n");
        vTaskDelay(20 / portTICK_PERIOD_MS);
        LOG_RAW("factory?\r\n");
        if (snfCliWaitFactoryReply(SNF_FACTORY_REPLY_TIMEOUT_MS) == 0)
        {
            factory_mode_flag = 1;
        }
    }

    if(factory_mode_flag == 1)
    {
        snfPrivateFactoryStart();
    }
    else
    {
        //disableLocalConsoleRx();
        snfMainInit();
        snfPrivateDeviceStart();

#if CONFIG_MATTER_START && CONFIG_SUPPORT_MATTER
        extern void ChipTest(void);
        ChipTest();
#endif
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        bk_openthread_init();
    }
}
