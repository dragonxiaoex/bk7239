/**
 * @file    sonoff_ota_matter.h
 * @brief   Matter OTA链路适配接口
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-02
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_OTA_MATTER_H__
#define __SONOFF_OTA_MATTER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 中止Matter OTA.
 *
 * @return 0表示无需中止或已结束, 负数表示请求失败或等待超时.
 */
int snfOtaMatterAbort(void);

/**
 * @brief 向OTA核心提交一段Matter镜像数据.
 *
 * 数据按offset递增提交，函数等待本段数据被处理。缓冲区由核心借用，
 * 等待超时不代表核心已停止使用数据，应在OTA结束后再释放或修改缓冲区。
 *
 * @param [in] offset - 镜像内偏移量.
 * @param [in] data - 镜像数据.
 * @param [in] len - 数据长度.
 * @return 0表示本段数据已处理, 负数表示提交失败、OTA失败或等待超时.
 */
int snfOtaMatterWrite(uint32_t offset, const uint8_t *data, uint32_t len);

/**
 * @brief 启动Matter OTA.
 *
 * @param [in] image_size - 镜像总长度.
 * @param [in] version - 镜像版本号.
 * @return 0表示请求成功, 负数表示请求失败.
 */
int snfOtaMatterStart(uint32_t image_size, uint32_t version);

#ifdef __cplusplus
}
#endif

#endif /* __SONOFF_OTA_MATTER_H__ */
