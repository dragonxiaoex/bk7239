/**
 * @file    sonoff_ota.h
 * @brief   OTA升级模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-27
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_OTA_SONOFF_OTA_H__
#define __SONOFF_OTA_SONOFF_OTA_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA接口返回值.
 */
#define SNF_OTA_OK                     (0)      /* 成功 */
#define SNF_OTA_ERR_INVALID_PARAM      (-1)     /* 参数错误 */
#define SNF_OTA_ERR_STATE              (-2)     /* 状态错误 */
#define SNF_OTA_ERR_BUSY               (-3)     /* 接口忙 */
#define SNF_OTA_ERR_QUEUE_FULL         (-4)     /* 事件队列已满 */
#define SNF_OTA_ERR_NO_MEMORY          (-5)     /* 资源不足 */

/**
 * @brief OTA任务状态.
 */
typedef enum
{
    SNF_OTA_STATE_IDLE = 0, /**< OTA空闲. */
    SNF_OTA_STATE_RECEIVING, /**< 接收数据中. */
    SNF_OTA_STATE_VERIFY_SUCCESS, /**< 镜像校验成功. */
    SNF_OTA_STATE_VERIFY_FAILED, /**< 镜像校验失败. */
    SNF_OTA_STATE_APPLY, /**< 正在设置启动分区. */
    SNF_OTA_STATE_ABORT, /**< OTA已中止. */
    SNF_OTA_STATE_FAILED, /**< OTA处理失败. */
    SNF_OTA_STATE_SUCCESS, /**< 成功 */
} SnfOtaState;

/**
 * @brief 镜像校验类型.
 */
typedef enum
{
    SNF_OTA_CHECK_NONE = 0, /**< 不执行内置镜像校验. */
    SNF_OTA_CHECK_SHA256, /**< 预留SHA256校验. */
} SnfOtaCheckType;

/**
 * @brief 镜像加密类型.
 */
typedef enum
{
    SNF_OTA_CIPHER_NONE = 0, /**< 镜像未加密. */
} SnfOtaCipherType;

/**
 * @brief OTA事件错误码.
 */
typedef enum
{
    SNF_OTA_ERROR_NONE = 0, /**< 无错误. */
    SNF_OTA_ERROR_INVALID_PARAM, /**< 参数无效. */
    SNF_OTA_ERROR_UNSUPPORTED, /**< 功能不支持. */
    SNF_OTA_ERROR_TIMEOUT, /**< 接收数据超时. */
    SNF_OTA_ERROR_ERASE_FAILED, /**< 擦除失败. */
    SNF_OTA_ERROR_WRITE_FAILED, /**< 写入失败. */
    SNF_OTA_ERROR_IMAGE_INCOMPLETE, /**< 镜像数据不完整. */
    SNF_OTA_ERROR_CHECK_FAILED, /**< 镜像校验失败. */
    SNF_OTA_ERROR_APPLY_FAILED, /**< 设置启动分区失败. */
    SNF_OTA_ERROR_ABORTED, /**< OTA被中止. */
} SnfOtaErrorCode;

/** @brief 镜像校验值最大长度. */
#define SNF_OTA_CHECK_VALUE_SIZE       (32U)

/**
 * @brief 镜像信息.
 */
typedef struct
{
    uint32_t size; /**< 镜像字节长度. */
    uint32_t version; /**< 镜像版本. */
    uint8_t check_type; /**< 镜像校验类型. */
    uint8_t check[SNF_OTA_CHECK_VALUE_SIZE]; /**< 镜像校验值, 格式由check_type定义. */
    uint8_t cipher_type; /**< 镜像加密类型. */
} SnfOtaImageInfo;

/**
 * @brief OTA事件数据.
 */
typedef struct
{
    uint32_t received_size; /**< 已接收镜像字节长度. */
    uint32_t total_size; /**< 镜像总字节长度. */
    uint8_t percent; /**< 接收进度, 范围为0到100. */
    uint8_t error_code; /**< 事件错误码. */
} SnfOtaEventData;

/**
 * @brief OTA状态事件回调函数类型.
 *
 * 回调在OTA任务上下文中同步执行，event_data仅在回调执行期间有效。
 *
 * @param [in] state - OTA新状态.
 * @param [in] event_data - 状态事件数据.
 */
typedef void (*SnfOtaEventCallback)(SnfOtaState state,
                                    const SnfOtaEventData *event_data);

/**
 * @brief OTA任务配置.
 */
typedef struct
{
    SnfOtaImageInfo image_info; /**< 镜像信息. */
    SnfOtaEventCallback event_callback; /**< OTA状态事件回调. */
} SnfOtaConfig;

/**
 * @brief 启动OTA任务.
 *
 * @param [in] ota_config - OTA任务配置，配置内容会被模块复制.
 * @return 0表示请求成功，负数表示请求未发送.
 */
int snfOtaStart(const SnfOtaConfig *ota_config);

/**
 * @brief 异步写入一段OTA镜像数据.
 *
 * data不会被模块复制，调用者必须保持其内容不变，直到收到本段数据对应的
 * 状态回调。数据必须按offset递增、无间隙地提交。
 *
 * @param [in] offset - 镜像内偏移量.
 * @param [in] data - 待写入数据.
 * @param [in] len - 数据长度.
 * @return 0表示请求成功，负数表示请求未发送.
 */
int snfOtaWrite(uint32_t offset, const uint8_t *data, uint32_t len);

/**
 * @brief 请求OTA任务设置启动分区并重启.
 *
 * 只有收到SNF_OTA_STATE_VERIFY_SUCCESS后才能调用本接口。
 *
 * @return 0表示请求成功，负数表示请求未发送.
 */
int snfOtaApply(void);

/**
 * @brief 请求中止当前OTA任务.
 *
 * @return 0表示请求成功，负数表示请求未发送.
 */
int snfOtaAbort(void);

#ifdef __cplusplus
}
#endif

#endif /* __SONOFF_OTA_SONOFF_OTA_H__ */
