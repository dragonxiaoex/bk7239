/**
 * @file    sonoff_ota_parse.c
 * @brief   带封装的OTA包流式解析
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-09
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sonoff_crc32.h"
#include "sonoff_ota_parse.h"
#include <sonoff_ota_key.h>
#include "sonoff_log.h"

static const char *tag = "SNF-OTA-PARSE";

/** @brief 相对元数据头起点的字段偏移，单位为字节. */
#define OTA_HEAD_STRUCT_VERSION_OFFSET  0  /* 结构版本 */
#define OTA_HEAD_FILE_COUNT_OFFSET      1  /* 文件数量 */
#define OTA_HEAD_MODEL_VERSION_OFFSET   2  /* 联合版本 */
#define OTA_HEAD_CIPHER_TYPE_OFFSET     10 /* 加密类型 */
#define OTA_HEAD_RESERVED_OFFSET        11 /* 预留字段 */
#define OTA_HEAD_CRC_OFFSET             20 /* 元数据头CRC32 */

/** @brief 相对单个文件属性起点的字段偏移，单位为字节. */
#define OTA_FILE_NAME_OFFSET            0  /* 文件名 */
#define OTA_FILE_VERSION_OFFSET         32 /* 文件版本 */
#define OTA_FILE_DATA_OFFSET            48 /* 文件在包中的起始偏移 */
#define OTA_FILE_SIZE_OFFSET            52 /* 文件长度 */
#define OTA_FILE_CRC_OFFSET             56 /* 明文文件CRC32 */
#define OTA_FILE_ATTRIBUTES_CRC_OFFSET  60 /* 文件属性CRC32 */
#define OTA_FILE_RESERVED_OFFSET        64 /* 预留字段 */

/* 读取未对齐的32位整数，data须为至少4字节且无副作用的字节指针。 */
#define OTA_GET_BIG_ENDIAN_DATA_4B(data) \
    (((uint32_t)(data)[0] << 24) | ((uint32_t)(data)[1] << 16) \
        | ((uint32_t)(data)[2] << 8) | (uint32_t)(data)[3])

/** @brief 顺序解析阶段. */
typedef enum
{
    OTA_PARSE_METADATA = 0, /* 接收并校验公司包元数据头. */
    OTA_PARSE_FILE,         /* 逐项接收文件属性，校验并选择目标文件. */
    OTA_PARSE_FILE_GAP,     /* 跳过目标文件之前的数据，推进到文件起点. */
    OTA_PARSE_GCM_IV,       /* 接收目标文件的IV，启动流式解密. */
    OTA_PARSE_IMAGE,        /* 处理目标文件内容，按需解密并输出写入数据. */
    OTA_PARSE_GCM_TAG,      /* 接收目标文件末尾的TAG，校验整体认证结果. */
    OTA_PARSE_TAIL,         /* 跳过目标文件之后的数据，直到整个包处理完毕. */
} OtaParseStage;

/**
 * @brief 解析并校验24字节OTA包元数据头
 *
 * @param [in] data - 元数据头起点.
 * @param [in] size - 可用字节长度.
 * @param [out] metadata - 成功时输出解析结果.
 * @return OTA错误码，SNF_OTA_ERROR_NONE表示成功.
 */
static SnfOtaErrorCode otaParseMetadata(const uint8_t *data, uint32_t size, SnfOtaMetadata *metadata)
{
    uint32_t crc;

    if ((data == NULL) || (metadata == NULL))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    if (size < SNF_OTA_META_HEAD_SIZE)
    {
        return SNF_OTA_ERROR_IMAGE_INCOMPLETE;
    }

    if (data[OTA_HEAD_STRUCT_VERSION_OFFSET] != SNF_OTA_STRUCT_VERSION)
    {
        LOG_I(tag, "unsupported version");
        return SNF_OTA_ERROR_UNSUPPORTED;
    }

    crc = OTA_GET_BIG_ENDIAN_DATA_4B(&data[OTA_HEAD_CRC_OFFSET]);
    if ((snfCrc32(UINT32_MAX, data, OTA_HEAD_CRC_OFFSET) ^ UINT32_MAX) != crc)
    {
        LOG_I(tag, "metadata crc check error");
        return SNF_OTA_ERROR_CHECK_FAILED;
    }

    if ((data[OTA_HEAD_FILE_COUNT_OFFSET] == 0) || (data[OTA_HEAD_MODEL_VERSION_OFFSET] == 0))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    if ((data[OTA_HEAD_CIPHER_TYPE_OFFSET] != SNF_OTA_CIPHER_NONE)
        && (data[OTA_HEAD_CIPHER_TYPE_OFFSET] != SNF_OTA_CIPHER_AES_256_GCM))
    {
        LOG_I(tag, "unsupported cipher type");
        return SNF_OTA_ERROR_UNSUPPORTED;
    }

    memset(metadata, 0, sizeof(*metadata));
    metadata->version = data[OTA_HEAD_STRUCT_VERSION_OFFSET];
    metadata->file_count = data[OTA_HEAD_FILE_COUNT_OFFSET];
    memcpy(metadata->model_version, &data[OTA_HEAD_MODEL_VERSION_OFFSET], SNF_OTA_MODEL_VERSION_SIZE);
    metadata->cipher_type = data[OTA_HEAD_CIPHER_TYPE_OFFSET];
    memcpy(metadata->reserved, &data[OTA_HEAD_RESERVED_OFFSET], sizeof(metadata->reserved));
    metadata->crc = crc;

    return SNF_OTA_ERROR_NONE;
}

/**
 * @brief 解析并校验76字节OTA包文件属性
 *
 * @param [in] data - 文件属性起点.
 * @param [in] size - 可用字节长度.
 * @param [out] file - 成功时输出解析结果，范围由包解析器校验.
 * @return OTA错误码，SNF_OTA_ERROR_NONE表示成功.
 */
static SnfOtaErrorCode otaParseFile(const uint8_t *data, uint32_t size, SnfOtaFileInfo *file)
{
    uint32_t attributes_crc;

    if ((data == NULL) || (file == NULL))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    if (size < SNF_OTA_META_FILE_SIZE)
    {
        return SNF_OTA_ERROR_IMAGE_INCOMPLETE;
    }

    attributes_crc = OTA_GET_BIG_ENDIAN_DATA_4B(&data[OTA_FILE_ATTRIBUTES_CRC_OFFSET]);
    if ((snfCrc32(UINT32_MAX, data, OTA_FILE_ATTRIBUTES_CRC_OFFSET) ^ UINT32_MAX) != attributes_crc)
    {
        return SNF_OTA_ERROR_CHECK_FAILED;
    }

    if ((data[OTA_FILE_NAME_OFFSET] == 0) || (data[OTA_FILE_VERSION_OFFSET] == 0))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    memset(file, 0, sizeof(*file));
    memcpy(file->name, &data[OTA_FILE_NAME_OFFSET], SNF_OTA_FILE_NAME_SIZE);
    memcpy(file->version, &data[OTA_FILE_VERSION_OFFSET], SNF_OTA_FILE_VERSION_SIZE);
    file->offset = OTA_GET_BIG_ENDIAN_DATA_4B(&data[OTA_FILE_DATA_OFFSET]);
    file->size = OTA_GET_BIG_ENDIAN_DATA_4B(&data[OTA_FILE_SIZE_OFFSET]);
    file->crc = OTA_GET_BIG_ENDIAN_DATA_4B(&data[OTA_FILE_CRC_OFFSET]);
    file->attributes_crc = attributes_crc;
    memcpy(file->reserved, &data[OTA_FILE_RESERVED_OFFSET], sizeof(file->reserved));

    return SNF_OTA_ERROR_NONE;
}

/**
 * @brief 校验文件范围并选择目标文件
 *
 * @param [in,out] context - 已缓存完整文件属性的上下文.
 * @return OTA错误码.
 */
static SnfOtaErrorCode otaSelectFile(SnfOtaParseContext *context)
{
    SnfOtaFileInfo file = {0};
    SnfOtaErrorCode error;
    uint32_t overhead = (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
        ? SNF_AES_GCM_IV_SIZE + SNF_AES_GCM_TAG_SIZE : 0;

    error = otaParseFile(context->buffer, context->buffered_size, &file);
    if (error != SNF_OTA_ERROR_NONE)
    {
        return error;
    }

    if ((file.size <= overhead) || (file.offset < context->previous_end)
        || (file.offset > context->package_size) || (file.size > (context->package_size - file.offset)))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    if (strcmp(file.name, context->file_name) == 0)
    {
        /* size非0表示已选中同名文件，拒绝重复目标；明文长度不得超过OTA分区. */
        if ((context->file.size != 0) || ((file.size - overhead) > context->flash_size))
        {
            return SNF_OTA_ERROR_INVALID_PARAM;
        }

        context->file = file;
        memcpy(&context->auth_header_data[SNF_OTA_META_HEAD_SIZE], context->buffer, SNF_OTA_META_FILE_SIZE);
    }

    context->previous_end = file.offset + file.size;
    context->file_index++;
    context->buffered_size = 0;
    if (context->file_index == context->metadata.file_count)
    {
        if (context->file.size == 0)
        {
            return SNF_OTA_ERROR_FILE_NOT_FOUND;
        }

        context->stage = OTA_PARSE_FILE_GAP;
        if (context->offset == context->file.offset)
        {
            context->stage = (overhead != 0) ? OTA_PARSE_GCM_IV : OTA_PARSE_IMAGE;
        }
    }

    return SNF_OTA_ERROR_NONE;
}

/**
 * @brief 解密目标文件数据并生成可写入Flash的明文片段.
 *
 * @param [in,out] context - 解析上下文，当前须处于文件内容阶段.
 * @param [in] data - 本次文件数据.
 * @param [in] size - 当前阶段内可处理的字节数.
 * @param [out] payload - 明文片段，加密时长度受解密缓冲区限制.
 * @return OTA错误码，SNF_OTA_ERROR_NONE表示成功.
 */
static SnfOtaErrorCode otaParsePayload(SnfOtaParseContext *context, const uint8_t *data,
                                     uint32_t size, SnfOtaPayload *payload)
{
    const uint8_t *image_data = data;
    uint32_t offset = context->offset - context->file.offset;

    if (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
    {
        if (size > sizeof(context->decrypt_buffer))
        {
            size = sizeof(context->decrypt_buffer);
        }

        if (snfAesGcmDecryptUpdate(&context->gcm, data, size, context->decrypt_buffer) != SNF_AES_GCM_OK)
        {
            return SNF_OTA_ERROR_DECRYPT_FAILED;
        }
        image_data = context->decrypt_buffer;
        offset -= SNF_AES_GCM_IV_SIZE;
    }

    /* 外层CRC覆盖完整明文文件，文件内容原样交给平台。 */
    context->file_crc = snfCrc32(context->file_crc, image_data, size);
    payload->data = image_data;
    payload->offset = offset;
    payload->size = size;

    return SNF_OTA_ERROR_NONE;
}

/**
 * @brief 收齐当前阶段数据后执行校验并推进解析状态.
 *
 * @param [in,out] context - 已更新处理进度的解析上下文，失败时记录错误.
 * @param [in] header_size - 当前待收字段长度，文件内容或跳过阶段为0.
 * @param [in] image_end - 文件内容结束偏移，仅在文件内容阶段使用.
 */
static void otaAdvanceStage(SnfOtaParseContext *context, uint32_t header_size, uint32_t image_end)
{
    if ((header_size != 0) && (context->buffered_size != header_size))
    {
        return;
    }

    switch (context->stage)
    {
        case OTA_PARSE_METADATA:
            /* 校验元数据头，确定后续文件属性表的范围。 */
            context->error = otaParseMetadata(context->buffer, header_size, &context->metadata);
            if (context->error != SNF_OTA_ERROR_NONE)
            {
                return;
            }

            memcpy(context->auth_header_data, context->buffer, SNF_OTA_META_HEAD_SIZE);
            context->previous_end = SNF_OTA_META_HEAD_SIZE
                + SNF_OTA_META_FILE_SIZE * (uint32_t)context->metadata.file_count;
            if (context->previous_end > context->package_size)
            {
                context->error = SNF_OTA_ERROR_IMAGE_INCOMPLETE;
                return;
            }
            context->buffered_size = 0;
            context->stage = OTA_PARSE_FILE;
            break;
        case OTA_PARSE_FILE:
            /* 逐项校验文件属性，并按文件名选择目标文件。 */
            context->error = otaSelectFile(context);
            break;
        case OTA_PARSE_FILE_GAP:
            if (context->offset == context->file.offset)
            {
                context->stage = (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
                    ? OTA_PARSE_GCM_IV : OTA_PARSE_IMAGE;
            }
            break;
        case OTA_PARSE_GCM_IV:
            /* IV收齐后，以外层头部和目标文件属性作为附加认证数据启动解密。 */
            if (snfAesGcmDecryptStart(&context->gcm, ota_aes_key, context->buffer,
                                      context->auth_header_data, sizeof(context->auth_header_data)) != SNF_AES_GCM_OK)
            {
                context->error = SNF_OTA_ERROR_DECRYPT_FAILED;
                return;
            }
            context->buffered_size = 0;
            context->stage = OTA_PARSE_IMAGE;
            break;
        case OTA_PARSE_IMAGE:
            if (context->offset == image_end)
            {
                context->stage = (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
                    ? OTA_PARSE_GCM_TAG : OTA_PARSE_TAIL;
            }
            break;
        case OTA_PARSE_GCM_TAG:
            /* 校验完整文件的认证标签，通过后继续处理包尾数据。 */
            if (snfAesGcmDecryptFinish(&context->gcm, context->buffer) != SNF_AES_GCM_OK)
            {
                context->error = SNF_OTA_ERROR_DECRYPT_FAILED;
                return;
            }
            context->buffered_size = 0;
            context->stage = OTA_PARSE_TAIL;
            break;
        case OTA_PARSE_TAIL:
            break;
        default:
            context->error = SNF_OTA_ERROR_INVALID_PARAM;
            break;
    }
}

SnfOtaErrorCode snfOtaParseInit(SnfOtaParseContext *context, uint32_t package_size,
                                const char *file_name, uint32_t flash_size)
{
    if (context == NULL)
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    memset(context, 0, sizeof(*context));
    context->error = SNF_OTA_ERROR_INVALID_PARAM;
    if ((package_size <= (SNF_OTA_META_HEAD_SIZE + SNF_OTA_META_FILE_SIZE)) || (flash_size == 0))
    {
        return context->error;
    }

    if ((file_name == NULL) || (file_name[0] == '\0'))
    {
        return context->error;
    }

    memcpy(context->file_name, file_name, SNF_OTA_FILE_NAME_SIZE);
    context->package_size = package_size;
    context->flash_size = flash_size;
    context->file_crc = UINT32_MAX;
    context->stage = OTA_PARSE_METADATA;
    context->error = SNF_OTA_ERROR_NONE;

    return SNF_OTA_ERROR_NONE;
}

SnfOtaErrorCode snfOtaParseData(SnfOtaParseContext *context, const uint8_t *data, uint32_t size,
                                uint32_t *consumed, SnfOtaPayload *payload)
{
    uint32_t take;
    uint32_t header_size = 0;
    uint32_t image_end = 0;

    if ((context == NULL) || (data == NULL) || (consumed == NULL) || (payload == NULL))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    *consumed = 0;
    memset(payload, 0, sizeof(*payload));
    if (context->error != SNF_OTA_ERROR_NONE)
    {
        return context->error;
    }

    if ((size == 0) || (context->package_size == 0) || (context->offset > context->package_size)
        || (size > (context->package_size - context->offset)))
    {
        context->error = SNF_OTA_ERROR_INVALID_PARAM;
        return context->error;
    }

    /* 根据当前阶段确定待收字段长度或待处理的数据范围。 */
    take = size;
    switch (context->stage)
    {
        case OTA_PARSE_METADATA:
            header_size = SNF_OTA_META_HEAD_SIZE;
            break;
        case OTA_PARSE_FILE:
            header_size = SNF_OTA_META_FILE_SIZE;
            break;
        case OTA_PARSE_GCM_IV:
            header_size = SNF_AES_GCM_IV_SIZE;
            break;
        case OTA_PARSE_GCM_TAG:
            header_size = SNF_AES_GCM_TAG_SIZE;
            break;
        case OTA_PARSE_FILE_GAP:
            take = context->file.offset - context->offset;
            break;
        case OTA_PARSE_IMAGE:
            image_end = context->file.offset + context->file.size;
            if (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
            {
                image_end -= SNF_AES_GCM_TAG_SIZE;
            }
            take = image_end - context->offset;
            break;
        case OTA_PARSE_TAIL:
            break;
        default:
            context->error = SNF_OTA_ERROR_INVALID_PARAM;
            break;
    }

    if (context->error != SNF_OTA_ERROR_NONE)
    {
        return context->error;
    }

    /* 计算当前头部或字段还需处理的长度。 */
    if (header_size != 0)
    {
        take = header_size - context->buffered_size;
    }

    /* 处理长度不能超过本次输入长度。 */
    if (take > size)
    {
        take = size;
    }

    /* 目标文件按需解密，输出可写入Flash的明文片段。 */
    if (context->stage == OTA_PARSE_IMAGE)
    {
        context->error = otaParsePayload(context, data, take, payload);
        if (context->error != SNF_OTA_ERROR_NONE)
        {
            return context->error;
        }
        take = payload->size;
    }

    /* 外层头部、IV和认证标签允许跨输入块缓存。 */
    if (header_size != 0)
    {
        memcpy(&context->buffer[context->buffered_size], data, take);
        context->buffered_size += (uint16_t)take;
    }

    /* 更新处理进度，收齐当前阶段的数据后执行校验或切换阶段。 */
    context->offset += take;
    *consumed = take;
    otaAdvanceStage(context, header_size, image_end);

    return context->error;
}

SnfOtaErrorCode snfOtaParseFinish(const SnfOtaParseContext *context)
{
    if (context == NULL)
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    if (context->error != SNF_OTA_ERROR_NONE)
    {
        return context->error;
    }

    if ((context->offset != context->package_size) || (context->stage != OTA_PARSE_TAIL))
    {
        return SNF_OTA_ERROR_IMAGE_INCOMPLETE;
    }

    if ((context->file_crc ^ UINT32_MAX) != context->file.crc)
    {
        return SNF_OTA_ERROR_CHECK_FAILED;
    }

    return SNF_OTA_ERROR_NONE;
}

void snfOtaParseFree(SnfOtaParseContext *context)
{
    if (context != NULL)
    {
        snfAesGcmFree(&context->gcm);
        memset(context, 0, sizeof(*context));
    }
}
