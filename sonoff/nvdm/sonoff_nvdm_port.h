/**
 * @file    sonoff_nvdm_port.h
 * @brief   NVDM平台适配
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-09-04
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
 #ifndef __SONOFF_NVDM_PORT_H__
 #define __SONOFF_NVDM_PORT_H__

#include <stdint.h>

int snfNvdmPortInit(void);
int snfNvdmPortExistStatus(const char *group, const char *key);
int snfNvdmPortReadStr(const char *group, const char *key, uint8_t *buff, int len);
int snfNvdmPortWriteStr(const char *group, const char *key, const uint8_t *value, int len);
int snfNvdmPortDelete(const char *group, const char *key);
int snfNvdmPortDeleteGroup(const char *group);

 #endif /* __SONOFF_NVDM_PORT_H__ */
