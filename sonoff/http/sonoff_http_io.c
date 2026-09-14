/**
 * @file    sonoff_http_io.c
 * @brief   HTTP报文解析与套接字收发
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-14
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <lwip/sockets.h>

#include "sonoff_http_internal.h"

int snfHttpSendAll(int socket, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    size_t sent = 0;

    while (sent < length)
    {
        int count = lwip_send(socket, bytes + sent, length - sent, 0);
        if ((count < 0) && (errno == EINTR))
        {
            count = 0;
        }
        else if (count <= 0)
        {
            return -1;
        }
        sent += (size_t)count;
    }

    return 0;
}

int snfHttpReadExact(SnfHttpReader *reader, void *data, size_t length)
{
    uint8_t *bytes = data;
    size_t copied = 0;

    while (copied < length)
    {
        if (reader->position == reader->length)
        {
            int count;
            do
            {
                count = lwip_recv(reader->socket, reader->buffer, sizeof(reader->buffer), 0);
            }
            while ((count < 0) && (errno == EINTR));
            if (count <= 0)
            {
                return -1;
            }
            reader->position = 0;
            reader->length = (size_t)count;
        }
        size_t count = reader->length - reader->position;
        if (count > length - copied)
        {
            count = length - copied;
        }
        memcpy(bytes + copied, reader->buffer + reader->position, count);
        reader->position += count;
        copied += count;
    }

    return 0;
}

bool snfHttpHeaderHasToken(const char *value, const char *token)
{
    size_t token_length = strlen(token);

    while ((value != NULL) && (*value != '\0'))
    {
        const char *end = strchr(value, ',');
        const char *last = end != NULL ? end : value + strlen(value);
        while ((*value == ' ') || (*value == '\t'))
        {
            value++;
        }
        while ((last > value) && ((last[-1] == ' ') || (last[-1] == '\t')))
        {
            last--;
        }
        if (((size_t)(last - value) == token_length) && (strncasecmp(value, token, token_length) == 0))
        {
            return true;
        }
        value = end != NULL ? end + 1 : NULL;
    }

    return false;
}

/** @brief 保存所需头字段，重复字段作为无效请求拒绝. */
static int httpStoreHeader(SnfHttpRequest *request, char *name, char *value)
{
    char **field = NULL;

    if (strcasecmp(name, "Content-Length") == 0)
    {
        size_t length = 0;
        if (request->has_content_length || (*value == '\0'))
        {
            return 400;
        }
        for (const char *p = value; *p != '\0'; p++)
        {
            if ((*p < '0') || (*p > '9'))
            {
                return 400;
            }
            if (length > (SNF_HTTP_RPC_MAX_LENGTH - (size_t)(*p - '0')) / 10)
            {
                return 413;
            }
            length = length * 10 + (size_t)(*p - '0');
        }
        request->content_length = length;
        request->has_content_length = true;
    }
    else if (strcasecmp(name, "Transfer-Encoding") == 0)
    {
        return 501;
    }
    else if (strcasecmp(name, "Host") == 0)
    {
        field = &request->host;
    }
    else if (strcasecmp(name, "Authorization") == 0)
    {
        field = &request->authorization;
    }
    else if (strcasecmp(name, "Connection") == 0)
    {
        field = &request->connection;
    }
    else if (strcasecmp(name, "Upgrade") == 0)
    {
        field = &request->upgrade;
    }
    else if (strcasecmp(name, "Sec-WebSocket-Key") == 0)
    {
        field = &request->websocket_key;
    }
    else if (strcasecmp(name, "Sec-WebSocket-Version") == 0)
    {
        field = &request->websocket_version;
    }
    else if (strcasecmp(name, "Expect") == 0)
    {
        field = &request->expect;
    }
    if (field != NULL)
    {
        if (*field != NULL)
        {
            return 400;
        }
        *field = value;
    }

    return 0;
}

int snfHttpReadRequest(SnfHttpReader *reader, char *buffer, SnfHttpRequest *request)
{
    size_t used = 0;
    char *line_end;
    char *line;
    bool complete = false;

    memset(request, 0, sizeof(*request));
    /* 输入流缓存TCP预读字节，逐字节查找头结束符不会丢失同包中的正文。 */
    while ((used < SNF_HTTP_HEADER_MAX_LENGTH) && !complete)
    {
        if (snfHttpReadExact(reader, buffer + used, 1) != 0)
        {
            return -1;
        }
        if ((buffer[used] == '\0') || ((uint8_t)buffer[used] == 127)
                || (((uint8_t)buffer[used] < 32) && (buffer[used] != '\r')
                    && (buffer[used] != '\n') && (buffer[used] != '\t')))
        {
            return 400;
        }
        used++;
        complete = (used >= 4) && (memcmp(buffer + used - 4, "\r\n\r\n", 4) == 0);
    }
    if (!complete)
    {
        return 431;
    }
    buffer[used] = '\0';
    line_end = strstr(buffer, "\r\n");
    *line_end = '\0';
    request->method = buffer;
    request->uri = strchr(buffer, ' ');
    if (request->uri == NULL)
    {
        return 400;
    }
    *request->uri++ = '\0';
    request->version = strchr(request->uri, ' ');
    if (request->version == NULL)
    {
        return 400;
    }
    *request->version++ = '\0';
    if ((request->method[0] == '\0') || (request->uri[0] != '/')
            || ((strcmp(request->version, "HTTP/1.1") != 0) && (strcmp(request->version, "HTTP/1.0") != 0)))
    {
        return 400;
    }
    line = line_end + 2;
    while (*line != '\r')
    {
        char *colon;
        char *value;
        char *end;
        int ret;

        line_end = strstr(line, "\r\n");
        if (line_end == NULL)
        {
            return 400;
        }
        *line_end = '\0';
        colon = strchr(line, ':');
        if ((colon == NULL) || (colon == line))
        {
            return 400;
        }
        *colon = '\0';
        for (const char *p = line; p < colon; p++)
        {
            if (!(((*p >= 'a') && (*p <= 'z')) || ((*p >= 'A') && (*p <= 'Z'))
                    || ((*p >= '0') && (*p <= '9')) || (strchr("!#$%&'*+-.^_`|~", *p) != NULL)))
            {
                return 400;
            }
        }
        value = colon + 1;
        while ((*value == ' ') || (*value == '\t'))
        {
            value++;
        }
        end = line_end;
        while ((end > value) && ((end[-1] == ' ') || (end[-1] == '\t')))
        {
            end--;
        }
        *end = '\0';
        if ((strchr(value, '\r') != NULL) || (strchr(value, '\n') != NULL))
        {
            return 400;
        }
        ret = httpStoreHeader(request, line, value);
        if (ret != 0)
        {
            return ret;
        }
        line = line_end + 2;
    }
    if ((strcmp(request->version, "HTTP/1.1") == 0)
            && ((request->host == NULL) || (request->host[0] == '\0')))
    {
        return 400;
    }

    return 0;
}
