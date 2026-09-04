/**
 * @file    sonoff_cli.c
 * @brief   Sonoff串口工具箱命令模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-28
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>
#include <components/system.h>

#include "cli.h"

#include "sonoff_cli.h"
#include "sonoff_common.h"
#include "sonoff_log.h"
#include "sonoff_net_test.h"
#include "sonoff_nvdm.h"
#include "sonoff_wifi.h"
#include "sonoff_project_config.h"
#include "sonoff_sha256.h"
#include "xf_lcd_nv3007.h"

static const char *tag = "SNF-CLI";

#define SNF_WIFI_CLI_AP_CHANNEL          1U
#define SNF_WIFI_CLI_AP_MAX_CONNECTIONS  4U
#define SNF_WIFI_CLI_SCAN_RESULT_MAX     10U
#define SNF_WIFI_CLI_SCAN_ONCE_MAX       15U
#define SNF_WIFI_CLI_SCAN_WAIT_MS        5000U
#define SNF_LCD_CLI_COLOR_DELAY_MS       500U

/** @brief vTaskList单行缓冲长度. */
#define SNF_CLI_TASK_LIST_LINE_SIZE      (configMAX_TASK_NAME_LEN + 18)

/** @brief 单次printf分段长度, 需小于平台__wrap_printf的127字节限制. */
#define SNF_CLI_PRINTF_CHUNK_SIZE        120

#define SNF_LCD_COLOR_BLACK              0x0000U
#define SNF_LCD_COLOR_BLUE               0x001fU
#define SNF_LCD_COLOR_GREEN              0x07e0U
#define SNF_LCD_COLOR_RED                0xf800U
#define SNF_LCD_COLOR_WHITE              0xffffU

#define AT_MASTER_CHIP_NAME              "BK723x"
#define AT_CMD_MASTER_CHIP_ID            "AT+MASTER_CHIP_ID"
#define AT_CMD_MASTER_CHIP_ID_QUERY      "AT+MASTER_CHIP_ID?"
#define AT_CMD_FW_VER                    "AT+FW_VER"
#define AT_CMD_FW_VER_QUERY              "AT+FW_VER?"
#define AT_CMD_MT_SERIAL_NUM             "AT+MT_SERIAL_NUM"
#define AT_CMD_MT_SERIAL_NUM_QUERY       "AT+MT_SERIAL_NUM?"
#define AT_CMD_MT_SERIAL_NUM_SET         "AT+MT_SERIAL_NUM_SET"
#define AT_CMD_MT_FACTORY_DATA_WRITE     "AT+MT_FACTORY_DATA_WRITE"
#define AT_MT_FACTORY_DATA_FIELD_NUM     10
#define AT_MT_FACTORY_DATA_WRITE_ARGC    12
#define AT_MT_FACTORY_DATA_SHA256_HEX_LEN 64

/**
 * @brief Sonoff工具箱子命令处理函数.
 */
typedef void (*SnfCliHandler)(int argc, char **argv);

/**
 * @brief Sonoff工具箱子命令表项.
 */
typedef struct
{
    const char *name;           /* 子命令名称 */
    const char *help;           /* 子命令说明 */
    SnfCliHandler handler;      /* 子命令处理函数 */
    void (*print_help)(void);   /* 子命令帮助打印函数 */
} SnfCliEntry;

/**
 * @brief Matter工厂数据单项写入函数.
 */
typedef int (*AtMatterItemSet)(const char *value);

/**
 * @brief 打印WIFI测试命令帮助.
 */
static void snfWifiCliPrintHelp(void)
{
    printf("sonoff wifi sta <ssid> <password>\r\n");
    printf("sonoff wifi ap <ssid> <password>\r\n");
    printf("sonoff wifi sta_disconnect\r\n");
    printf("sonoff wifi ap_stop\r\n");
    printf("sonoff wifi scan\r\n");
    printf("sonoff wifi scan_results\r\n");
    printf("sonoff wifi status\r\n");
    printf("sonoff wifi rssi\r\n");
}

/**
 * @brief 处理WIFI测试串口命令.
 *
 * @param [in] argc - 参数数量, argv[0]为wifi.
 * @param [in] argv - 参数列表.
 */
static void snfWifiCliCommand(int argc, char **argv)
{
    SnfWifiStaConfig sta_config = {0};
    SnfWifiApConfig ap_config = {0};
    uint16_t i;
    int rssi = 0;
    int ret = -1;
    uint8_t show_help = 0U;

    if (argc < 2)
    {
        show_help = 1U;
    }
    else if ((strcmp(argv[1], "sta") == 0) && (argc == 4))
    {
        strncpy(sta_config.ssid, argv[2], SNF_WIFI_SSID_MAX_LEN);
        strncpy(sta_config.password, argv[3], SNF_WIFI_PASSWORD_MAX_LEN);
        ret = snfWifiStaConnect(&sta_config);
    }
    else if ((strcmp(argv[1], "ap") == 0) && (argc == 4))
    {
        strncpy(ap_config.ssid, argv[2], SNF_WIFI_SSID_MAX_LEN);
        strncpy(ap_config.password, argv[3], SNF_WIFI_PASSWORD_MAX_LEN);
        ap_config.channel = SNF_WIFI_CLI_AP_CHANNEL;
        ap_config.max_connections = SNF_WIFI_CLI_AP_MAX_CONNECTIONS;
        ap_config.security = SNF_WIFI_SECURITY_WPA2;
        ret = snfWifiApStart(&ap_config);
    }
    else if ((strcmp(argv[1], "sta_disconnect") == 0) && (argc == 2))
    {
        ret = snfWifiStaDisconnect();
    }
    else if ((strcmp(argv[1], "ap_stop") == 0) && (argc == 2))
    {
        ret = snfWifiApStop();
    }
    else if ((strcmp(argv[1], "scan") == 0) && (argc == 2))
    {
        SnfWifiLinkInfo scan_once[SNF_WIFI_CLI_SCAN_ONCE_MAX] = {0};
        uint16_t result_count = 0;

        ret = snfWifiScan();
        vTaskDelay(SNF_WIFI_CLI_SCAN_WAIT_MS / portTICK_PERIOD_MS);
        snfWifiScanGetResults(scan_once, SNF_WIFI_CLI_SCAN_ONCE_MAX, &result_count);
        for (i = 0; i < result_count; i++)
        {
            LOG_I(tag,
                  "scan result: %s, rssi=%d dBm, channel=%d, security=%d",
                  scan_once[i].ssid,
                  scan_once[i].rssi,
                  scan_once[i].channel,
                  scan_once[i].security);
        }
    }
    else if ((strcmp(argv[1], "scan_results") == 0) && (argc == 2))
    {
        SnfWifiLinkInfo scan_results[SNF_WIFI_CLI_SCAN_RESULT_MAX] = {0};
        uint16_t result_count = 0;

        ret = snfWifiScanGetResults(scan_results,
                                    SNF_WIFI_CLI_SCAN_RESULT_MAX,
                                    &result_count);
        if (ret == 0)
        {
            LOG_I(tag, "scan result count: %d\r\n", result_count);
            for (i = 0; i < result_count; i++)
            {
                LOG_I(tag, "[%d] ssid=%s, rssi=%d, channel=%d\r\n",
                      i,
                      scan_results[i].ssid,
                      scan_results[i].rssi,
                      scan_results[i].channel);
            }
        }
    }
    else if ((strcmp(argv[1], "status") == 0) && (argc == 2))
    {
        printf("link=%d, scan=%d\r\n",
               snfWifiGetLinkStatus(),
               snfWifiScanStatus());
        ret = 0;
    }
    else if ((strcmp(argv[1], "rssi") == 0) && (argc == 2))
    {
        ret = snfWifiStaGetRssi(&rssi);
        if (ret == 0)
        {
            printf("rssi=%d dBm\r\n", rssi);
        }
    }
    else
    {
        show_help = 1U;
    }

    if (show_help != 0U)
    {
        snfWifiCliPrintHelp();
    }
    else
    {
        printf("sonoff wifi ret=%d\r\n", ret);
    }
}

/**
 * @brief 打印网络测试命令帮助.
 */
static void snfNetCliPrintHelp(void)
{
    printf("sonoff net wifi_tcp\r\n");
    printf("sonoff net wifi_udp\r\n");
    printf("sonoff net thread\r\n");
}

/**
 * @brief 处理网络测试串口命令.
 *
 * @param [in] argc - 参数数量, argv[0]为net.
 * @param [in] argv - 参数列表.
 */
static void snfNetCliCommand(int argc, char **argv)
{
    int ret = -1;
    uint8_t show_help = 0U;

    if (argc < 2)
    {
        show_help = 1U;
    }
    else if ((strcmp(argv[1], "wifi_tcp") == 0) && (argc == 2))
    {
        ret = snfNetTestWifiTcpInit();
    }
    else if ((strcmp(argv[1], "wifi_udp") == 0) && (argc == 2))
    {
        ret = snfNetTestWifiUdpInit();
    }
    else if ((strcmp(argv[1], "thread") == 0) && (argc == 2))
    {
        ret = snfNetTestThreadInit();
    }
    else
    {
        show_help = 1U;
    }

    if (show_help != 0U)
    {
        snfNetCliPrintHelp();
    }
    else
    {
        printf("sonoff net ret=%d\r\n", ret);
    }
}

/**
 * @brief 打印LCD测试命令帮助.
 */
static void snfLcdCliPrintHelp(void)
{
    printf("sonoff lcd\r\n");
    printf("sonoff lcd bl <on|off>\r\n");
}

/**
 * @brief 处理LCD测试串口命令.
 *
 * @param [in] argc - 参数数量, argv[0]为lcd.
 * @param [in] argv - 参数列表.
 */
static void snfLcdCliCommand(int argc, char **argv)
{
    int ret = -1;

    if (argc == 1)
    {
        ret = xf_lcd_init();
        if (ret == 0)
        {
            xf_lcd_full_color(SNF_LCD_COLOR_BLACK);
            xf_lcd_bl_output(1U);

            xf_lcd_full_color(SNF_LCD_COLOR_RED);
            vTaskDelay(pdMS_TO_TICKS(SNF_LCD_CLI_COLOR_DELAY_MS));
            xf_lcd_full_color(SNF_LCD_COLOR_GREEN);
            vTaskDelay(pdMS_TO_TICKS(SNF_LCD_CLI_COLOR_DELAY_MS));
            xf_lcd_full_color(SNF_LCD_COLOR_BLUE);
            vTaskDelay(pdMS_TO_TICKS(SNF_LCD_CLI_COLOR_DELAY_MS));
            xf_lcd_full_color(SNF_LCD_COLOR_WHITE);
        }

        printf("sonoff lcd ret=%d\r\n", ret);
    }
    else if ((argc == 3) && (strcmp(argv[1], "bl") == 0))
    {
        if (strcmp(argv[2], "on") == 0)
        {
            xf_lcd_bl_output(1U);
            ret = 0;
        }
        else if (strcmp(argv[2], "off") == 0)
        {
            xf_lcd_bl_output(0U);
            ret = 0;
        }
        else
        {
            ret = -1;
        }

        if (ret == 0)
        {
            printf("sonoff lcd backlight %s ret=%d\r\n", argv[2], ret);
        }
        else
        {
            snfLcdCliPrintHelp();
        }
    }
    else
    {
        snfLcdCliPrintHelp();
    }
}

/**
 * @brief 打印堆内存查询命令帮助.
 */
static void memCliPrintHelp(void)
{
    printf("sonoff mem\r\n");
}

/**
 * @brief 处理堆内存查询串口命令.
 *
 * @param [in] argc - 参数数量, argv[0]为mem.
 * @param [in] argv - 参数列表.
 */
static void memCliCommand(int argc, char **argv)
{
    uint32_t heap0_size;
    uint32_t heap1_size;
    uint32_t total_size;

    if ((argc != 1) || (argv == NULL))
    {
        memCliPrintHelp();
        return;
    }

    heap0_size = (uint32_t)prvHeapGetTotalSize();
    heap1_size = (uint32_t)xPortGetPsramTotalHeapSize();
    total_size = heap0_size + heap1_size;

    printf("\n\rTotalHeapSize:%u(%u+%u)",
           (unsigned int)total_size,
           (unsigned int)heap0_size,
           (unsigned int)heap1_size);
    printf("\n\rFreeHeapSize: %u", (unsigned int)xPortGetFreeHeapSize());
    printf("\n\rMinimumEverFreeHeapSize:  %u",
           (unsigned int)xPortGetMinimumEverFreeHeapSize());
}

/**
 * @brief 打印任务列表命令帮助.
 */
static void taskCliPrintHelp(void)
{
    printf("sonoff task\r\n");
}

/**
 * @brief 按行分段打印字符串, 避免一次printf超过平台截断长度.
 *
 * @param [in,out] text - 待打印缓冲区, 打印过程中会临时改写再恢复.
 */
static void printLines(char *text)
{
    uint32_t start;
    uint32_t end;
    uint32_t chunk_end;
    uint32_t i;
    char saved;

    if (text == NULL)
    {
        return;
    }

    start = 0;
    while (text[start] != '\0')
    {
        end = start;
        while ((text[end] != '\0') && (text[end] != '\n') && (text[end] != '\r'))
        {
            end++;
        }

        if (end == start)
        {
            printf("\n\r");
        }
        else
        {
            i = start;
            while (i < end)
            {
                chunk_end = i + SNF_CLI_PRINTF_CHUNK_SIZE;
                if (chunk_end > end)
                {
                    chunk_end = end;
                }

                saved = text[chunk_end];
                text[chunk_end] = '\0';
                printf("\n\r%s", &text[i]);
                text[chunk_end] = saved;
                i = chunk_end;
            }
        }

        while ((text[end] == '\r') || (text[end] == '\n'))
        {
            end++;
        }

        start = end;
    }
}

/**
 * @brief 处理任务列表查询串口命令.
 *
 * @param [in] argc - 参数数量, argv[0]为task.
 * @param [in] argv - 参数列表.
 */
static void taskCliCommand(int argc, char **argv)
{
    char *task_list_buf;
    uint32_t buf_size;

    if ((argc != 1) || (argv == NULL))
    {
        taskCliPrintHelp();
        return;
    }

    buf_size = ((uint32_t)uxTaskGetNumberOfTasks() * SNF_CLI_TASK_LIST_LINE_SIZE) + 1;
    task_list_buf = (char *)pvPortMalloc(buf_size);
    if (task_list_buf == NULL)
    {
        printf("\n\rmemory malloced failed.");
        return;
    }

    printf("\n\rtask info:");
    printf("\n\rname            | status | prio | stack | id | tcb");
    vTaskList(task_list_buf);
    printLines(task_list_buf);
    vPortFree(task_list_buf);
}

/**
 * @brief 打印NVDM测试命令帮助.
 */
static void snfNvdmCliPrintHelp(void)
{
    printf("sonoff nvdm show\r\n");
    printf("sonoff nvdm read <group> <key>\r\n");
    printf("sonoff nvdm write <group> <key> <value>\r\n");
    printf("sonoff nvdm clean\r\n");
}

/**
 * @brief 处理NVDM测试串口命令.
 *
 * @param [in] argc - 参数数量, argv[0]为nvdm.
 * @param [in] argv - 参数列表.
 */
static void snfNvdmCliCommand(int argc, char **argv)
{
    int ret = -1;
    uint8_t show_help = 0;

    if (argc < 2)
    {
        show_help = 1;
    }
    else if ((strcmp(argv[1], "show") == 0) && (argc == 2))
    {
        ret = snfNvdmShow();
    }
    else if ((strcmp(argv[1], "read") == 0) && (argc == 4))
    {
        snfNvdmCliReadItem(argv[2], argv[3]);
        ret = 0;
    }
    else if ((strcmp(argv[1], "write") == 0) && (argc == 5))
    {
        snfNvdmCliWriteItem(argv[2], argv[3], argv[4]);
        ret = 0;
    }
    else if ((strcmp(argv[1], "clean") == 0) && (argc == 2))
    {
        ret = snfNvdmCleanUserGroup();
    }
    else
    {
        show_help = 1;
    }

    if (show_help != 0)
    {
        snfNvdmCliPrintHelp();
    }
    else
    {
        printf("sonoff nvdm ret=%d\r\n", ret);
    }
}

static const SnfCliEntry snf_cli_command_table[] = {
    {"wifi", "WIFI test commands", &snfWifiCliCommand, &snfWifiCliPrintHelp},
    {"net", "network test commands", &snfNetCliCommand, &snfNetCliPrintHelp},
    {"lcd", "LCD test commands", &snfLcdCliCommand, &snfLcdCliPrintHelp},
    {"nvdm", "NVDM test commands", &snfNvdmCliCommand, &snfNvdmCliPrintHelp},
    {"mem", "show heap memory", &memCliCommand, &memCliPrintHelp},
    {"task", "show task list", &taskCliCommand, &taskCliPrintHelp},
};

#define SNF_CLI_COMMAND_COUNT (sizeof(snf_cli_command_table) / sizeof(snf_cli_command_table[0]))

/**
 * @brief 打印Sonoff工具箱全部命令.
 */
static void snfCliPrintHelp(void)
{
    uint16_t i;

    printf("Usage: sonoff <command> [args]\r\n");
    printf("Commands:\r\n");
    for (i = 0U; i < SNF_CLI_COMMAND_COUNT; i++)
    {
        printf("  %-8s %s\r\n",
               snf_cli_command_table[i].name,
               snf_cli_command_table[i].help);
    }

    printf("\r\n");
    for (i = 0U; i < SNF_CLI_COMMAND_COUNT; i++)
    {
        if (snf_cli_command_table[i].print_help != NULL)
        {
            snf_cli_command_table[i].print_help();
        }
    }
}

/**
 * @brief 处理Sonoff工具箱串口命令.
 *
 * @param [in] pcWriteBuffer - CLI输出缓冲区.
 * @param [in] xWriteBufferLen - CLI输出缓冲区长度.
 * @param [in] argc - 参数数量.
 * @param [in] argv - 参数列表.
 */
static void snfCliCommand(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint16_t i;
    uint8_t handled = 0U;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc >= 2)
    {
        for (i = 0U; i < SNF_CLI_COMMAND_COUNT; i++)
        {
            if (strcmp(argv[1], snf_cli_command_table[i].name) == 0)
            {
                snf_cli_command_table[i].handler((argc - 1), &argv[1]);
                handled = 1U;
                break;
            }
        }
    }

    if (handled == 0U)
    {
        snfCliPrintHelp();
    }
}

/**
 * @brief 打印AT命令错误响应.
 *
 * @param [in] cmd - AT命令名, 不含问号.
 */
static void atPrintError(const char *cmd)
{
    printf("%s=ERROR\r\n", cmd);
}

/**
 * @brief 打印AT命令成功响应.
 *
 * @param [in] cmd - AT命令名, 不含问号.
 * @param [in] value - 响应值.
 */
static void atPrintValue(const char *cmd, const char *value)
{
    printf("%s=%s\r\n", cmd, value);
}

/**
 * @brief 读取主芯片唯一标识.
 *
 * @param [out] uid - 唯一标识缓冲区.
 * @param [in] uid_size - 缓冲区长度.
 * @param [out] uid_len - 实际有效字节数.
 * @return 0表示成功, 负数表示失败.
 */
static int atGetMasterChipId(uint8_t *uid, uint16_t uid_size, uint16_t *uid_len)
{
    if ((uid == NULL) || (uid_len == NULL) || (uid_size < BK_MAC_ADDR_LEN))
    {
        return -1;
    }

    memset(uid, 0, uid_size);
    if (bk_get_mac(uid, MAC_TYPE_BASE) != BK_OK)
    {
        return -1;
    }

    if (BK_IS_ZERO_MAC(uid))
    {
        return -1;
    }

    *uid_len = BK_MAC_ADDR_LEN;

    return 0;
}

/**
 * @brief 处理AT+MASTER_CHIP_ID?查询命令.
 *
 * @param [in] pcWriteBuffer - CLI输出缓冲区.
 * @param [in] xWriteBufferLen - CLI输出缓冲区长度.
 * @param [in] argc - 参数数量.
 * @param [in] argv - 参数列表.
 */
static void atMasterChipIdCommand(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint8_t uid[BUFF_SIZE_32];
    char hex[BUFF_SIZE_32 * 2 + 1];
    char value[BUFF_SIZE_128];
    uint16_t uid_len;
    uint16_t i;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if ((argc < 1) || (argv == NULL) || (argv[0] == NULL))
    {
        atPrintError(AT_CMD_MASTER_CHIP_ID);
        return;
    }

    if (atGetMasterChipId(uid, sizeof(uid), &uid_len) != 0)
    {
        LOG_E(tag, "get master chip id failed");
        atPrintError(AT_CMD_MASTER_CHIP_ID);
        return;
    }

    for (i = 0; i < uid_len; i++)
    {
        snprintf(&hex[i * 2], 3, "%02X", (unsigned int)uid[i]);
    }

    snprintf(value, sizeof(value), "%s-%s", AT_MASTER_CHIP_NAME, hex);
    atPrintValue(AT_CMD_MASTER_CHIP_ID, value);
}

/**
 * @brief 处理AT+FW_VER?查询命令.
 *
 * @param [in] pcWriteBuffer - CLI输出缓冲区.
 * @param [in] xWriteBufferLen - CLI输出缓冲区长度.
 * @param [in] argc - 参数数量.
 * @param [in] argv - 参数列表.
 */
static void atFwVerCommand(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    char value[BUFF_SIZE_128];
    int ret;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if ((argc < 1) || (argv == NULL) || (argv[0] == NULL))
    {
        atPrintError(AT_CMD_FW_VER);
        return;
    }

    ret = snprintf(value,
                   sizeof(value),
                   "FW%s-%s-%s-%s-v%s",
                   SONOFF_DEVICE_CLASS,
                   SONOFF_DEVICE_SERIAL_NUMBER,
                   SONOFF_DEVICE_FUNCTION,
                   SONOFF_DEVICE_CHIP,
                   SONOFF_SOFTWARE_VERSION_STRING);
    if ((ret < 0) || (ret >= (int)sizeof(value)))
    {
        atPrintError(AT_CMD_FW_VER);
        return;
    }

    atPrintValue(AT_CMD_FW_VER, value);
}

/**
 * @brief 处理AT+MT_SERIAL_NUM?查询命令.
 *
 * @param [in] pcWriteBuffer - CLI输出缓冲区.
 * @param [in] xWriteBufferLen - CLI输出缓冲区长度.
 * @param [in] argc - 参数数量.
 * @param [in] argv - 参数列表.
 */
static void atMtSerialNumQueryCommand(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    char serial_number[NVDM_FACTORY_SERIAL_NUMBER_LEN + 1];

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if ((argc < 1) || (argv == NULL) || (argv[0] == NULL))
    {
        atPrintError(AT_CMD_MT_SERIAL_NUM);
        return;
    }

    if (snfSerialNumberGet(serial_number, sizeof(serial_number)) != 0)
    {
        atPrintError(AT_CMD_MT_SERIAL_NUM);
        return;
    }

    atPrintValue(AT_CMD_MT_SERIAL_NUM, serial_number);
}

/**
 * @brief 处理AT+MT_SERIAL_NUM_SET设置命令.
 *
 * @param [in] pcWriteBuffer - CLI输出缓冲区.
 * @param [in] xWriteBufferLen - CLI输出缓冲区长度.
 * @param [in] argc - 参数数量.
 * @param [in] argv - 参数列表.
 */
static void atMtSerialNumSetCommand(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if ((argc != 2) || (argv == NULL) || (argv[1] == NULL))
    {
        atPrintError(AT_CMD_MT_SERIAL_NUM_SET);
        return;
    }

    if (snfSerialNumberSet(argv[1]) != 0)
    {
        atPrintError(AT_CMD_MT_SERIAL_NUM_SET);
        return;
    }

    atPrintValue(AT_CMD_MT_SERIAL_NUM_SET, "OK");
}

/**
 * @brief 将十六进制字符转换为半字节.
 *
 * @param [in] ch - 十六进制字符.
 * @param [out] nibble - 半字节值.
 * @return 1表示转换成功, 0表示字符非法.
 */
static int atHexNibble(char ch, uint8_t *nibble)
{
    if (nibble == NULL)
    {
        return 0;
    }

    if ((ch >= '0') && (ch <= '9'))
    {
        *nibble = (uint8_t)(ch - '0');
        return 1;
    }

    if ((ch >= 'A') && (ch <= 'F'))
    {
        *nibble = (uint8_t)((ch - 'A') + 10);
        return 1;
    }

    if ((ch >= 'a') && (ch <= 'f'))
    {
        *nibble = (uint8_t)((ch - 'a') + 10);
        return 1;
    }

    return 0;
}

/**
 * @brief 解码64位十六进制SHA256摘要.
 *
 * @param [in] hex - 64位十六进制字符串.
 * @param [out] digest - 32字节摘要缓冲区.
 * @return 1表示解码成功, 0表示非法.
 */
static int atSha256HexDecode(const char *hex, uint8_t *digest)
{
    uint16_t i;
    uint8_t high;
    uint8_t low;

    if ((hex == NULL) || (digest == NULL))
    {
        return 0;
    }

    if (strlen(hex) != AT_MT_FACTORY_DATA_SHA256_HEX_LEN)
    {
        return 0;
    }

    for (i = 0; i < SNF_SHA256_DIGEST_SIZE; i++)
    {
        if (atHexNibble(hex[i * 2], &high) == 0)
        {
            return 0;
        }

        if (atHexNibble(hex[(i * 2) + 1], &low) == 0)
        {
            return 0;
        }

        digest[i] = (uint8_t)((high << 4) | low);
    }

    return 1;
}

/**
 * @brief 校验factory_data含末尾逗号的SHA256摘要.
 *
 * @param [in] fields - 10个工厂数据字段.
 * @param [in] sha256_hex - 64位十六进制摘要.
 * @return 0表示匹配, 负数表示计算失败或不匹配.
 */
static int atFactoryDataSha256Verify(const char * const *fields, const char *sha256_hex)
{
    SnfSha256Ctx ctx;
    uint8_t digest[SNF_SHA256_DIGEST_SIZE];
    uint8_t expected[SNF_SHA256_DIGEST_SIZE];
    uint16_t i;
    int ret;

    if ((fields == NULL) || (sha256_hex == NULL))
    {
        return -1;
    }

    if (atSha256HexDecode(sha256_hex, expected) == 0)
    {
        return -1;
    }

    ret = snfSha256Init(&ctx);
    if (ret != SNF_SHA256_OK)
    {
        return -1;
    }

    for (i = 0; i < AT_MT_FACTORY_DATA_FIELD_NUM; i++)
    {
        if (fields[i] == NULL)
        {
            snfSha256Free(&ctx);
            return -1;
        }

        ret = snfSha256Update(&ctx, (const uint8_t *)fields[i], (uint32_t)strlen(fields[i]));
        if (ret != SNF_SHA256_OK)
        {
            snfSha256Free(&ctx);
            return -1;
        }

        ret = snfSha256Update(&ctx, (const uint8_t *)",", 1);
        if (ret != SNF_SHA256_OK)
        {
            snfSha256Free(&ctx);
            return -1;
        }
    }

    ret = snfSha256Finish(&ctx, digest, sizeof(digest));
    if (ret != SNF_SHA256_OK)
    {
        return -1;
    }

    if (memcmp(digest, expected, SNF_SHA256_DIGEST_SIZE) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 处理AT+MT_FACTORY_DATA_WRITE写入命令.
 *
 * @param [in] pcWriteBuffer - CLI输出缓冲区.
 * @param [in] xWriteBufferLen - CLI输出缓冲区长度.
 * @param [in] argc - 参数数量.
 * @param [in] argv - 参数列表.
 */
static void atMtFactoryDataWriteCommand(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    static const AtMatterItemSet item_set[AT_MT_FACTORY_DATA_FIELD_NUM] = {
        snfMatterDiscriminatorSet,
        snfMatterIterationCountSet,
        snfMatterSaltSet,
        snfMatterVerifierSet,
        snfMatterVendorIdSet,
        snfMatterVendorNameSet,
        snfMatterProductIdSet,
        snfMatterProductNameSet,
        snfMatterRdIdUidSet,
        snfMatterPasscodeSet,
    };
    const char *fields[AT_MT_FACTORY_DATA_FIELD_NUM];
    uint16_t i;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if ((argc != AT_MT_FACTORY_DATA_WRITE_ARGC) || (argv == NULL))
    {
        atPrintError(AT_CMD_MT_FACTORY_DATA_WRITE);
        return;
    }

    for (i = 1; i < AT_MT_FACTORY_DATA_WRITE_ARGC; i++)
    {
        if (argv[i] == NULL)
        {
            atPrintError(AT_CMD_MT_FACTORY_DATA_WRITE);
            return;
        }
    }

    for (i = 0; i < AT_MT_FACTORY_DATA_FIELD_NUM; i++)
    {
        fields[i] = argv[i + 1];
    }

    if (atFactoryDataSha256Verify(fields, argv[11]) != 0)
    {
        LOG_E(tag, "matter factory data sha256 mismatch");
        atPrintError(AT_CMD_MT_FACTORY_DATA_WRITE);
        return;
    }

    for (i = 0; i < AT_MT_FACTORY_DATA_FIELD_NUM; i++)
    {
        if (item_set[i](fields[i]) != 0)
        {
            atPrintError(AT_CMD_MT_FACTORY_DATA_WRITE);
            return;
        }
    }

    atPrintValue(AT_CMD_MT_FACTORY_DATA_WRITE, "OK");
}

/**
 * @brief AT指令表. 每条使用完整命令名, 因为平台CLI按argv[0]精确匹配.
 */
static const struct cli_command snfAtCliCommands[] = {
    {AT_CMD_MASTER_CHIP_ID_QUERY, "query master chip unique id", atMasterChipIdCommand},
    {AT_CMD_FW_VER_QUERY, "query firmware version", atFwVerCommand},
    {AT_CMD_MT_SERIAL_NUM_QUERY, "query product serial number", atMtSerialNumQueryCommand},
    {AT_CMD_MT_SERIAL_NUM_SET, "set product serial number", atMtSerialNumSetCommand},
    {AT_CMD_MT_FACTORY_DATA_WRITE, "write matter factory data", atMtFactoryDataWriteCommand},
};

#define SNF_AT_COMMAND_COUNT (sizeof(snfAtCliCommands) / sizeof(snfAtCliCommands[0]))

/**
 * @brief 处理AT帮助命令.
 *
 * @param [in] pcWriteBuffer - CLI输出缓冲区.
 * @param [in] xWriteBufferLen - CLI输出缓冲区长度.
 * @param [in] argc - 参数数量.
 * @param [in] argv - 参数列表.
 */
static void atHelpCommand(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint16_t i;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;
    (void)argc;
    (void)argv;

    for (i = 0; i < SNF_AT_COMMAND_COUNT; i++)
    {
        printf("%s\r\n", snfAtCliCommands[i].name);
    }
}

/**
 * @brief Sonoff工具箱串口命令表.
 */
static const struct cli_command snfCliCommands[] = {
    {"sonoff", "sonoff <command> [args]", snfCliCommand},
    {"AT", "AT command", atHelpCommand},
};

int snfCliInit(void)
{
    int ret;

    ret = cli_register_commands(snfCliCommands,
                                sizeof(snfCliCommands) / sizeof(snfCliCommands[0]));
    if (ret != 0)
    {
        return ret;
    }

    ret = cli_register_commands(snfAtCliCommands, (int)SNF_AT_COMMAND_COUNT);

    return ret;
}
