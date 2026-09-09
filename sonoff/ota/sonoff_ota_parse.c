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

    /* 头部CRC覆盖CRC字段之前的全部字节，包含元数据预留字段. */
    crc = OTA_GET_BIG_ENDIAN_DATA_4B(&data[OTA_HEAD_CRC_OFFSET]);
    if ((snfCrc32(UINT32_MAX, data, OTA_HEAD_CRC_OFFSET) ^ UINT32_MAX) != crc)
    {
        LOG_I(tag, "metadata crc check error");
        return SNF_OTA_ERROR_CHECK_FAILED;
    }

    /* 包内至少有一个文件；联合版本只要求首字节非0. */
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

    /* 属性CRC只覆盖其前面的字段，后面的文件属性预留字段不参与计算. */
    attributes_crc = OTA_GET_BIG_ENDIAN_DATA_4B(&data[OTA_FILE_ATTRIBUTES_CRC_OFFSET]);
    if ((snfCrc32(UINT32_MAX, data, OTA_FILE_ATTRIBUTES_CRC_OFFSET) ^ UINT32_MAX) != attributes_crc)
    {
        return SNF_OTA_ERROR_CHECK_FAILED;
    }

    if ((data[OTA_FILE_NAME_OFFSET] == 0) || (data[OTA_FILE_VERSION_OFFSET] == 0))
    {
        return SNF_OTA_ERROR_INVALID_PARAM;
    }

    /* 名称和版本按定长复制；结构体先清零，使各字符串额外预留的末字节保持为结束符. */
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

    /*
     * 加密文件长度包含IV和TAG，扣除overhead后必须仍有镜像数据.
     * previous_end从文件表末尾开始递推，禁止文件覆盖头部、乱序或相互重叠.
     * 先确认起点在包内，再比较包内剩余长度，避免文件越界及偏移加法溢出.
     */
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
        /* 保留选中文件属性的原始字节，与元数据头一起参与GCM认证. */
        memcpy(&context->auth_header_data[SNF_OTA_META_HEAD_SIZE], context->buffer, SNF_OTA_META_FILE_SIZE);
    }

    context->previous_end = file.offset + file.size;
    context->file_index++;
    context->buffered_size = 0;
    /* 找到目标后仍须检查完全部文件属性，以发现后续重复目标或非法文件范围. */
    if (context->file_index == context->metadata.file_count)
    {
        /* 合法文件长度必大于0，此处为0表示遍历完仍未找到目标. */
        if (context->file.size == 0)
        {
            return SNF_OTA_ERROR_FILE_NOT_FOUND;
        }

        context->stage = OTA_PARSE_FILE_GAP;
        /* 已到目标起点时直接处理文件，无需再经过跳过间隔的阶段. */
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
    /* 未加密时直接借用输入；offset先从包内位置换算成目标文件内的位置. */
    const uint8_t *image_data = data;
    uint32_t offset = context->offset - context->file.offset;

    if (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
    {
        /* 单次解密结果必须放得进缓冲区，剩余输入由调用方继续送入. */
        if (size > sizeof(context->decrypt_buffer))
        {
            size = sizeof(context->decrypt_buffer);
        }

        /* 各输入块沿用同一个GCM上下文，整个文件解密完后才统一校验TAG. */
        if (snfAesGcmDecryptUpdate(&context->gcm, data, size, context->decrypt_buffer) != SNF_AES_GCM_OK)
        {
            return SNF_OTA_ERROR_DECRYPT_FAILED;
        }
        image_data = context->decrypt_buffer;
        /* 加密文件的起点指向IV；扣除IV长度后，首段镜像才从OTA分区偏移0写入. */
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
    /* 头部、IV和TAG允许跨输入块；未收齐时保留当前阶段和缓存，等待下一次输入. */
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
            /* 初始previous_end指向整个文件表末尾，首个文件的起点不得早于此位置. */
            context->previous_end = SNF_OTA_META_HEAD_SIZE
                + SNF_OTA_META_FILE_SIZE * (uint32_t)context->metadata.file_count;
            /* 文件数量声明的属性表必须完整放在输入包内. */
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
            /* 前面的其他文件或填充已跳过，抵达目标起点后才接收IV或镜像内容. */
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
            /* 内容处理完后，加密文件还需接收TAG；未加密文件可直接进入包尾阶段. */
            if (context->offset == image_end)
            {
                context->stage = (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
                    ? OTA_PARSE_GCM_TAG : OTA_PARSE_TAIL;
            }
            break;
        case OTA_PARSE_GCM_TAG:
            /* TAG校验整个目标文件及附加头部数据，通过后仍须接收包尾并在Finish中核对明文CRC. */
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
    /* 包至少包含元数据头、一项文件属性和非空文件，且OTA分区必须有可用容量. */
    if ((package_size <= (SNF_OTA_META_HEAD_SIZE + SNF_OTA_META_FILE_SIZE)) || (flash_size == 0))
    {
        return context->error;
    }

    if ((file_name == NULL) || (file_name[0] == '\0'))
    {
        return context->error;
    }

    /* 调用方保证完整定长缓冲区可读，直接复制；初始化清零保留了末尾的字符串结束符. */
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

    /* 每次只处理当前阶段的一段数据，头部和跳过阶段的payload保持为空. */
    *consumed = 0;
    memset(payload, 0, sizeof(*payload));
    if (context->error != SNF_OTA_ERROR_NONE)
    {
        return context->error;
    }

    /* offset以整个公司包为基准；先确认位置有效，再限制输入长度不得超过包内剩余字节数. */
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
            /* 只跳过到目标文件起点，不能把目标文件的数据也一起跳过. */
            take = context->file.offset - context->offset;
            break;
        case OTA_PARSE_IMAGE:
            /* file.offset和file.size选中后固定，二者之和指向包内目标文件末尾的下一字节. */
            image_end = context->file.offset + context->file.size;
            if (context->metadata.cipher_type == SNF_OTA_CIPHER_AES_256_GCM)
            {
                /* TAG单独收集校验，镜像密文必须在TAG之前结束；前面的IV已在IV阶段处理. */
                image_end -= SNF_AES_GCM_TAG_SIZE;
            }
            take = image_end - context->offset;
            break;
        case OTA_PARSE_TAIL:
            /* 目标文件已处理完，剩余输入可能是其他文件或填充，只推进包内位置. */
            break;
        default:
            context->error = SNF_OTA_ERROR_INVALID_PARAM;
            break;
    }

    if (context->error != SNF_OTA_ERROR_NONE)
    {
        return context->error;
    }

    /* header_size是完整字段长度，buffered_size是前几次已缓存的长度，差值就是还缺的字节数. */
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
        /* 解密缓冲区可能进一步缩短处理长度，输入进度必须以实际输出的镜像长度为准. */
        take = payload->size;
    }

    /* 头部、IV和TAG从上次缓存末尾继续追加，收齐后才由otaAdvanceStage校验并清零缓存长度. */
    if (header_size != 0)
    {
        memcpy(&context->buffer[context->buffered_size], data, take);
        context->buffered_size += (uint16_t)take;
    }

    /*
     * offset累计包内处理进度；consumed返回本次处理长度，包含头部和跳过的数据，供调用方移动输入指针.
     * 先更新位置，再判断是否到达当前阶段终点；只有IMAGE阶段会输出可写入Flash的payload.
     */
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

    /* 必须处理到整包末尾且已进入TAIL；否则目标文件内容或加密文件的TAG可能尚未收齐. */
    if ((context->offset != context->package_size) || (context->stage != OTA_PARSE_TAIL))
    {
        return SNF_OTA_ERROR_IMAGE_INCOMPLETE;
    }

    /* 累计的是完整目标文件的明文CRC，最终异或后与文件属性中的CRC比较，通过后才允许应用升级. */
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
