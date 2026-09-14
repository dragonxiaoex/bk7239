/**
 * @file    sonoff_websocket.c
 * @brief   WebSocket握手、分片与控制帧
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-14
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/sha1.h"

#include "sonoff_base64.h"
#include "sonoff_websocket.h"

static const char websocket_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/** @brief 客户端帧头. */
typedef struct
{
    uint32_t length;
    uint8_t mask[4];
    uint8_t opcode;
    bool final;
} SnfWebsocketFrame;

/** @brief 验证完整UTF-8文本，拒绝过长编码、代理码点和越界码点. */
static bool websocketValidUtf8(const uint8_t *data, size_t length)
{
    uint32_t codepoint = 0;
    uint32_t minimum = 0;
    uint8_t remaining = 0;

    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = data[i];
        if (remaining != 0)
        {
            if ((byte & 0xC0) != 0x80)
            {
                return false;
            }
            codepoint = (codepoint << 6) | (byte & 0x3F);
            remaining--;
            if ((remaining == 0) && ((codepoint < minimum) || (codepoint > 0x10FFFF)
                                     || ((codepoint >= 0xD800) && (codepoint <= 0xDFFF))))
            {
                return false;
            }
        }
        else if (byte >= 0x80)
        {
            if ((byte >= 0xC2) && (byte <= 0xDF))
            {
                codepoint = byte & 0x1F;
                minimum = 0x80;
                remaining = 1;
            }
            else if ((byte >= 0xE0) && (byte <= 0xEF))
            {
                codepoint = byte & 0x0F;
                minimum = 0x800;
                remaining = 2;
            }
            else if ((byte >= 0xF0) && (byte <= 0xF4))
            {
                codepoint = byte & 7;
                minimum = 0x10000;
                remaining = 3;
            }
            else
            {
                return false;
            }
        }
    }

    return remaining == 0;
}

/** @brief 读取帧头；正数为协议关闭码，负数为连接结束. */
static int websocketReadHeader(SnfHttpReader *reader, SnfWebsocketFrame *frame)
{
    uint8_t header[2];
    uint8_t extended[8];
    uint64_t length;

    if (snfHttpReadExact(reader, header, sizeof(header)) != 0)
    {
        return -1;
    }
    frame->final = (header[0] & 0x80) != 0;
    frame->opcode = header[0] & 15;
    length = header[1] & 0x7F;
    if (((header[0] & 0x70) != 0) || ((header[1] & 0x80) == 0)
            || ((frame->opcode >= 3) && (frame->opcode <= 7)) || (frame->opcode > SNF_WS_PONG)
            || ((frame->opcode >= SNF_WS_CLOSE) && (!frame->final || (length > 125))))
    {
        return 1002;
    }
    if (length >= 126)
    {
        uint32_t count = length == 126 ? 2 : 8;
        if (snfHttpReadExact(reader, extended, count) != 0)
        {
            return -1;
        }
        if ((count == 8) && ((extended[0] & 0x80) != 0))
        {
            return 1002;
        }
        length = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            length = (length << 8) | extended[i];
        }
        if (((count == 2) && (length < 126)) || ((count == 8) && (length < 65536)))
        {
            return 1002;
        }
    }
    if (length > SNF_HTTP_RPC_MAX_LENGTH)
    {
        return 1009;
    }
    frame->length = (uint32_t)length;

    return snfHttpReadExact(reader, frame->mask, sizeof(frame->mask));
}

/** @brief 校验关闭码和UTF-8关闭原因. */
static int websocketValidateClose(const uint8_t *data, size_t length)
{
    if (length == 1)
    {
        return 1002;
    }
    if (length >= 2)
    {
        uint32_t code = ((uint32_t)data[0] << 8) | data[1];
        if (!(((code >= 1000) && (code <= 1014) && (code != 1004)
                && (code != 1005) && (code != 1006)) || ((code >= 3000) && (code <= 4999))))
        {
            return 1002;
        }
        if (!websocketValidUtf8(data + 2, length - 2))
        {
            return 1007;
        }
    }

    return 0;
}

int snfWebsocketBuildAccept(const char *key, char *accept)
{
    uint8_t decoded[16];
    uint8_t digest[20];
    char input[24 + sizeof(websocket_guid)];
    uint32_t length = 0;
    int ret;

    if ((key == NULL) || (accept == NULL) || (strlen(key) != 24))
    {
        return -1;
    }
    ret = snfBase64Decode(decoded, sizeof(decoded), &length, (const uint8_t *)key, 24);
    if ((ret != 0) || (length != sizeof(decoded)))
    {
        return -1;
    }
    memcpy(input, key, 24);
    memcpy(input + 24, websocket_guid, sizeof(websocket_guid));
    if (mbedtls_sha1((const uint8_t *)input, sizeof(input) - 1, digest) != 0)
    {
        return -1;
    }

    return snfBase64Encode((uint8_t *)accept, SNF_WS_ACCEPT_SIZE, &length, digest, sizeof(digest));
}

int snfWebsocketSendFrame(int socket, uint8_t opcode, const void *data, size_t length)
{
    uint8_t header[10];
    uint32_t header_length = 2;

    if (((length != 0) && (data == NULL)) || ((opcode >= SNF_WS_CLOSE) && (length > 125)))
    {
        return -1;
    }
    header[0] = 0x80 | opcode;
    if (length < 126)
    {
        header[1] = (uint8_t)length;
    }
    else if (length <= 65535)
    {
        header[1] = 126;
        header[2] = (uint8_t)(length >> 8);
        header[3] = (uint8_t)length;
        header_length = 4;
    }
    else
    {
        uint64_t value = length;
        header[1] = 127;
        for (uint32_t i = 0; i < 8; i++)
        {
            header[9 - i] = (uint8_t)value;
            value >>= 8;
        }
        header_length = 10;
    }
    if (snfHttpSendAll(socket, header, header_length) != 0)
    {
        return -1;
    }

    return snfHttpSendAll(socket, data, length);
}

void snfWebsocketRun(SnfHttpReader *reader, const SnfWebsocketCallbacks *callbacks, void *ctx)
{
    uint8_t *message = malloc(SNF_HTTP_RPC_MAX_LENGTH + 1);
    uint8_t control[125];
    size_t used = 0;
    bool fragmented = false;
    bool running = message != NULL;
    int close_code = message == NULL ? 1011 : 0;

    while (running)
    {
        SnfWebsocketFrame frame = {0};
        int ret = websocketReadHeader(reader, &frame);
        if (ret != 0)
        {
            close_code = ret > 0 ? ret : 0;
            break;
        }
        bool is_control = frame.opcode >= SNF_WS_CLOSE;
        if (!is_control && ((frame.opcode == 2) || ((frame.opcode == 0) != fragmented)))
        {
            close_code = frame.opcode == 2 ? 1003 : 1002;
            running = false;
        }
        else if (!is_control && (frame.length > SNF_HTTP_RPC_MAX_LENGTH - used))
        {
            close_code = 1009;
            running = false;
        }
        else
        {
            uint8_t *payload = is_control ? control : message + used;
            if (snfHttpReadExact(reader, payload, frame.length) != 0)
            {
                running = false;
            }
            else
            {
                for (uint32_t i = 0; i < frame.length; i++)
                {
                    payload[i] ^= frame.mask[i % 4];
                }
                if (frame.opcode == SNF_WS_CLOSE)
                {
                    close_code = websocketValidateClose(payload, frame.length);
                    if (close_code == 0)
                    {
                        callbacks->send_frame(ctx, SNF_WS_CLOSE, payload, frame.length);
                    }
                    running = false;
                }
                else if (frame.opcode == SNF_WS_PING)
                {
                    running = callbacks->send_frame(ctx, SNF_WS_PONG, payload, frame.length) == 0;
                }
                else if (!is_control)
                {
                    used += frame.length;
                    fragmented = !frame.final;
                    if (frame.final)
                    {
                        /* 重组后校验UTF-8，允许码点跨帧；RPC字符串不接受内嵌NUL。 */
                        if (!websocketValidUtf8(message, used) || (memchr(message, 0, used) != NULL))
                        {
                            close_code = 1007;
                            running = false;
                        }
                        else
                        {
                            message[used] = '\0';
                            running = callbacks->receive_text(ctx, (const char *)message) == 0;
                        }
                        used = 0;
                    }
                }
            }
        }
    }
    if (close_code != 0)
    {
        uint8_t code[2] = {(uint8_t)(close_code >> 8), (uint8_t)close_code};
        callbacks->send_frame(ctx, SNF_WS_CLOSE, code, sizeof(code));
    }
    free(message);
}
