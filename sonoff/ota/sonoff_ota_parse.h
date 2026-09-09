/**
 * @file    sonoff_ota_parse.h
 * @brief   带封装的OTA包解析
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_OTA_PARSE_H__
#define __SONOFF_OTA_PARSE_H__

#include <stdint.h>

#include "sonoff_ota.h"
#include "sonoff_aes_gcm.h"

/** @brief 带封装的OTA包字段长度与结构版本. */
#define SNF_OTA_STRUCT_VERSION       1   /* 线上的结构版本 */
#define SNF_OTA_META_HEAD_SIZE       24  /* 元数据头长度 */
#define SNF_OTA_META_FILE_SIZE       76  /* 单个文件属性长度 */
#define SNF_OTA_MODEL_VERSION_SIZE   8   /* 联合版本字段长度 */
#define SNF_OTA_FILE_VERSION_SIZE    16  /* 文件版本字段长度 */
#define SNF_OTA_META_RESERVED_SIZE   9   /* 加密类型之后的预留字段长度 */
#define SNF_OTA_FILE_RESERVED_SIZE   12  /* 文件属性预留字段长度 */
#define SNF_OTA_DECRYPT_BUFFER_SIZE  512 /* 单次流式解密长度 */

/** @brief 已校验的OTA包元数据头. */
typedef struct
{
    uint8_t version;                                    /**< 结构版本. */
    uint8_t file_count;                                 /**< 文件个数. */
    char model_version[SNF_OTA_MODEL_VERSION_SIZE + 1]; /**< 联合版本字符串. */
    uint8_t cipher_type;                                /**< 加密类型，0未加密，1为AES-256-GCM. */
    uint8_t reserved[SNF_OTA_META_RESERVED_SIZE];       /**< 原样保留的预留字段，参与头部CRC. */
    uint32_t crc;                                       /**< 元数据头CRC32. */
} SnfOtaMetadata;

/** @brief 已校验的文件属性，字符串均补充结束符. */
typedef struct
{
    char name[SNF_OTA_FILE_NAME_SIZE + 1];        /**< 文件名. */
    char version[SNF_OTA_FILE_VERSION_SIZE + 1];  /**< 文件版本字符串. */
    uint32_t offset;                              /**< 相对带封装的OTA包起点的偏移. */
    uint32_t size;                                /**< 包内文件长度，加密时含IV和认证标签. */
    uint32_t crc;                                 /**< 加密前完整文件的CRC32. */
    uint32_t attributes_crc;                      /**< 文件属性CRC32. */
    uint8_t reserved[SNF_OTA_FILE_RESERVED_SIZE]; /**< 原样保留的预留字段，不参与属性CRC. */
} SnfOtaFileInfo;

/** @brief 解析输出的一段OTA分区数据. */
typedef struct
{
    const uint8_t *data; /**< 借用输入或内部解密缓冲区，下次解析前有效. */
    uint32_t offset;     /**< OTA分区内偏移. */
    uint32_t size;       /**< 待写入长度，纯头部时为0. */
} SnfOtaPayload;

/**
 * @brief OTA流解析上下文，由OTA任务独占，调用者不得直接修改成员.
 *
 * metadata和file在对应头部校验成功后可供读取，其余成员仅用于跨分块解析。
 */
typedef struct
{
    SnfOtaMetadata metadata;                                      /**< 公司外层封装信息. */
    SnfOtaFileInfo file;                                          /**< 选中的文件. */
    SnfAesGcmCtx gcm;                                             /**< 流式解密上下文. */
    uint8_t aad[SNF_OTA_META_HEAD_SIZE + SNF_OTA_META_FILE_SIZE]; /**< 元数据头与选中文件属性的原始字节. */
    uint8_t plain[SNF_OTA_DECRYPT_BUFFER_SIZE];                   /**< 解密缓冲区. */
    char file_name[SNF_OTA_FILE_NAME_SIZE + 1];                   /**< 目标文件名. */
    uint8_t buffer[SNF_OTA_META_FILE_SIZE];                       /**< 外层头部、IV和认证标签的跨块缓存. */
    uint32_t package_size;                                        /**< 输入包总长度. */
    uint32_t offset;                                              /**< 已解析的输入长度. */
    uint32_t previous_end;                                        /**< 前一个文件的结束位置. */
    uint32_t file_crc;                                            /**< 封装内目标文件累计CRC. */
    uint32_t flash_size;                                          /**< OTA分区容量. */
    uint16_t buffered_size;                                       /**< 已缓存的头部长度. */
    uint8_t file_index;                                           /**< 已解析的文件属性个数. */
    uint8_t stage;                                                /**< 内部解析阶段. */
    SnfOtaErrorCode error;                                        /**< 首次解析错误. */
} SnfOtaParseContext;

/**
 * @brief 初始化公司外层OTA包的顺序解析
 *
 * 文件表须按偏移递增且互不重叠，按文件名提取完整文件，不解析镜像内部格式。
 * 其他文件仅检查属性和范围，不执行协同固件分发。重用前须调用snfOtaParseFree。
 * 文件名按SNF_OTA_FILE_NAME_SIZE字节复制，调用方须保证缓冲区至少有这些可读字节。
 *
 * @param [out] context - 解析上下文.
 * @param [in] package_size - 完整输入包长度，不含Matter头.
 * @param [in] file_name - 带封装的OTA包目标文件名，必须非NULL且首字节非0.
 * @param [in] flash_size - OTA暂存分区容量.
 * @return OTA错误码，SNF_OTA_ERROR_NONE表示成功.
 */
SnfOtaErrorCode snfOtaParseInit(SnfOtaParseContext *context, uint32_t package_size,
                                const char *file_name, uint32_t flash_size);

/**
 * @brief 处理当前阶段的数据并输出可写入载荷
 *
 * 顺序调用直到本输入块全部处理。头部允许在任意字节处分块。
 * payload借用本次data或内部解密缓冲区，返回后应先写完载荷再处理下一段。
 * 解密数据仅可写入暂存区，snfOtaParseFinish成功前不得提交升级。
 *
 * @param [in,out] context - 解析上下文.
 * @param [in] data - 当前尚未处理的数据.
 * @param [in] size - 当前尚未处理的长度.
 * @param [out] consumed - 本次已处理长度，成功时必大于0.
 * @param [out] payload - 载荷视图，size为0时无需写入.
 * @return OTA错误码，SNF_OTA_ERROR_NONE表示成功.
 */
SnfOtaErrorCode snfOtaParseData(SnfOtaParseContext *context, const uint8_t *data, uint32_t size,
                                uint32_t *consumed, SnfOtaPayload *payload);

/**
 * @brief 检查外层包接收完整性及所选文件CRC
 *
 * @param [in] context - 解析上下文.
 * @return OTA错误码，SNF_OTA_ERROR_NONE表示成功.
 */
SnfOtaErrorCode snfOtaParseFinish(const SnfOtaParseContext *context);

/**
 * @brief 释放解析资源并清除会话，可用于完成、失败或中止
 *
 * @param [in,out] context - 已零初始化或已初始化的解析上下文，允许NULL.
 */
void snfOtaParseFree(SnfOtaParseContext *context);

#endif /* __SONOFF_OTA_PARSE_H__ */
