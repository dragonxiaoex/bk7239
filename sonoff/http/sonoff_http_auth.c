/**
 * @file    sonoff_http_auth.c
 * @brief   独立HTTP SHA-256摘要认证
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-14
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <FreeRTOS.h>
#include <task.h>
#include <driver/trng.h>

#include "sonoff_http.h"
#include "sonoff_http_internal.h"
#include "sonoff_sha256.h"

/** @brief HTTP认证参数. */
#define HTTP_AUTH_USERNAME_SIZE    32        /* 用户名容量 */
#define HTTP_AUTH_REALM_SIZE       64        /* 认证域容量 */
#define HTTP_AUTH_URI_SIZE         64        /* 路径容量 */
#define HTTP_AUTH_NONCE_COUNT      8         /* 跨短连接保留的质询数量 */
#define HTTP_AUTH_NONCE_TTL_MS     300000    /* 质询有效期 */

/** @brief HTTP凭据；ha1为空表示未启用认证. */
typedef struct
{
    char username[HTTP_AUTH_USERNAME_SIZE];
    char realm[HTTP_AUTH_REALM_SIZE];
    char uri[HTTP_AUTH_URI_SIZE];
    char ha1[65];
} SnfHttpAuthInfo;

/** @brief 单次质询的计数与有效期. */
typedef struct
{
    char value[33];
    uint32_t nc;
    TickType_t issued;
} SnfHttpAuthNonce;

/** @brief 配置和质询缓存，仅在短临界区内访问. */
typedef struct
{
    SnfHttpAuthInfo info;
    SnfHttpAuthNonce nonces[HTTP_AUTH_NONCE_COUNT];
    uint32_t next_nonce;
    uint32_t generation;
} SnfHttpAuthState;

static SnfHttpAuthState http_auth =
{
    .info = {.uri = "/rpc"},
};

/** @brief 授权参数均指向原始请求头，解析后不分配字符串. */
typedef struct
{
    char *username;
    char *realm;
    char *nonce;
    char *uri;
    char *response;
    char *algorithm;
    char *qop;
    char *nc;
    char *cnonce;
} SnfHttpDigest;

/** @brief 对多个字符串片段计算SHA-256，避免构造大临时缓冲区. */
static int httpAuthHash(const char *const *parts, size_t count, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    SnfSha256Ctx ctx;
    uint8_t digest[SNF_SHA256_DIGEST_SIZE];

    if (snfSha256Init(&ctx) != 0)
    {
        return -1;
    }
    for (size_t i = 0; i < count; i++)
    {
        if (snfSha256Update(&ctx, (const uint8_t *)parts[i], strlen(parts[i])) != 0)
        {
            snfSha256Free(&ctx);
            return -1;
        }
    }
    if (snfSha256Finish(&ctx, digest, sizeof(digest)) != 0)
    {
        return -1;
    }
    for (size_t i = 0; i < sizeof(digest); i++)
    {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = '\0';

    return 0;
}

/** @brief 验证会进入HTTP头字段的配置字符串. */
static bool httpAuthValidText(const char *text, size_t capacity)
{
    if ((text == NULL) || (text[0] == '\0') || (strlen(text) >= capacity))
    {
        return false;
    }
    for (const uint8_t *p = (const uint8_t *)text; *p != 0; p++)
    {
        if ((*p < 32) || (*p == 127) || (*p == '"') || (*p == '\\'))
        {
            return false;
        }
    }

    return true;
}

/** @brief 原地解析Digest参数，包括带引号的值和转义字符. */
static bool httpAuthParse(char *header, SnfHttpDigest *digest)
{
    char *p;

    if ((header == NULL) || (strncasecmp(header, "Digest ", 7) != 0))
    {
        return false;
    }
    p = header + 7;
    while (*p != '\0')
    {
        char *name;
        char *value;
        char *end;
        char **field = NULL;

        while ((*p == ' ') || (*p == '\t'))
        {
            p++;
        }
        name = p;
        while (((*p >= 'a') && (*p <= 'z')) || ((*p >= 'A') && (*p <= 'Z')) || (*p == '-'))
        {
            p++;
        }
        if (p == name)
        {
            return false;
        }
        end = p;
        while ((*p == ' ') || (*p == '\t'))
        {
            p++;
        }
        if (*p != '=')
        {
            return false;
        }
        *end = '\0';
        p++;
        while ((*p == ' ') || (*p == '\t'))
        {
            p++;
        }
        if (*p == '"')
        {
            p++;
            value = p;
            end = value;
            while ((*p != '\0') && (*p != '"'))
            {
                if (*p == '\\')
                {
                    p++;
                    if (*p == '\0')
                    {
                        return false;
                    }
                }
                *end++ = *p++;
            }
            if (*p != '"')
            {
                return false;
            }
            p++;
            *end = '\0';
        }
        else
        {
            value = p;
            while ((*p != '\0') && (*p != ',') && (*p != ' ') && (*p != '\t'))
            {
                p++;
            }
            end = p;
        }
        while ((*p == ' ') || (*p == '\t'))
        {
            p++;
        }
        if ((*p != '\0') && (*p != ','))
        {
            return false;
        }
        if (*p == ',')
        {
            p++;
            if (*p == '\0')
            {
                return false;
            }
        }
        *end = '\0';
        if (strcasecmp(name, "username") == 0)
        {
            field = &digest->username;
        }
        else if (strcasecmp(name, "realm") == 0)
        {
            field = &digest->realm;
        }
        else if (strcasecmp(name, "nonce") == 0)
        {
            field = &digest->nonce;
        }
        else if (strcasecmp(name, "uri") == 0)
        {
            field = &digest->uri;
        }
        else if (strcasecmp(name, "response") == 0)
        {
            field = &digest->response;
        }
        else if (strcasecmp(name, "algorithm") == 0)
        {
            field = &digest->algorithm;
        }
        else if (strcasecmp(name, "qop") == 0)
        {
            field = &digest->qop;
        }
        else if (strcasecmp(name, "nc") == 0)
        {
            field = &digest->nc;
        }
        else if (strcasecmp(name, "cnonce") == 0)
        {
            field = &digest->cnonce;
        }
        if (field != NULL)
        {
            if (*field != NULL)
            {
                return false;
            }
            *field = value;
        }
    }

    return (digest->username != NULL) && (digest->realm != NULL) && (digest->nonce != NULL)
           && (digest->uri != NULL) && (digest->response != NULL) && (digest->algorithm != NULL)
           && (digest->qop != NULL) && (digest->nc != NULL) && (digest->cnonce != NULL);
}

/** @brief 检查质询仍有效；调用者持有临界区. */
static SnfHttpAuthNonce *httpAuthFindNonce(const char *value, uint32_t nc)
{
    SnfHttpAuthState *state = &http_auth;
    TickType_t now = xTaskGetTickCount();

    for (uint32_t i = 0; i < HTTP_AUTH_NONCE_COUNT; i++)
    {
        SnfHttpAuthNonce *nonce = &state->nonces[i];
        if ((strcmp(nonce->value, value) == 0) && (nc > nonce->nc)
                && ((TickType_t)(now - nonce->issued) < pdMS_TO_TICKS(HTTP_AUTH_NONCE_TTL_MS)))
        {
            return nonce;
        }
    }

    return NULL;
}

/** @brief 验证标准HTTP摘要；nc参与哈希时保留客户端的8位十六进制文本. */
static bool httpAuthVerify(SnfHttpRequest *request, const SnfHttpAuthInfo *info, uint32_t generation)
{
    SnfHttpDigest digest = {0};
    SnfHttpAuthState *state = &http_auth;
    SnfHttpAuthNonce *nonce;
    char ha2[65];
    char expected[65];
    uint32_t nc = 0;
    bool valid;

    if (!httpAuthParse(request->authorization, &digest)
            || (strcmp(digest.username, info->username) != 0) || (strcmp(digest.realm, info->realm) != 0)
            || (strcmp(digest.uri, request->uri) != 0) || (strcasecmp(digest.algorithm, "SHA-256") != 0)
            || (strcmp(digest.qop, "auth") != 0) || (strlen(digest.nc) != 8)
            || (strlen(digest.nonce) != 32) || (strlen(digest.response) != 64)
            || (digest.cnonce[0] == '\0') || (strlen(digest.cnonce) > 128))
    {
        return false;
    }
    for (uint32_t i = 0; i < 8; i++)
    {
        uint32_t digit;
        char c = digest.nc[i];
        if ((c >= '0') && (c <= '9'))
        {
            digit = (uint32_t)(c - '0');
        }
        else if ((c >= 'a') && (c <= 'f'))
        {
            digit = (uint32_t)(c - 'a') + 10;
        }
        else if ((c >= 'A') && (c <= 'F'))
        {
            digit = (uint32_t)(c - 'A') + 10;
        }
        else
        {
            return false;
        }
        nc = (nc << 4) | digit;
    }
    taskENTER_CRITICAL();
    valid = (generation == state->generation) && (httpAuthFindNonce(digest.nonce, nc) != NULL);
    taskEXIT_CRITICAL();
    if (!valid)
    {
        return false;
    }
    const char *ha2_parts[] = {request->method, ":", request->uri};
    const char *response_parts[] = {info->ha1, ":", digest.nonce, ":", digest.nc, ":",
                                    digest.cnonce, ":auth:", ha2
                                   };
    if ((httpAuthHash(ha2_parts, 3, ha2) != 0) || (httpAuthHash(response_parts, 9, expected) != 0))
    {
        return false;
    }
    uint8_t difference = 0;
    for (uint32_t i = 0; i < 64; i++)
    {
        difference |= (uint8_t)expected[i] ^ (uint8_t)digest.response[i];
    }
    if (difference != 0)
    {
        return false;
    }
    /* 哈希计算期间配置或计数可能变化，提交前重新检查，避免重复请求同时通过。 */
    taskENTER_CRITICAL();
    nonce = httpAuthFindNonce(digest.nonce, nc);
    valid = (generation == state->generation) && (nonce != NULL);
    if (valid)
    {
        nonce->nc = nc;
    }
    taskEXIT_CRITICAL();

    return valid;
}

int snfHttpSetAuthInfo(const char *username, const char *password, const char *realm)
{
    SnfHttpAuthState *state = &http_auth;
    SnfHttpAuthInfo info = {0};
    const char *auth_realm = realm != NULL ? realm : "sonoff";

    if ((username != NULL) || (password != NULL))
    {
        if (!httpAuthValidText(username, sizeof(info.username)) || (password == NULL)
                || !httpAuthValidText(auth_realm, sizeof(info.realm)) || (strchr(username, ':') != NULL))
        {
            return -1;
        }
        const char *parts[] = {username, ":", auth_realm, ":", password};
        if (httpAuthHash(parts, 5, info.ha1) != 0)
        {
            return -1;
        }
        strcpy(info.username, username);
        strcpy(info.realm, auth_realm);
    }
    taskENTER_CRITICAL();
    memcpy(info.uri, state->info.uri, sizeof(info.uri));
    state->info = info;
    state->generation++;
    memset(state->nonces, 0, sizeof(state->nonces));
    taskEXIT_CRITICAL();

    return 0;
}

int snfHttpSetAuthUri(const char *uri)
{
    SnfHttpAuthState *state = &http_auth;

    if (!httpAuthValidText(uri, HTTP_AUTH_URI_SIZE) || (uri[0] != '/')
            || (strpbrk(uri, " ?#") != NULL))
    {
        return -1;
    }
    taskENTER_CRITICAL();
    strcpy(state->info.uri, uri);
    state->generation++;
    memset(state->nonces, 0, sizeof(state->nonces));
    taskEXIT_CRITICAL();

    return 0;
}

int snfHttpAuthCheck(SnfHttpRequest *request, char *challenge)
{
    static const char digits[] = "0123456789abcdef";
    SnfHttpAuthState *state = &http_auth;
    SnfHttpAuthInfo info;
    SnfHttpAuthNonce nonce = {0};
    uint32_t generation;
    int length;

    taskENTER_CRITICAL();
    info = state->info;
    generation = state->generation;
    taskEXIT_CRITICAL();
    challenge[0] = '\0';
    if ((info.ha1[0] == '\0') || (strcmp(info.uri, request->uri) != 0))
    {
        return 0;
    }
    if (httpAuthVerify(request, &info, generation))
    {
        return 0;
    }
    for (uint32_t i = 0; i < 4; i++)
    {
        uint32_t random = bk_rand();
        for (uint32_t j = 0; j < 8; j++)
        {
            nonce.value[i * 8 + j] = digits[random & 15];
            random >>= 4;
        }
    }
    nonce.issued = xTaskGetTickCount();
    taskENTER_CRITICAL();
    if (generation != state->generation)
    {
        taskEXIT_CRITICAL();
        return 500;
    }
    state->nonces[state->next_nonce] = nonce;
    state->next_nonce = (state->next_nonce + 1) % HTTP_AUTH_NONCE_COUNT;
    taskEXIT_CRITICAL();
    length = snprintf(challenge, SNF_HTTP_AUTH_HEADER_SIZE,
                      "WWW-Authenticate: Digest realm=\"%s\", nonce=\"%s\", algorithm=SHA-256, qop=\"auth\"\r\n",
                      info.realm, nonce.value);

    return ((length > 0) && (length < SNF_HTTP_AUTH_HEADER_SIZE)) ? 401 : 500;
}
