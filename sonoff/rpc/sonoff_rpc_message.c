/**
 * @file    sonoff_rpc_message.c
 * @brief   RPC 消息解析和处理实现
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-11
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

#include "sonoff_log.h"
#include "sonoff_rpc_internal.h"

static const char *tag = "SNF-RPC-M";

/**
 * @brief 获取用于日志输出的传输类型名称.
 */
static const char *rpcTransportTypeToStr(uint8_t type)
{
    switch (type)
    {
    case SNF_RPC_TRANSPORT_LOCAL:
        return "local";
    case SNF_RPC_TRANSPORT_SERIAL:
        return "serial";
    case SNF_RPC_TRANSPORT_UDP:
        return "udp";
    case SNF_RPC_TRANSPORT_HTTP:
        return "http";
    case SNF_RPC_TRANSPORT_WEBSOCKET:
        return "ws";
    case SNF_RPC_TRANSPORT_MQTT:
        return "mqtt";
    default:
        return "unknown";
    }
}

/**
 * @brief 在原响应超限时尝试发送短错误响应.
 */
static void rpcSendResponseTooLargeError(SnfRpcChannelNode *channel, cJSON *id_node, bool force_null_id)
{
    cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_NO_MEMORY, "Response too large");
    cJSON *response = snfRpcCreateResponse(channel, id_node, force_null_id, NULL, error);
    if (response)
    {
        int32_t ret = snfRpcSendJsonMessage(channel, response);
        if (ret != SNF_RPC_OK)
        {
            LOG_W(tag, "snfRpcProcessRequest: Failed to send size-error response, ret=%d", ret);
        }
        cJSON_Delete(response);
    }
    else if (error)
    {
        cJSON_Delete(error);
    }
}

/**
 * @brief 判断发送失败是否由报文长度超限引起.
 */
static bool rpcIsTooLargeSendError(int32_t ret)
{
    return (ret == SNF_RPC_ERR_NO_MEMORY || ret == SNF_RPC_ERR_INVALID_SIZE);
}

/**
 * @brief 校验并执行单条请求，返回由调用方释放的完整响应.
 */
static cJSON *rpcBuildSingleResponse(SnfRpcChannelNode *channel, cJSON *request_obj, SnfRpcBus *bus)
{
    if (!JSON_IS_OBJECT(request_obj))
    {
        snfRpcStatsInc(&bus->stats.invalid_request);
        cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_REQUEST, "Invalid request");
        cJSON *response = snfRpcCreateResponse(channel, NULL, true, NULL, error);
        if (!response && error)
        {
            cJSON_Delete(error);
        }
        return response;
    }

    /* 获取必需字段 */
    cJSON *jsonrpc_node = cJSON_GetObjectItem(request_obj, "jsonrpc");
    cJSON *method_node = cJSON_GetObjectItem(request_obj, "method");
    cJSON *id_node = cJSON_GetObjectItem(request_obj, "id");
    cJSON *params_node = cJSON_GetObjectItem(request_obj, "params");

    /* Validate jsonrpc version if present. Do not require field to exist. */
    if (jsonrpc_node)
    {
        if (!JSON_IS_STRING(jsonrpc_node))
        {
            snfRpcStatsInc(&bus->stats.invalid_request);
            cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_REQUEST, "Invalid request");
            cJSON *response = snfRpcCreateResponse(channel, NULL, true, NULL, error);
            if (!response && error)
            {
                cJSON_Delete(error);
            }
            return response;
        }

        const char *jsonrpc_str = jsonrpc_node->valuestring;
        if (!jsonrpc_str || strcmp(jsonrpc_str, "2.0") != 0)
        {
            snfRpcStatsInc(&bus->stats.invalid_request);
            cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_REQUEST, "Invalid request");
            cJSON *response = snfRpcCreateResponse(channel, NULL, true, NULL, error);
            if (!response && error)
            {
                cJSON_Delete(error);
            }
            return response;
        }
    }

    bool id_is_number = JSON_IS_NUMBER(id_node);
    bool id_is_null = JSON_IS_NULL(id_node);

    if (id_node && !id_is_number && !id_is_null)
    {
        snfRpcStatsInc(&bus->stats.invalid_request);
        cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_REQUEST, "Invalid request");
        cJSON *response = snfRpcCreateResponse(channel, NULL, true, NULL, error);
        if (!response && error)
        {
            cJSON_Delete(error);
        }
        return response;
    }

    /* Validate method field */
    if (!JSON_IS_STRING(method_node))
    {
        snfRpcStatsInc(&bus->stats.invalid_request);
        cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_REQUEST, "Invalid request");
        bool force_null_id = (id_node == NULL || !id_is_number);
        cJSON *response = snfRpcCreateResponse(channel, id_is_number ? id_node : NULL, force_null_id, NULL, error);
        if (!response && error)
        {
            cJSON_Delete(error);
        }
        return response;
    }

    const char *method_name = method_node->valuestring;
    if (!method_name || strlen(method_name) >= RPC_MAX_METHOD_NAME_LEN)
    {
        snfRpcStatsInc(&bus->stats.invalid_request);
        cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_REQUEST, "Invalid request");
        bool force_null_id = (id_node == NULL || !id_is_number);
        cJSON *response = snfRpcCreateResponse(channel, id_is_number ? id_node : NULL, force_null_id, NULL, error);
        if (!response && error)
        {
            cJSON_Delete(error);
        }
        return response;
    }

    bool can_respond = id_is_number;

    /* Validate params */
    if (params_node && !JSON_IS_OBJECT(params_node) && !JSON_IS_ARRAY(params_node))
    {
        if (id_is_number)
        {
            snfRpcStatsInc(&bus->stats.invalid_params);
            cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_PARAMS, "Invalid params");
            cJSON *response = snfRpcCreateResponse(channel, id_node, false, NULL, error);
            if (!response && error)
            {
                cJSON_Delete(error);
            }
            return response;
        }
        return NULL;
    }

    /* Validate and authenticate request */
    int32_t auth_ret = snfRpcValidateAndAuth(request_obj, channel);
    if (auth_ret != SNF_RPC_OK)
    {
        const char *error_msg = NULL;
        int error_code = SNF_RPC_ERR_INVALID_REQUEST;
        if (auth_ret == SNF_RPC_ERR_ACCESS_DENIED)
        {
            error_code = SNF_RPC_ERR_ACCESS_DENIED;
            error_msg = "Access denied";
        }
        else if (auth_ret == SNF_RPC_ERR_NO_PASSWORD)
        {
            error_code = SNF_RPC_ERR_NO_PASSWORD;
            error_msg = "No password configured, access denied";
        }
        else
        {
            error_code = SNF_RPC_ERR_INVALID_REQUEST;
            error_msg = "Invalid request";
        }
        char *challenge_str = NULL;

        if (auth_ret == SNF_RPC_ERR_ACCESS_DENIED && channel->transport_type == SNF_RPC_TRANSPORT_WEBSOCKET)
        {
            SnfRpcAuthChallenge challenge;
            if (snfRpcAuthGetChallenge(&challenge, true) == SNF_RPC_OK)
            {
                cJSON *challenge_obj = cJSON_CreateObject();
                if (challenge_obj)
                {
                    cJSON_AddStringToObject(challenge_obj, "auth_type", "digest");
                    cJSON_AddStringToObject(challenge_obj, "nonce", challenge.nonce);
                    cJSON_AddNumberToObject(challenge_obj, "nc", (double)challenge.nc);
                    cJSON_AddStringToObject(challenge_obj, "realm", challenge.realm);
                    cJSON_AddStringToObject(challenge_obj, "algorithm", "SHA-256");
                    challenge_str = cJSON_PrintUnformatted(challenge_obj);
                    cJSON_Delete(challenge_obj);
                    if (challenge_str)
                    {
                        /* 外层响应由 cJSON 统一转义此 JSON 字符串。 */
                        error_code = 401;
                        error_msg = challenge_str;
                    }
                }
            }
        }

        cJSON *response = NULL;
        if (id_is_number)
        {
            cJSON *error = snfRpcCreateErrorObject(error_code, error_msg);
            response = snfRpcCreateResponse(channel, id_node, false, NULL, error);
            if (!response && error)
            {
                cJSON_Delete(error);
            }
            snfRpcStatsInc(&bus->stats.auth_failed);
        }

        if (challenge_str)
        {
            free(challenge_str);
        }

        return response;
    }

    /* 查找方法处理器 */
    SnfRpcMethodNode *method = snfRpcMethodFind(method_name);
    if (!method)
    {
        if (id_is_number)
        {
            snfRpcStatsInc(&bus->stats.method_not_found);
            cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_METHOD_NOT_FOUND, "Method not found");
            cJSON *response = snfRpcCreateResponse(channel, id_node, false, NULL, error);
            if (!response && error)
            {
                cJSON_Delete(error);
            }
            return response;
        }
        return NULL;
    }

    /* 如果方法标记为内部方法，但通道不允许内部方法访问，则视为未找到 */
    if ((method->flags & RPC_METHOD_FLAG_INTERNAL) && (!channel || !channel->allow_internal_methods))
    {
        if (id_is_number)
        {
            snfRpcStatsInc(&bus->stats.method_not_found);
            cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_METHOD_NOT_FOUND, "Method not found");
            cJSON *response = snfRpcCreateResponse(channel, id_node, false, NULL, error);
            if (!response && error)
            {
                cJSON_Delete(error);
            }
            return response;
        }
        return NULL;
    }

    cJSON *result = NULL;
    cJSON *error = NULL;

    LOG_I(tag, "[RPC] handle %s form %s", method_name, rpcTransportTypeToStr(channel->transport_type));
    if (method->handler)
    {
        /* 在当前任务登记通道上下文，供 handler 查询请求来源，调用结束后撤销。 */
        (void)snfRpcCurrentRequestPush(channel);
        result = method->handler(method_name, params_node, method->user_ctx);
        snfRpcCurrentRequestPop();
    }

    /*
     * handler 返回的是由 RPC 接管的包装对象，result 此时尚不是业务结果节点。
     * 子节点仍属于包装对象，须先深拷贝为独立节点，再删除包装对象并构造完整响应。
     */
    {
        bool has_result = false;
        bool has_error = false;
        cJSON *result_node = NULL;
        cJSON *error_node = NULL;

        if (JSON_IS_OBJECT(result))
        {
            result_node = cJSON_GetObjectItem(result, "result");
            error_node = cJSON_GetObjectItem(result, "error");
            has_result = (result_node != NULL);
            has_error = (error_node != NULL);
        }

        if (has_result && !has_error)
        {
            cJSON *unwrapped_result = cJSON_Duplicate(result_node, true);
            if (!unwrapped_result)
            {
                if (result)
                {
                    cJSON_Delete(result);
                    result = NULL;
                }
                error = snfRpcCreateErrorObject(SNF_RPC_ERR_INTERNAL, "Internal error");
            }
            else
            {
                cJSON_Delete(result);
                result = unwrapped_result;
            }
        }
        else if (!has_result && has_error)
        {
            cJSON *unwrapped_error = cJSON_Duplicate(error_node, true);
            if (!unwrapped_error)
            {
                if (result)
                {
                    cJSON_Delete(result);
                    result = NULL;
                }
                error = snfRpcCreateErrorObject(SNF_RPC_ERR_INTERNAL, "Internal error");
            }
            else
            {
                cJSON_Delete(result);
                result = NULL;
                error = unwrapped_error;
            }
        }
        else
        {
            if (result)
            {
                cJSON_Delete(result);
                result = NULL;
            }
            error = snfRpcCreateErrorObject(SNF_RPC_ERR_INTERNAL,
                                            "Invalid handler response: exactly one of top-level 'result' or 'error' is required");
        }
    }

    /* 通知也会执行 handler，但没有可关联的请求 ID，因此只释放执行结果，不发送响应。 */
    if (!can_respond)
    {
        if (result)
        {
            cJSON_Delete(result);
        }
        if (error)
        {
            cJSON_Delete(error);
        }
        return NULL;
    }

    cJSON *response = snfRpcCreateResponse(channel, id_node, false, result, error);
    if (!response)
    {
        /* 构造失败未发生所有权转移，独立的 result/error 仍由当前调用方释放。 */
        if (result)
        {
            cJSON_Delete(result);
        }
        if (error)
        {
            cJSON_Delete(error);
        }
    }

    return response;
}

void snfRpcProcessRequest(SnfRpcChannelNode *channel, const char *json_str, uint32_t json_len)
{
    LOG_I(tag, "snfRpcProcessRequest");

    if (!channel || !json_str)
    {
        return;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();

    /* 解析 JSON 请求 */
    cJSON *request_obj = cJSON_Parse(json_str);
    if (!request_obj)
    {
        LOG_W(tag, "snfRpcProcessRequest: Failed to parse JSON");

        /* Send error response */
        cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_PARSE_ERROR, "JSON parse error");
        snfRpcStatsInc(&bus->stats.invalid_request);
        cJSON *response = snfRpcCreateResponse(channel, NULL, true, NULL, error);

        if (response)
        {
            snfRpcSendJsonMessage(channel, response);
            cJSON_Delete(response);
        }
        else if (error)
        {
            cJSON_Delete(error);
        }

        return;
    }

    if (JSON_IS_ARRAY(request_obj))
    {
        int item_count = cJSON_GetArraySize(request_obj);
        if (item_count <= 0)
        {
            snfRpcStatsInc(&bus->stats.invalid_request);
            cJSON *error = snfRpcCreateErrorObject(SNF_RPC_ERR_INVALID_REQUEST, "Invalid request");
            cJSON *response = snfRpcCreateResponse(channel, NULL, true, NULL, error);
            if (response)
            {
                snfRpcSendJsonMessage(channel, response);
                cJSON_Delete(response);
            }
            else if (error)
            {
                cJSON_Delete(error);
            }
            cJSON_Delete(request_obj);
            return;
        }

        cJSON *batch_response = cJSON_CreateArray();
        if (!batch_response)
        {
            cJSON_Delete(request_obj);
            return;
        }

        /* 通知不进入响应数组；加入数组的响应由 batch_response 统一持有和释放。 */
        for (int i = 0; i < item_count; i++)
        {
            cJSON *item = cJSON_GetArrayItem(request_obj, i);
            cJSON *item_response = rpcBuildSingleResponse(channel, item, bus);
            if (item_response)
            {
                cJSON_AddItemToArray(batch_response, item_response);
            }
        }

        if (cJSON_GetArraySize(batch_response) > 0)
        {
            int32_t send_ret = snfRpcSendJsonMessage(channel, batch_response);
            if (send_ret != SNF_RPC_OK)
            {
                LOG_W(tag,
                      "snfRpcProcessRequest: Failed to send batch response (ret=%d), falling back to per-item send",
                      send_ret);
                if (rpcIsTooLargeSendError(send_ret))
                {
                    /* 批量响应超限时尝试逐条发送；single 借用数组子节点，不单独删除。 */
                    int resp_count = cJSON_GetArraySize(batch_response);
                    for (int i = 0; i < resp_count; i++)
                    {
                        cJSON *single = cJSON_GetArrayItem(batch_response, i);
                        if (!single)
                        {
                            continue;
                        }
                        int32_t item_ret = snfRpcSendJsonMessage(channel, single);
                        if (item_ret != SNF_RPC_OK)
                        {
                            LOG_W(tag, "snfRpcProcessRequest: Failed to send batch item[%d], ret=%d", i, item_ret);
                            if (rpcIsTooLargeSendError(item_ret))
                            {
                                cJSON *resp_id = cJSON_GetObjectItem(single, "id");
                                bool is_null_id = (!resp_id || JSON_IS_NULL(resp_id));
                                rpcSendResponseTooLargeError(channel, is_null_id ? NULL : resp_id, is_null_id);
                            }
                        }
                    }
                }
            }
        }

        cJSON_Delete(batch_response);
        cJSON_Delete(request_obj);
        return;
    }

    cJSON *id_node = cJSON_GetObjectItem(request_obj, "id");
    bool id_is_number = JSON_IS_NUMBER(id_node);

    cJSON *response = rpcBuildSingleResponse(channel, request_obj, bus);
    if (response)
    {
        int32_t send_ret = snfRpcSendJsonMessage(channel, response);
        if (send_ret != SNF_RPC_OK)
        {
            LOG_W(tag, "snfRpcProcessRequest: Failed to send response, ret=%d", send_ret);
            if (rpcIsTooLargeSendError(send_ret))
            {
                rpcSendResponseTooLargeError(channel, id_is_number ? id_node : NULL, !id_is_number);
            }
        }
        cJSON_Delete(response);
    }

    cJSON_Delete(request_obj);
}

cJSON *snfRpcCall(const char *method, cJSON *params, uint32_t timeout_ms)
{
    LOG_I(tag, "snfRpcCall: method=%s, timeout=%u", method ? method : "NULL", timeout_ms);

    if (!method)
    {
        return NULL;
    }

    SnfRpcBus *bus = snfRpcBusGetInstance();
    if (!bus->initialized)
    {
        LOG_E(tag, "snfRpcCall: RPC not initialized");
        return NULL;
    }

    SnfRpcMethodNode *m = snfRpcMethodFind(method);
    if (!m || !m->handler)
    {
        return NULL;
    }

    cJSON *result = m->handler(method, params, m->user_ctx);
    if (!result)
    {
        return NULL;
    }

    bool has_result = false;
    bool has_error = false;
    cJSON *result_node = NULL;
    cJSON *error_node = NULL;

    if (JSON_IS_OBJECT(result))
    {
        result_node = cJSON_GetObjectItem(result, "result");
        error_node = cJSON_GetObjectItem(result, "error");
        has_result = (result_node != NULL);
        has_error = (error_node != NULL);
    }

    if (has_result && !has_error)
    {
        cJSON *unwrapped_result = cJSON_Duplicate(result_node, true);
        if (!unwrapped_result)
        {
            if (result)
            {
                cJSON_Delete(result);
            }
            return NULL;
        }
        cJSON_Delete(result);
        return unwrapped_result;
    }
    else if (!has_result && has_error)
    {
        cJSON *unwrapped_error = cJSON_Duplicate(error_node, true);
        if (!unwrapped_error)
        {
            if (result)
            {
                cJSON_Delete(result);
            }
            return NULL;
        }
        cJSON_Delete(result);
        return unwrapped_error;
    }
    else
    {
        if (result)
        {
            cJSON_Delete(result);
        }
        return NULL;
    }
}
