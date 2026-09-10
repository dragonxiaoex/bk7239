/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Platform-specific configuration overrides for the chip Device Layer
 *          on Beken platform.
 */

#pragma once

// ==================== Platform Adaptations ====================
#define CHIP_CONFIG_ERROR_CLASS 1
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION 1
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_AP 0
#endif
#if CHIP_ENABLE_OPENTHREAD
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD 1
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT 1
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_DNS_CLIENT 1
#endif

#define CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE 1

#define CHIP_DEVICE_CONFIG_ENABLE_CHIP_TIME_SERVICE_TIME_SYNC 0

// ========== Platform-specific Configuration =========

// These are configuration options that are unique to the platform.
// These can be overridden by the application as needed.

// ...

// ========== Platform-specific Configuration Overrides =========

#ifndef CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE
#define CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE 8192
#endif // CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE

#ifndef CHIP_DEVICE_CONFIG_THREAD_TASK_STACK_SIZE
#define CHIP_DEVICE_CONFIG_THREAD_TASK_STACK_SIZE 4096
#endif // CHIP_DEVICE_CONFIG_THREAD_TASK_STACK_SIZE

#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_TELEMETRY 0
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_TELEMETRY 0
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_TELEMETRY_FULL 0
#define CHIP_DEVICE_CONFIG_LOG_PROVISIONING_HASH 0

#define CHIP_DEVICE_LAYER_NONE 0

//  Enable use of test setup parameters for testing purposes only.
//
//    WARNING: This option makes it possible to circumvent basic chip security functionality.
//    Because of this it SHOULD NEVER BE ENABLED IN PRODUCTION BUILDS.
//
/* sonoff modify start */
#define CONFIG_ENABLE_BEKEN_DEVICE_INFO 1
/* sonoff modify end */

#if CONFIG_ENABLE_BEKEN_DEVICE_INFO
#define CHIP_DEVICE_CONFIG_ENABLE_TEST_SETUP_PARAMS 0
#else
#define CHIP_DEVICE_CONFIG_ENABLE_TEST_SETUP_PARAMS 1
#endif

#define CONFIG_RENDEZVOUS_MODE 6
#define CHIP_DEVICE_CONFIG_CHIP_TASK_PRIORITY 3// CHIP_PRIORITY must higer than OpenThread_PRIORITY

#define CHIP_DEVICE_CONFIG_ENABLE_EXTENDED_DISCOVERY 1
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DISCOVERY 1

// Activate restore the latest session after rebooting
#if CHIP_CONFIG_ENABLE_ICD_SERVER
#define CONFIG_BEKEN_ACTIVATE_SESSION_RESUMPTION 1
#else
#define CONFIG_BEKEN_ACTIVATE_SESSION_RESUMPTION 0
#endif

#define CONFIG_BEKEN_DAC_PRIV_ENCRYPT 1

#if !CONFIG_ENABLE_BEKEN_DEVICE_INFO
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID 0xFFF1
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID 0x8005
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "Beken Corporation"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME CONFIG_MATTER_EXAMPLE
#define CHIP_DEVICE_CONFIG_TEST_SERIAL_NUMBER "BK000001"
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "1.0"
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION 1
#endif
/* sonoff modify start */
#define CHIP_DEVICE_CONFIG_THREAD_ENABLE_CLI 0
/* sonoff modify end */
#define CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT 50
#define CHIP_DEVICE_CONFIG_ICD_FAST_POLL_INTERVAL System::Clock::Milliseconds32(700)
#define CHIP_DEVICE_CONFIG_ICD_SLOW_POLL_INTERVAL System::Clock::Milliseconds32(10000)
