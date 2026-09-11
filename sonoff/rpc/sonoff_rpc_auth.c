/**
 * @file    sonoff_rpc_auth.c
 * @brief   RPC 认证和授权实现
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <driver/trng.h>

#include "sonoff_log.h"
#include "sonoff_sha256.h"
#include "sonoff_rpc_internal.h"

static const char *tag = "SNF-RPC-A";

#define RPC_AUTH_NONCE_LEN 16

/**
 * @brief 计算SHA-256并输出小写十六进制摘要.
 *
 * @param [in] input - 待计算字符串.
 * @param [out] output_hex - 至少SNF_RPC_AUTH_SHA256_HEX_LEN + 1字节的缓冲区.
 * @return SNF_RPC_OK表示成功, 其他为错误码.
 */
static int32_t rpcAuthComputeSha256Hex(const char *input, char *output_hex)
{
    static const char hex_digits[] = "0123456789abcdef";
    SnfSha256Ctx ctx = {0};
    uint8_t digest[SNF_SHA256_DIGEST_SIZE];
    int32_t ret;
    uint32_t i;

    if (input == NULL || output_hex == NULL)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    output_hex[0] = '\0';
    ret = snfSha256Init(&ctx);
    if (ret != SNF_SHA256_OK)
    {
        return SNF_RPC_ERR_INTERNAL;
    }

    ret = snfSha256Update(&ctx, (const uint8_t *)input, (uint32_t)strlen(input));
    if (ret != SNF_SHA256_OK)
    {
        snfSha256Free(&ctx);
        return SNF_RPC_ERR_INTERNAL;
    }

    /* 项目 Finish 接口会清理底层上下文；只有前面的 Update 失败路径需要显式 Free。 */
    ret = snfSha256Finish(&ctx, digest, sizeof(digest));
    if (ret != SNF_SHA256_OK)
    {
        return SNF_RPC_ERR_INTERNAL;
    }

    for (i = 0; i < SNF_SHA256_DIGEST_SIZE; i++)
    {
        output_hex[i * 2] = hex_digits[digest[i] >> 4];
        output_hex[i * 2 + 1] = hex_digits[digest[i] & 0x0f];
    }
    output_hex[SNF_RPC_AUTH_SHA256_HEX_LEN] = '\0';

    return SNF_RPC_OK;
}

/**
 * @brief 计算 username:realm:password 的 SHA-256 十六进制摘要.
 */
static bool rpcAuthBuildHa1(const char *username, const char *realm, const char *password, char *output_ha1)
{
    if (!username || !realm || !password || !output_ha1)
    {
        return false;
    }

    char ha1_input[RPC_MAX_USERNAME_LEN + RPC_MAX_REALM_LEN + RPC_MAX_PASSWORD_LEN + 3];
    int written = snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s", username, realm, password);
    if (written < 0 || written >= (int)sizeof(ha1_input))
    {
        return false;
    }

    return rpcAuthComputeSha256Hex(ha1_input, output_ha1) == SNF_RPC_OK;
}

/**
 * @brief 将指定长度的十六进制字符串转为小写并补终止符.
 */
static void rpcAuthNormalizeHexString(const char *input, char *output, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = input[i];
        output[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
    }
    output[len] = '\0';
}

/**
 * @brief 借用字符串节点或将非负整数节点格式化到指定缓冲区.
 */
static bool rpcAuthNodeToString(const cJSON *node, char *buf, size_t buf_len, const char **out_str)
{
    if (!node || !out_str || !buf || buf_len == 0)
    {
        return false;
    }

    if (JSON_IS_STRING(node))
    {
        const char *v = node->valuestring;
        if (!v)
        {
            return false;
        }
        if (strlen(v) >= buf_len)
        {
            return false;
        }
        *out_str = v;
        return true;
    }

    if (JSON_IS_NUMBER(node))
    {
        double num = node->valuedouble;
        if (!isfinite(num) || num < 0)
        {
            return false;
        }
        double intpart = 0.0;
        if (modf(num, &intpart) != 0.0)
        {
            return false;
        }
        int written = snprintf(buf, buf_len, "%.0f", num);
        if (written <= 0 || written >= (int)buf_len)
        {
            return false;
        }
        *out_str = buf;
        return true;
    }

    return false;
}

/**
 * @brief 检查字符串是否包含指定数量的十六进制字符.
 */
static bool rpcAuthIsHexString(const char *str, size_t len)
{
    if (!str)
    {
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        {
            return false;
        }
    }

    return true;
}

int32_t snfRpcAuthSetEnabled(bool enabled)
{
    LOG_I(tag, "snfRpcAuthSetEnabled: enabled=%d", enabled);

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->auth_ctx.auth_mutex, portMAX_DELAY);
    bus->auth_ctx.auth_enabled = enabled;
    xSemaphoreGive(bus->auth_ctx.auth_mutex);

    LOG_I(tag, "Global authentication %s", enabled ? "enabled" : "disabled");

    return SNF_RPC_OK;
}

int32_t snfRpcAuthSetRealm(const char *realm)
{
    LOG_I(tag, "snfRpcAuthSetRealm: realm=%s", realm ? realm : "NULL");

    if (!realm)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    bool external_ha1_invalidated = false;
    xSemaphoreTake(bus->auth_ctx.auth_mutex, portMAX_DELAY);

    char old_realm[RPC_MAX_REALM_LEN];
    strncpy(old_realm, bus->auth_ctx.realm, sizeof(old_realm) - 1);
    old_realm[sizeof(old_realm) - 1] = '\0';

    strncpy(bus->auth_ctx.realm, realm, RPC_MAX_REALM_LEN - 1);
    bus->auth_ctx.realm[RPC_MAX_REALM_LEN - 1] = '\0';

    if (bus->auth_ctx.password_valid && bus->auth_ctx.username[0] != '\0')
    {
        if (!rpcAuthBuildHa1(bus->auth_ctx.username, bus->auth_ctx.realm,
                             bus->auth_ctx.password, bus->auth_ctx.ha1))
        {
            memset(bus->auth_ctx.ha1, 0, sizeof(bus->auth_ctx.ha1));
            bus->auth_ctx.ha1_valid = false;
            xSemaphoreGive(bus->auth_ctx.auth_mutex);
            LOG_E(tag, "snfRpcAuthSetRealm: Failed to refresh HA1");
            return SNF_RPC_ERR_INTERNAL;
        }
        bus->auth_ctx.ha1_valid = true;
    }
    else if (old_realm[0] != '\0' && strcmp(old_realm, bus->auth_ctx.realm) != 0 && bus->auth_ctx.ha1_valid)
    {
        memset(bus->auth_ctx.ha1, 0, sizeof(bus->auth_ctx.ha1));
        bus->auth_ctx.ha1_valid = false;
        external_ha1_invalidated = true;
    }

    xSemaphoreGive(bus->auth_ctx.auth_mutex);

    if (external_ha1_invalidated)
    {
        LOG_W(tag, "Realm changed, cached external HA1 invalidated");
    }

    LOG_I(tag, "Auth realm set: %s", realm);

    return SNF_RPC_OK;
}

int32_t snfRpcAuthSetCredentials(const char *username, const char *password)
{
    LOG_I(tag, "snfRpcAuthSetCredentials: username=%s", username ? username : "NULL");

    if (!username || !password)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->auth_ctx.auth_mutex, portMAX_DELAY);

    strncpy(bus->auth_ctx.username, username, RPC_MAX_USERNAME_LEN - 1);
    bus->auth_ctx.username[RPC_MAX_USERNAME_LEN - 1] = '\0';

    strncpy(bus->auth_ctx.password, password, RPC_MAX_PASSWORD_LEN - 1);
    bus->auth_ctx.password[RPC_MAX_PASSWORD_LEN - 1] = '\0';
    bus->auth_ctx.password_valid = true;

    memset(bus->auth_ctx.ha1, 0, sizeof(bus->auth_ctx.ha1));
    bus->auth_ctx.ha1_valid = false;

    if (bus->auth_ctx.realm[0] != '\0')
    {
        if (!rpcAuthBuildHa1(bus->auth_ctx.username, bus->auth_ctx.realm,
                             bus->auth_ctx.password, bus->auth_ctx.ha1))
        {
            xSemaphoreGive(bus->auth_ctx.auth_mutex);
            LOG_E(tag, "snfRpcAuthSetCredentials: Failed to compute HA1");
            return SNF_RPC_ERR_INTERNAL;
        }
        bus->auth_ctx.ha1_valid = true;
    }

    xSemaphoreGive(bus->auth_ctx.auth_mutex);

    LOG_I(tag, "Credentials set for user: %s", username);

    return SNF_RPC_OK;
}

int32_t snfRpcAuthSetCredentialsHa1(const char *username, const char *ha1)
{
    LOG_I(tag, "snfRpcAuthSetCredentialsHa1: username=%s", username ? username : "NULL");

    if (!username || !ha1)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    size_t ha1_len = strlen(ha1);
    if (ha1_len != SNF_RPC_AUTH_SHA256_HEX_LEN || !rpcAuthIsHexString(ha1, ha1_len))
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    char normalized_ha1[SNF_RPC_AUTH_SHA256_HEX_LEN + 1];
    rpcAuthNormalizeHexString(ha1, normalized_ha1, ha1_len);

    xSemaphoreTake(bus->auth_ctx.auth_mutex, portMAX_DELAY);

    strncpy(bus->auth_ctx.username, username, RPC_MAX_USERNAME_LEN - 1);
    bus->auth_ctx.username[RPC_MAX_USERNAME_LEN - 1] = '\0';

    memset(bus->auth_ctx.password, 0, sizeof(bus->auth_ctx.password));
    bus->auth_ctx.password_valid = false;

    memcpy(bus->auth_ctx.ha1, normalized_ha1, sizeof(bus->auth_ctx.ha1));
    bus->auth_ctx.ha1_valid = true;

    xSemaphoreGive(bus->auth_ctx.auth_mutex);

    LOG_I(tag, "HA1 credentials set for user: %s", username);

    return SNF_RPC_OK;
}

int32_t snfRpcAuthGetChallenge(SnfRpcAuthChallenge *out_challenge, bool refresh_nonce)
{
    if (!out_challenge)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        return SNF_RPC_ERR_NOT_INITED;
    }

    xSemaphoreTake(bus->auth_ctx.auth_mutex, portMAX_DELAY);
    if (bus->auth_ctx.realm[0] == '\0')
    {
        xSemaphoreGive(bus->auth_ctx.auth_mutex);
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    /* 未要求刷新时复用已有挑战；生成新 nonce 后重置与旧 nonce 绑定的计数记录。 */
    if (refresh_nonce || !bus->auth_ctx.last_nonce_valid)
    {
        static const char nonce_chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        char nonce_buf[RPC_AUTH_NONCE_LEN + 1];
        uint32_t i;

        for (i = 0; i < RPC_AUTH_NONCE_LEN; i++)
        {
            nonce_buf[i] = nonce_chars[(uint32_t)bk_rand() % (sizeof(nonce_chars) - 1)];
        }
        nonce_buf[RPC_AUTH_NONCE_LEN] = '\0';
        strncpy(bus->auth_ctx.last_nonce, nonce_buf, sizeof(bus->auth_ctx.last_nonce) - 1);
        bus->auth_ctx.last_nonce[sizeof(bus->auth_ctx.last_nonce) - 1] = '\0';

        bus->auth_ctx.last_nonce_valid = true;
        bus->auth_ctx.last_nc = 0;
        bus->auth_ctx.last_nc_valid = false;
    }

    strncpy(out_challenge->realm, bus->auth_ctx.realm, sizeof(out_challenge->realm) - 1);
    out_challenge->realm[sizeof(out_challenge->realm) - 1] = '\0';
    strncpy(out_challenge->nonce, bus->auth_ctx.last_nonce, sizeof(out_challenge->nonce) - 1);
    out_challenge->nonce[sizeof(out_challenge->nonce) - 1] = '\0';
    out_challenge->nc = 1;
    xSemaphoreGive(bus->auth_ctx.auth_mutex);

    return SNF_RPC_OK;
}

bool snfRpcIsMethodExempt(const char *method_name)
{
    if (!method_name)
    {
        return false;
    }

    SnfRpcMethodNode *method_node = snfRpcMethodFind(method_name);
    if (!method_node)
    {
        return false;
    }

    return ((method_node->flags & RPC_METHOD_FLAG_EXEMPT_AUTH) != 0);
}

int32_t snfRpcValidateAndAuth(cJSON *request_obj, SnfRpcChannelNode *channel)
{
    LOG_I(tag, "snfRpcValidateAndAuth");

    if (!request_obj || !channel)
    {
        return SNF_RPC_ERR_INVALID_ARG;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();

    /* 获取方法名 */
    cJSON *method_node = cJSON_GetObjectItem(request_obj, "method");
    if (!JSON_IS_STRING(method_node))
    {
        return SNF_RPC_ERR_INVALID_REQUEST;
    }

    const char *method_name = method_node->valuestring;

    /* 验证 src 字段（dst 为可选，不作为必填，不进行匹配校验）*/
    cJSON *src_node = cJSON_GetObjectItem(request_obj, "src");

    if (!JSON_IS_STRING(src_node))
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Missing or invalid src field");
        return SNF_RPC_ERR_INVALID_REQUEST;
    }

    const char *src = src_node->valuestring;
    if (!src || src[0] == '\0')
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Empty src");
        return SNF_RPC_ERR_INVALID_REQUEST;
    }
    if (strlen(src) >= RPC_MAX_SRC_LEN)
    {
        LOG_W(tag, "snfRpcValidateAndAuth: src too long");
        return SNF_RPC_ERR_INVALID_REQUEST;
    }

    /* Update remote_src for response routing */
    snfRpcChannelSetRemoteSrc((SnfRpcChannelHandle)channel, src);

    /* 检查通道是否支持认证 */
    /* 如果通道不支持认证（如 HTTP 通道，已在服务端被认证）,则跳过 RPC 层的认证 */
    if (!channel->auth_supported)
    {
        LOG_I(tag, "snfRpcValidateAndAuth: Channel doesn't support auth, skipping RPC-level auth");
        return SNF_RPC_OK;
    }

    /* 检查方法是否在豁免列表中（豁免方法在任何情况下都放行） */
    if (snfRpcIsMethodExempt(method_name))
    {
        return SNF_RPC_OK;
    }

    /* 在未设置密码 (HA1 为空) 时，非豁免方法全部拒绝访问 */
    if (!bus->auth_ctx.ha1_valid || bus->auth_ctx.ha1[0] == '\0')
    {
        LOG_W(tag, "snfRpcValidateAndAuth: No password configured, access denied for %s", method_name);
        return SNF_RPC_ERR_NO_PASSWORD;
    }

    /* Check if authentication is needed (仅当通道支持认证时) */
    bool auth_required = bus->auth_ctx.auth_enabled;
    if (!auth_required)
    {
        return SNF_RPC_OK;  /* Auth not required */
    }

    /* 从请求中获取 auth 字段 */
    cJSON *auth_node = cJSON_GetObjectItem(request_obj, "auth");
    if (!JSON_IS_OBJECT(auth_node))
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Auth required but not provided");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    /* 验证认证凭据（摘要认证）*/
    /* 格式：auth = {realm, username, nonce, cnonce, nc, response, algorithm} */

    cJSON *auth_realm = cJSON_GetObjectItem(auth_node, "realm");
    cJSON *auth_username = cJSON_GetObjectItem(auth_node, "username");
    cJSON *auth_nonce = cJSON_GetObjectItem(auth_node, "nonce");
    cJSON *auth_cnonce = cJSON_GetObjectItem(auth_node, "cnonce");
    cJSON *auth_nc = cJSON_GetObjectItem(auth_node, "nc");
    cJSON *auth_response = cJSON_GetObjectItem(auth_node, "response");
    cJSON *auth_algorithm = cJSON_GetObjectItem(auth_node, "algorithm");

    if (!auth_realm || !auth_username || !auth_nonce || !auth_cnonce || !auth_nc || !auth_response)
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Missing auth fields");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    if (!JSON_IS_STRING(auth_realm) || !JSON_IS_STRING(auth_username) || !JSON_IS_STRING(auth_response))
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Invalid auth field types");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    if (auth_algorithm && !JSON_IS_STRING(auth_algorithm))
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Invalid algorithm type");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    const char *algo = auth_algorithm ? auth_algorithm->valuestring : NULL;
    if (algo && strcmp(algo, "SHA-256") != 0)
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Unsupported algorithm: %s", algo);
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    const char *auth_username_str = auth_username->valuestring;
    const char *auth_realm_str = auth_realm->valuestring;
    if (!auth_username_str || !auth_realm_str)
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Invalid username/realm");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    char nonce_buf[RPC_MAX_AUTH_NONCE_LEN + 1];
    char cnonce_buf[RPC_MAX_AUTH_NONCE_LEN + 1];
    const char *nonce = NULL;
    const char *cnonce = NULL;
    if (!rpcAuthNodeToString(auth_nonce, nonce_buf, sizeof(nonce_buf), &nonce) ||
            !rpcAuthNodeToString(auth_cnonce, cnonce_buf, sizeof(cnonce_buf), &cnonce))
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Invalid nonce/cnonce");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    /* 仅接受数字形式的 nc（JSON Number），不再支持 8 位十六进制字符串 */
    char nc_buf[32];
    const char *nc_str = NULL;
    uint32_t nc_val = 0;
    if (JSON_IS_NUMBER(auth_nc))
    {
        double nc_num = auth_nc->valuedouble;
        if (!isfinite(nc_num) || nc_num < 0)
        {
            LOG_W(tag, "snfRpcValidateAndAuth: Invalid nc value");
            return SNF_RPC_ERR_ACCESS_DENIED;
        }
        double nc_int = 0.0;
        if (modf(nc_num, &nc_int) != 0.0)
        {
            LOG_W(tag, "snfRpcValidateAndAuth: Invalid nc value");
            return SNF_RPC_ERR_ACCESS_DENIED;
        }
        nc_val = (uint32_t)nc_int;
        /* 使用不补零的十进制字符串参与摘要计算 */
        int written = snprintf(nc_buf, sizeof(nc_buf), "%u", nc_val);
        if (written <= 0 || written >= (int)sizeof(nc_buf))
        {
            LOG_W(tag, "snfRpcValidateAndAuth: Invalid nc formatting");
            return SNF_RPC_ERR_ACCESS_DENIED;
        }
        nc_str = nc_buf;
    }
    else
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Invalid nc type (only JSON number supported)");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    /* 读取本地认证配置 */
    char stored_realm[RPC_MAX_REALM_LEN];
    char stored_username[RPC_MAX_USERNAME_LEN];
    char stored_ha1[SNF_RPC_AUTH_SHA256_HEX_LEN + 1];
    bool stored_ha1_valid = false;
    xSemaphoreTake(bus->auth_ctx.auth_mutex, portMAX_DELAY);
    strncpy(stored_realm, bus->auth_ctx.realm, sizeof(stored_realm) - 1);
    stored_realm[sizeof(stored_realm) - 1] = '\0';
    strncpy(stored_username, bus->auth_ctx.username, sizeof(stored_username) - 1);
    stored_username[sizeof(stored_username) - 1] = '\0';
    strncpy(stored_ha1, bus->auth_ctx.ha1, sizeof(stored_ha1) - 1);
    stored_ha1[sizeof(stored_ha1) - 1] = '\0';
    stored_ha1_valid = bus->auth_ctx.ha1_valid;
    xSemaphoreGive(bus->auth_ctx.auth_mutex);

    if (stored_realm[0] == '\0')
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Realm not configured");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    if (strcmp(auth_username_str, stored_username) != 0)
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Username mismatch (got=%s, expected=%s)",
              auth_username_str, stored_username[0] ? stored_username : "<empty>");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    if (strcmp(auth_realm_str, stored_realm) != 0)
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Realm mismatch");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    if (!stored_ha1_valid || stored_ha1[0] == '\0')
    {
        LOG_W(tag, "snfRpcValidateAndAuth: HA1 not configured");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    /* HA2 使用 RPC 方法名：按第一个点拆成“命名空间:方法”；无点时使用“方法名:”。 */
    char ha2_input[RPC_MAX_METHOD_NAME_LEN + 2];
    int written = 0;
    const char *dot = strchr(method_name, '.');
    if (dot)
    {
        int prefix_len = (int)(dot - method_name);
        if (prefix_len < 0 || prefix_len >= (int)sizeof(ha2_input))
        {
            LOG_E(tag, "snfRpcValidateAndAuth: Invalid method name");
            return SNF_RPC_ERR_INVALID_REQUEST;
        }
        written = snprintf(ha2_input, sizeof(ha2_input), "%.*s:%s", prefix_len, method_name, dot + 1);
    }
    else
    {
        written = snprintf(ha2_input, sizeof(ha2_input), "%s:", method_name);
    }
    if (written < 0 || written >= (int)sizeof(ha2_input))
    {
        LOG_E(tag, "snfRpcValidateAndAuth: HA2 input overflow");
        return SNF_RPC_ERR_INTERNAL;
    }

    char ha2[65];
    if (rpcAuthComputeSha256Hex(ha2_input, ha2) != SNF_RPC_OK)
    {
        return SNF_RPC_ERR_INTERNAL;
    }

    /* response = SHA256(HA1:nonce:nc:cnonce:auth:HA2)，nc 使用不补零的十进制字符串。 */
    char response_input[512];
    written = snprintf(response_input, sizeof(response_input), "%s:%s:%s:%s:auth:%s",
                       stored_ha1, nonce, nc_str, cnonce, ha2);
    if (written < 0 || written >= (int)sizeof(response_input))
    {
        LOG_E(tag, "snfRpcValidateAndAuth: Response input overflow");
        return SNF_RPC_ERR_INTERNAL;
    }

    char computed_response[65];
    if (rpcAuthComputeSha256Hex(response_input, computed_response) != SNF_RPC_OK)
    {
        return SNF_RPC_ERR_INTERNAL;
    }

    const char *auth_response_str = auth_response->valuestring;
    if (!auth_response_str || strcmp(auth_response_str, computed_response) != 0)
    {
        LOG_W(tag, "snfRpcValidateAndAuth: Response mismatch");
        return SNF_RPC_ERR_ACCESS_DENIED;
    }

    /* 摘要匹配后检查计数；这里只跟踪最近一次 nonce，同一 nonce 的 nc 必须递增。 */
    xSemaphoreTake(bus->auth_ctx.auth_mutex, portMAX_DELAY);
    if (bus->auth_ctx.last_nonce_valid && strncmp(nonce, bus->auth_ctx.last_nonce, RPC_MAX_AUTH_NONCE_LEN) == 0)
    {
        if (bus->auth_ctx.last_nc_valid && nc_val <= bus->auth_ctx.last_nc)
        {
            xSemaphoreGive(bus->auth_ctx.auth_mutex);
            LOG_W(tag, "snfRpcValidateAndAuth: nc replay detected");
            return SNF_RPC_ERR_ACCESS_DENIED;
        }
    }

    /* 更新最近 nonce 和 nc */
    strncpy(bus->auth_ctx.last_nonce, nonce, RPC_MAX_AUTH_NONCE_LEN);
    bus->auth_ctx.last_nonce[RPC_MAX_AUTH_NONCE_LEN] = '\0';

    bus->auth_ctx.last_nonce_valid = true;
    bus->auth_ctx.last_nc = nc_val;
    bus->auth_ctx.last_nc_valid = true;
    xSemaphoreGive(bus->auth_ctx.auth_mutex);

    LOG_I(tag, "Authentication successful for user: %s", auth_username_str);

    return SNF_RPC_OK;
}
