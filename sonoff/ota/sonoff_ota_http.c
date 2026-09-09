/**
 * @file    sonoff_ota_http.c
 * @brief   OTA升级HTTP适配实现
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-27
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include <lwip/sockets.h>
#include <task.h>

#include "sonoff_log.h"
#include "sonoff_ota.h"
#include "sonoff_ota_http.h"

static const char *tag = "SNF-OTA-HTTP";

#define SNF_OTA_HTTP_UPLOAD_PATH            "/cgi/ota_upload" /* OTA上传请求路径 */
#define SNF_OTA_HTTP_HEADER_BUFFER_SIZE     (1024)            /* HTTP请求头缓存大小 */
#define SNF_OTA_HTTP_DATA_BUFFER_SIZE       (1024)            /* HTTP数据缓存大小 */
#define SNF_OTA_HTTP_WAIT_TIMEOUT_MS        (30000)           /* OTA处理等待超时 */
#define SNF_OTA_HTTP_WAIT_INTERVAL_MS       (5)               /* OTA状态轮询间隔 */
#define SNF_OTA_HTTP_HEADER_END_SIZE        (4)               /* 请求头结束标记长度 */

/** @brief OTA HTTP运行状态. */
typedef struct
{
    volatile SnfOtaState state;
    volatile uint32_t received_size;
    volatile uint8_t percent;
    volatile uint8_t error_code;
} SnfHttpOtaState;

/** @brief HTTP OTA运行状态实例. */
static SnfHttpOtaState http_ota_state = {
    .state = SNF_OTA_STATE_IDLE,
    .received_size = 0,
    .percent = 0,
    .error_code = SNF_OTA_ERROR_NONE,
};

/** @brief HTTP OTA单包收包缓存, 写完flash后再收下一包. */
static uint8_t ota_http_data_buffer[SNF_OTA_HTTP_DATA_BUFFER_SIZE];

/**
 * @brief 查找HTTP请求头结束位置.
 *
 * @param [in] data - HTTP请求数据.
 * @param [in] data_size - 数据长度.
 * @return 请求头结束位置之后的偏移量, 0表示未找到.
 */
static size_t otaHttpFindHeaderEnd(const char *data, size_t data_size)
{
    static const char header_end[] = "\r\n\r\n";
    size_t index;

    if (data == NULL)
    {
        return 0;
    }

    for (index = 0;
         (index + sizeof(header_end) - 1) <= data_size;
         index++)
    {
        if (memcmp(data + index, header_end, sizeof(header_end) - 1) == 0)
        {
            return index + sizeof(header_end) - 1;
        }
    }

    return 0;
}

/**
 * @brief 判断ASCII字符是否为空白字符.
 *
 * @param [in] value - 待判断字符.
 * @return 1表示是空格或制表符, 0表示不是.
 */
static int otaHttpIsSpace(char value)
{

    return ((value == ' ') || (value == '\t')) ? 1 : 0;
}

/**
 * @brief 比较不区分大小写的ASCII字符串片段.
 *
 * @param [in] left - 左侧字符串.
 * @param [in] right - 右侧字符串.
 * @param [in] length - 比较长度.
 * @return 1表示相等, 0表示不相等.
 */
static int otaHttpAsciiEqual(const char *left, const char *right, size_t length)
{
    size_t index;

    for (index = 0; index < length; index++)
    {
        char left_char = left[index];
        char right_char = right[index];

        if ((left_char >= 'A') && (left_char <= 'Z'))
        {
            left_char = (char)(left_char - 'A' + 'a');
        }

        if ((right_char >= 'A') && (right_char <= 'Z'))
        {
            right_char = (char)(right_char - 'A' + 'a');
        }

        if (left_char != right_char)
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 解析HTTP头字段中的十进制整数.
 *
 * @param [in] value - 字段值.
 * @param [in] value_size - 字段值长度.
 * @param [out] result - 解析结果.
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpParseUint32(const char *value,
                              size_t value_size,
                              uint32_t *result)
{
    size_t start = 0;
    size_t end = value_size;
    size_t index;
    uint32_t number = 0;

    if ((value == NULL) || (result == NULL))
    {
        return -1;
    }

    while ((start < end) && (otaHttpIsSpace(value[start]) != 0))
    {
        start++;
    }

    while ((end > start) && (otaHttpIsSpace(value[end - 1]) != 0))
    {
        end--;
    }

    if (start == end)
    {
        return -1;
    }

    for (index = start; index < end; index++)
    {
        uint32_t digit;

        if ((value[index] < '0') || (value[index] > '9'))
        {
            return -1;
        }

        digit = (uint32_t)(value[index] - '0');
        if (number > (UINT32_MAX - digit) / 10)
        {
            return -1;
        }

        number = (number * 10) + digit;
    }

    if (number == 0)
    {
        return -1;
    }

    *result = number;

    return 0;
}

/**
 * @brief 解析HTTP请求中的Content-Length.
 *
 * @param [in] headers - HTTP请求头.
 * @param [in] headers_size - 请求头长度, 包含请求头结束标记.
 * @param [out] content_length - HTTP消息体长度.
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpParseContentLength(const char *headers,
                                     size_t headers_size,
                                     uint32_t *content_length)
{
    static const char field_name[] = "Content-Length";
    size_t header_data_size;
    size_t line_start = 0;
    uint8_t found = 0;

    if ((headers == NULL) || (content_length == NULL)
        || (headers_size < SNF_OTA_HTTP_HEADER_END_SIZE))
    {
        return -1;
    }

    header_data_size = headers_size - SNF_OTA_HTTP_HEADER_END_SIZE;
    while (line_start < header_data_size)
    {
        size_t line_end = line_start;
        size_t line_size;

        while ((line_end + 1 < header_data_size)
            && ((headers[line_end] != '\r') || (headers[line_end + 1] != '\n')))
        {
            line_end++;
        }

        if ((line_end + 1 >= header_data_size)
            || (headers[line_end] != '\r')
            || (headers[line_end + 1] != '\n'))
        {
            line_end = header_data_size;
        }

        line_size = line_end - line_start;
        if ((line_start != 0)
            && (line_size > (sizeof(field_name) - 1))
            && (headers[line_start + sizeof(field_name) - 1] == ':')
            && (otaHttpAsciiEqual(headers + line_start,
                                  field_name,
                                  sizeof(field_name) - 1) != 0))
        {
            size_t value_start = line_start + sizeof(field_name);

            if (found != 0)
            {
                return -1;
            }

            if (otaHttpParseUint32(headers + value_start,
                                   line_end - value_start,
                                   content_length) != 0)
            {
                return -1;
            }

            found = 1;
        }

        if (line_end == header_data_size)
        {
            break;
        }

        line_start = line_end + 2;
    }

    return (found != 0) ? 0 : -1;
}

/**
 * @brief 写入HTTP OTA响应内容.
 *
 * @param [out] response - 响应缓冲区.
 * @param [in] response_size - 响应缓冲区大小.
 * @param [in] message - 响应消息.
 * @param [in] result_code - 响应中的结果码.
 * @return 响应内容长度, 负数表示失败.
 */
static int otaHttpWriteResponse(char *response,
                                size_t response_size,
                                const char *message,
                                int result_code)
{
    int response_length;

    if ((response == NULL) || (response_size == 0) || (message == NULL))
    {
        return -1;
    }

    response_length = snprintf(response,
                               response_size,
                               "%s (%d)",
                               message,
                               result_code);
    if ((response_length < 0) || ((size_t)response_length >= response_size))
    {
        return -1;
    }

    return response_length;
}

/**
 * @brief 等待一段镜像数据完成写入.
 *
 * @param [in] expected_size - 期望已写入长度.
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpWaitWrite(uint32_t expected_size)
{
    SnfHttpOtaState *ota_state = &http_ota_state;
    TickType_t start_tick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(SNF_OTA_HTTP_WAIT_TIMEOUT_MS))
    {
        if (ota_state->received_size >= expected_size)
        {
            return snfOtaStateIsFailed(ota_state->state);
        }

        if (snfOtaStateIsFailed(ota_state->state) != 0)
        {
            return -1;
        }

        vTaskDelay(pdMS_TO_TICKS(SNF_OTA_HTTP_WAIT_INTERVAL_MS));
    }

    return -1;
}

/**
 * @brief 等待OTA镜像校验及应用完成.
 *
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpWaitForComplete(void)
{
    SnfHttpOtaState *ota_state = &http_ota_state;
    TickType_t start_tick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(SNF_OTA_HTTP_WAIT_TIMEOUT_MS))
    {
        SnfOtaState state = ota_state->state;

        if (state == SNF_OTA_STATE_SUCCESS)
        {
            return 0;
        }

        if (snfOtaStateIsFailed(state) != 0)
        {
            return -1;
        }

        vTaskDelay(pdMS_TO_TICKS(SNF_OTA_HTTP_WAIT_INTERVAL_MS));
    }

    return -1;
}

/** @brief 中止失败的HTTP OTA任务并等待任务结束. */
static void otaHttpAbort(void)
{
    SnfHttpOtaState *ota_state = &http_ota_state;
    TickType_t start_tick;
    int ret;

    if (snfOtaStateIsActive(ota_state->state) == 0)
    {
        return;
    }

    ret = snfOtaAbort();
    if (ret != SNF_OTA_OK)
    {
        LOG_I(tag, "abort HTTP OTA failed: %d", ret);
        return;
    }

    start_tick = xTaskGetTickCount();

    while ((snfOtaStateIsActive(ota_state->state) != 0)
        && ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(SNF_OTA_HTTP_WAIT_TIMEOUT_MS)))
    {
        vTaskDelay(pdMS_TO_TICKS(SNF_OTA_HTTP_WAIT_INTERVAL_MS));
    }
}

/**
 * @brief 处理OTA模块状态回调.
 *
 * @param [in] state - OTA状态.
 * @param [in] event_data - OTA事件数据.
 */
static void otaHttpEventCallback(SnfOtaState state,
                                 const SnfOtaEventData *event_data)
{
    SnfHttpOtaState *ota_state = &http_ota_state;

    ota_state->state = state;
    if (event_data != NULL)
    {
        ota_state->received_size = event_data->received_size;
        ota_state->error_code = event_data->error_code;
    }

    if ((ota_state->state == SNF_OTA_STATE_RECEIVING)
        && (ota_state->percent != event_data->percent))
    {
        ota_state->percent = event_data->percent;
        LOG_I(tag, "OTA receiving: %u%%", (unsigned int)ota_state->percent);
    }

    if (state != SNF_OTA_STATE_VERIFY_SUCCESS)
    {
        return;
    }

    if (snfOtaApply() != SNF_OTA_OK)
    {
        ota_state->state = SNF_OTA_STATE_FAILED;
        ota_state->error_code = SNF_OTA_ERROR_APPLY_FAILED;
    }
}

/**
 * @brief 接收完整HTTP请求头.
 *
 * @param [in] client_socket - HTTP客户端套接字.
 * @param [in] request - 已接收请求数据.
 * @param [in] request_size - 已接收请求数据长度.
 * @param [out] buffer - 请求头缓存.
 * @param [in] buffer_size - 请求头缓存大小.
 * @param [out] buffered_size - 缓存中的总数据长度.
 * @param [out] header_end - 请求头结束偏移量.
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpReceiveHeader(int client_socket,
                                const char *request,
                                size_t request_size,
                                char *buffer,
                                size_t buffer_size,
                                size_t *buffered_size,
                                size_t *header_end)
{
    int receive_length;

    if ((request == NULL) || (request_size == 0)
        || (buffer == NULL) || (request_size > buffer_size)
        || (buffered_size == NULL) || (header_end == NULL))
    {
        return -1;
    }

    memcpy(buffer, request, request_size);
    *buffered_size = request_size;
    *header_end = otaHttpFindHeaderEnd(buffer, *buffered_size);

    while ((*header_end == 0) && (*buffered_size < buffer_size))
    {
        receive_length = (int)lwip_recv(client_socket,
                                        buffer + *buffered_size,
                                        buffer_size - *buffered_size,
                                        0);
        if (receive_length <= 0)
        {
            return -1;
        }

        *buffered_size += (size_t)receive_length;
        *header_end = otaHttpFindHeaderEnd(buffer, *buffered_size);
    }

    return (*header_end != 0) ? 0 : -1;
}

/**
 * @brief 启动HTTP OTA核心任务.
 *
 * @param [in] image_size - 镜像总长度.
 * @return SNF_OTA_OK表示成功, 其他值表示失败.
 */
static int otaHttpStart(uint32_t image_size)
{
    SnfHttpOtaState *ota_state = &http_ota_state;
    SnfOtaConfig ota_config = {0};

    ota_state->state = SNF_OTA_STATE_RECEIVING;
    ota_state->received_size = 0;
    ota_state->percent = 0;
    ota_state->error_code = SNF_OTA_ERROR_NONE;

    ota_config.image_info.size = image_size;
    ota_config.format = SNF_OTA_FORMAT_PACKAGE;
    memcpy(ota_config.file_name, SNF_OTA_DEFAULT_FILE_NAME, sizeof(SNF_OTA_DEFAULT_FILE_NAME));
    ota_config.image_info.check_type = SNF_OTA_CHECK_NONE;
    ota_config.image_info.cipher_type = SNF_OTA_CIPHER_NONE;
    ota_config.event_callback = otaHttpEventCallback;

    return snfOtaStart(&ota_config);
}

/**
 * @brief 从套接字接收指定长度的数据.
 *
 * @param [in] client_socket - HTTP客户端套接字.
 * @param [out] buffer - 接收缓存.
 * @param [in] size - 需要接收的字节长度.
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpReceiveBuffer(int client_socket, uint8_t *buffer, uint32_t size)
{
    uint32_t filled_size = 0;
    int receive_length;

    while (filled_size < size)
    {
        receive_length = (int)lwip_recv(client_socket,
                                        buffer + filled_size,
                                        size - filled_size,
                                        0);
        if (receive_length <= 0)
        {
            LOG_I(tag,
                  "OTA upload recv failed: len=%d, filled=%lu/%lu",
                  receive_length,
                  (unsigned long)filled_size,
                  (unsigned long)size);
            return -1;
        }

        filled_size += (uint32_t)receive_length;
    }

    return 0;
}

/**
 * @brief 提交一段HTTP OTA数据并等待写入完成.
 *
 * @param [in] offset - 镜像内偏移量.
 * @param [in] data - 镜像数据.
 * @param [in] len - 数据长度.
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpWriteData(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (snfOtaWrite(offset, data, len) != SNF_OTA_OK)
    {
        LOG_I(tag,
              "OTA upload write failed: offset=%lu, len=%lu",
              (unsigned long)offset,
              (unsigned long)len);
        return -1;
    }

    if (otaHttpWaitWrite(offset + len) != 0)
    {
        LOG_I(tag, "OTA upload wait failed: offset=%lu", (unsigned long)offset);
        return -1;
    }

    return 0;
}

/**
 * @brief 接收并写入完整HTTP OTA消息体.
 *
 * 写完一段flash后再收取下一段.
 *
 * @param [in] client_socket - HTTP客户端套接字.
 * @param [in] initial_data - 请求头缓存中已有的消息体数据.
 * @param [in] initial_size - 已有消息体数据长度.
 * @param [in] image_size - 镜像总长度.
 * @return 0表示成功, 负数表示失败.
 */
static int otaHttpReceiveImage(int client_socket,
                               const uint8_t *initial_data,
                               uint32_t initial_size,
                               uint32_t image_size)
{
    uint32_t written_size = 0;
    uint32_t remaining_size;
    uint32_t chunk_size;

    if (initial_size > image_size)
    {
        return -1;
    }

    if (initial_size != 0)
    {
        if (otaHttpWriteData(0, initial_data, initial_size) != 0)
        {
            return -1;
        }

        written_size = initial_size;
    }

    while (written_size < image_size)
    {
        remaining_size = image_size - written_size;
        chunk_size = SNF_OTA_HTTP_DATA_BUFFER_SIZE;

        if (chunk_size > remaining_size)
        {
            chunk_size = remaining_size;
        }

        if (otaHttpReceiveBuffer(client_socket, ota_http_data_buffer, chunk_size) != 0)
        {
            return -1;
        }

        if (otaHttpWriteData(written_size, ota_http_data_buffer, chunk_size) != 0)
        {
            return -1;
        }

        written_size += chunk_size;
    }

    return otaHttpWaitForComplete();
}

int snfOtaHttpIsUploadRequest(const char *request, size_t request_size)
{
    static const char request_prefix[] = "POST " SNF_OTA_HTTP_UPLOAD_PATH " ";

    if ((request == NULL) || (request_size < sizeof(request_prefix) - 1))
    {
        return 0;
    }

    return (memcmp(request, request_prefix, sizeof(request_prefix) - 1) == 0) ? 1 : 0;
}

int snfOtaHttpHandleUpload(int client_socket,
                           const char *request,
                           size_t request_size,
                           char *response,
                           size_t response_size)
{
    SnfHttpOtaState *ota_state = &http_ota_state;
    char header_buffer[SNF_OTA_HTTP_HEADER_BUFFER_SIZE] = {0};
    size_t buffered_size = 0;
    size_t header_end = 0;
    size_t body_size;
    uint32_t content_length = 0;
    int ret;

    if ((response == NULL) || (response_size == 0))
    {
        return -1;
    }

    ret = otaHttpReceiveHeader(client_socket,
                               request,
                               request_size,
                               header_buffer,
                               sizeof(header_buffer),
                               &buffered_size,
                               &header_end);
    if (ret != 0)
    {
        return otaHttpWriteResponse(response,
                                    response_size,
                                    "OTA upload header error",
                                    SNF_OTA_ERROR_INVALID_PARAM);
    }

    if (otaHttpParseContentLength(header_buffer,
                                  header_end,
                                  &content_length) != 0)
    {
        return otaHttpWriteResponse(response,
                                    response_size,
                                    "OTA upload length error",
                                    SNF_OTA_ERROR_INVALID_PARAM);
    }

    body_size = buffered_size - header_end;
    if (body_size > content_length)
    {
        return otaHttpWriteResponse(response,
                                    response_size,
                                    "OTA upload body error",
                                    SNF_OTA_ERROR_INVALID_PARAM);
    }

    ret = otaHttpStart(content_length);
    if (ret != SNF_OTA_OK)
    {
        return otaHttpWriteResponse(response,
                                    response_size,
                                    "OTA upload start failed",
                                    ret);
    }

    ret = otaHttpReceiveImage(client_socket,
                              (const uint8_t *)header_buffer + header_end,
                              (uint32_t)body_size,
                              content_length);
    if (ret == 0)
    {
        return otaHttpWriteResponse(response,
                                    response_size,
                                    "OTA upload success",
                                    SNF_OTA_ERROR_NONE);
    }

    LOG_I(tag,
          "OTA upload failed: ret=%d, state=%d, error=%u",
          ret,
          ota_state->state,
          (unsigned int)ota_state->error_code);
    ret = ota_state->error_code;
    otaHttpAbort();

    return otaHttpWriteResponse(response,
                                response_size,
                                "OTA upload failed",
                                ret);
}

int snfOtaHttpGetProgress(const char *query,
                          size_t query_length,
                          char *response,
                          size_t response_size)
{
    SnfHttpOtaState *ota_state = &http_ota_state;
    int response_length;

    if ((response == NULL) || (response_size == 0))
    {
        return -1;
    }

    response_length = snprintf(response,
                               response_size,
                               "%u%%",
                               (unsigned int)ota_state->percent);
    if ((response_length < 0) || ((size_t)response_length >= response_size))
    {
        return -1;
    }

    return response_length;
}
