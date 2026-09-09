/**
 * @file    ota_core_test.c
 * @brief   OTA核心与流解析集成测试，Flash和任务调度使用主机替身
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include "../../sonoff/ota/sonoff_ota.c"

#define TEST_FLASH_SIZE (1492 * 1024)

static uint8_t test_flash[TEST_FLASH_SIZE];
static uint32_t test_written;
static uint32_t test_applied;
static uint32_t test_notified;
static uint32_t test_received;
static uint32_t test_total;
static uint32_t test_queue_count;
static uint8_t test_error;
static SnfOtaEvent test_event;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{

    return &test_event;
}

int32_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks)
{
    assert(mutex != NULL);

    return pdPASS;
}

int32_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex != NULL);

    return pdPASS;
}

QueueHandle_t xQueueCreate(uint32_t length, size_t size)
{
    assert(size == sizeof(test_event));
    test_queue_count = 0;

    return &test_event;
}

int32_t xQueueSend(QueueHandle_t queue, const void *event, TickType_t ticks)
{
    assert(queue != NULL);
    assert(test_queue_count == 0);
    memcpy(&test_event, event, sizeof(test_event));
    test_queue_count++;

    return pdPASS;
}

int32_t xQueueReceive(QueueHandle_t queue, void *event, TickType_t ticks)
{
    assert(queue != NULL);
    assert(test_queue_count == 1);
    memcpy(event, &test_event, sizeof(test_event));
    test_queue_count--;

    return pdPASS;
}

void vQueueDelete(QueueHandle_t queue)
{
    test_queue_count = 0;
}

int32_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack,
                    void *argument, uint32_t priority, TaskHandle_t *handle)
{
    *handle = &test_event;

    return pdPASS;
}

void vTaskDelay(TickType_t ticks)
{
}

void vTaskDelete(TaskHandle_t handle)
{
}

int32_t snfOtaAdapterGetSize(uint32_t *size)
{
    *size = TEST_FLASH_SIZE;

    return 0;
}

int snfOtaAdapterErase(uint32_t offset, uint32_t size)
{
    const char *failure = getenv("OTA_TEST_FAIL");

    if ((failure != NULL) && (strcmp(failure, "erase") == 0))
    {
        return -1;
    }

    assert((offset % 4096) == 0);
    assert(size == 4096);
    assert(offset + size <= sizeof(test_flash));
    memset(&test_flash[offset], 0xff, size);

    return 0;
}

int snfOtaAdapterWrite(uint32_t offset, const void *data, uint32_t size)
{
    const char *failure = getenv("OTA_TEST_FAIL");

    if ((failure != NULL) && (strcmp(failure, "write") == 0))
    {
        return -1;
    }

    assert(ota_info.state == SNF_OTA_STATE_RECEIVING);
    assert(offset == test_written);
    test_written += size;
    assert(size != 0);
    assert(offset + size <= sizeof(test_flash));
    memcpy(&test_flash[offset], data, size);

    return 0;
}

int snfOtaAdapterSetBootPartition(void)
{
    const char *failure = getenv("OTA_TEST_FAIL");

    if ((failure != NULL) && (strcmp(failure, "apply") == 0))
    {
        return -1;
    }

    test_applied++;

    return 0;
}

void snfOtaAdapterReboot(void)
{
}

/**
 * @brief 模拟接入通道的进度观察与校验成功后提交
 *
 * @param [in] state - 核心状态.
 * @param [in] data - 输入消费进度.
 */
static void testCallback(SnfOtaState state, const SnfOtaEventData *data)
{
    assert(data != NULL);
    assert(data->received_size >= test_received);
    assert(data->total_size == test_total);
    test_received = data->received_size;
    test_error = data->error_code;
    test_notified++;
    if (state == SNF_OTA_STATE_VERIFY_SUCCESS)
    {
        assert(test_received == test_total);
        assert(snfOtaApply() == SNF_OTA_OK);
    }
}

int main(int argc, char *argv[])
{
    SnfOtaConfig config = {0};
    SnfOtaEvent event = {0};
    uint32_t chunk;
    uint32_t size;
    uint32_t offset = 0;
    uint8_t *data;
    FILE *file;
    int32_t result;

    assert(argc == 7);
    chunk = (uint32_t)strtoul(argv[3], NULL, 10);
    assert(chunk > 0);
    test_total = (uint32_t)strtoul(argv[5], NULL, 10);
    file = fopen(argv[1], "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = (uint32_t)ftell(file);
    rewind(file);
    data = malloc(size);
    assert(data != NULL);
    assert(fread(data, 1, size, file) == size);
    fclose(file);

    config.format = (strcmp(argv[6], "raw") == 0) ? SNF_OTA_FORMAT_RAW : SNF_OTA_FORMAT_PACKAGE;
    config.image_info.size = test_total;
    config.event_callback = &testCallback;
    assert(strlen(argv[4]) <= SNF_OTA_FILE_NAME_SIZE);
    memcpy(config.file_name, argv[4], strlen(argv[4]));
    result = snfOtaStart(&config);
    if (result == SNF_OTA_OK)
    {
        assert(snfOtaStart(&config) == SNF_OTA_ERR_BUSY);
        assert(snfOtaWrite(1, data, 1) == SNF_OTA_ERR_INVALID_PARAM);
        assert(snfOtaApply() == SNF_OTA_ERR_STATE);
        while (offset < size)
        {
            uint32_t take = size - offset;
            uint32_t notified = test_notified;

            if (take > chunk)
            {
                take = chunk;
            }
            result = snfOtaWrite(offset, &data[offset], take);
            if (result == SNF_OTA_OK)
            {
                assert(xQueueReceive(ota_control.event_queue, &event, 0) == pdPASS);
                assert(event.id == SNF_OTA_EVT_IMAGE_DATA);
                result = otaHandleData(&event);
                assert(test_notified == notified + 1);
            }
            if (result != SNF_OTA_OK)
            {
                break;
            }
            offset += take;
            assert(test_received == offset);
        }

        if (ota_info.state == SNF_OTA_STATE_RECEIVING)
        {
            for (uint32_t i = 0; i < SNF_OTA_MAX_TIMEOUT_COUNT; i++)
            {
                otaHandleTimeout();
            }
        }
        if (ota_info.state == SNF_OTA_STATE_VERIFY_SUCCESS)
        {
            if (config.format == SNF_OTA_FORMAT_PACKAGE)
            {
                const SnfOtaParseContext *parser = &ota_control.parser;
                uint32_t selected = 0;
                uint32_t encoded_value;

                assert(parser->metadata.version == data[0]);
                assert(parser->metadata.file_count == data[1]);
                assert(memcmp(parser->metadata.model_version, &data[2], 8) == 0);
                assert(parser->metadata.model_version[8] == '\0');
                assert(parser->metadata.cipher_type == data[10]);
                assert(memcmp(parser->metadata.reserved, &data[11],
                              sizeof(parser->metadata.reserved)) == 0);
                memcpy(&encoded_value, &data[20], sizeof(encoded_value));
                assert(parser->metadata.crc == ntohl(encoded_value));
                for (uint32_t i = 0; i < data[1]; i++)
                {
                    uint32_t entry = 24 + 76 * i;

                    if (memcmp(&data[entry], parser->file.name, SNF_OTA_FILE_NAME_SIZE) == 0)
                    {
                        assert(parser->file.name[32] == '\0');
                        assert(memcmp(parser->file.version, &data[entry + 32], 16) == 0);
                        assert(parser->file.version[16] == '\0');
                        memcpy(&encoded_value, &data[entry + 48], sizeof(encoded_value));
                        assert(parser->file.offset == ntohl(encoded_value));
                        memcpy(&encoded_value, &data[entry + 52], sizeof(encoded_value));
                        assert(parser->file.size == ntohl(encoded_value));
                        memcpy(&encoded_value, &data[entry + 56], sizeof(encoded_value));
                        assert(parser->file.crc == ntohl(encoded_value));
                        memcpy(&encoded_value, &data[entry + 60], sizeof(encoded_value));
                        assert(parser->file.attributes_crc == ntohl(encoded_value));
                        assert(memcmp(parser->file.reserved, &data[entry + 64],
                                      sizeof(parser->file.reserved)) == 0);
                        selected++;
                    }
                }
                assert(selected == 1);
            }

            assert(xQueueReceive(ota_control.event_queue, &event, 0) == pdPASS);
            assert(event.id == SNF_OTA_EVT_IMAGE_APPLY);
            result = otaHandleApply();
            if (result != 0)
            {
                assert(test_applied == 0);
            }
        }
        result = (ota_info.state == SNF_OTA_STATE_SUCCESS) ? 0 : 1;

        /* 覆盖任务退出后的清理与再次启动，解析状态不得泄漏到下一次会话。 */
        vQueueDelete(ota_control.event_queue);
        ota_control.event_queue = NULL;
        otaClearSessionLocked();
        assert(snfOtaStart(&config) == SNF_OTA_OK);
        assert(ota_control.parser.offset == 0);
        assert(ota_control.parser.file.size == 0);
        assert(ota_info.received_size == 0);
    }

    file = fopen(argv[2], "wb");
    assert(file != NULL);
    assert(fwrite(test_flash, 1, test_written, file) == test_written);
    fclose(file);
    free(data);
    fprintf(stderr, "error=%u received=%lu wrote=%lu apply=%lu\n", test_error,
            (unsigned long)test_received, (unsigned long)test_written, (unsigned long)test_applied);

    return (result == 0) ? 0 : 1;
}
