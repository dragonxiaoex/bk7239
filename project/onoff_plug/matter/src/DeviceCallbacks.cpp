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
 * @file DeviceCallbacks.cpp
 *
 * Implements all the callbacks to the application from the CHIP Stack
 *
 **/

#include "DeviceCallbacks.h"

/* sonoff modify start */
#include "sonoff_plug_handle.h"
/* sonoff modify end */

#include <common/CHIPDeviceManager.h>

#include <app/CommandHandler.h>
#include <app/server/Dnssd.h>
#include <app/util/attribute-table.h>
#include <app/util/basic-types.h>
#include <app/util/util.h>
#include <lib/dnssd/Advertiser.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <lib/support/logging/Constants.h>
/* sonoff modify start */
#include <app-common/zap-generated/attribute-type.h>
/* sonoff modify end */

static const char * TAG = "app-devicecallbacks";

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceManager;
using namespace ::chip::Logging;
using namespace ::chip::app;

uint32_t identifyTimerCount;
constexpr uint32_t kIdentifyTimerDelayMS = 250;

/* sonoff modify start */
static void onOffReportWork(intptr_t arg)
{
    uint8_t value = static_cast<uint8_t>(arg);

    emberAfWriteAttribute(1, Clusters::OnOff::Id, Clusters::OnOff::Attributes::OnOff::Id, &value,
                          ZCL_BOOLEAN_ATTRIBUTE_TYPE);
}

extern "C" int snfMatterOnOffReport(uint8_t onoff)
{
    CHIP_ERROR err = DeviceLayer::PlatformMgr().ScheduleWork(onOffReportWork, static_cast<intptr_t>(onoff != 0 ? 1 : 0));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "[%s] schedule onoff report failed", TAG);
        return -1;
    }

    return 0;
}
/* sonoff modify end */

void AppDeviceCallbacks::DeviceEventCallback(const ChipDeviceEvent * event, intptr_t arg)
{
    CommonDeviceCallbacks::DeviceEventCallback(event, arg);
}

void AppDeviceCallbacks::PostAttributeChangeCallback(EndpointId endpointId, ClusterId clusterId, AttributeId attributeId,
                                                     uint8_t type, uint16_t size, uint8_t * value)
{
    switch (clusterId)
    {
    case Clusters::OnOff::Id:
        OnOnOffPostAttributeChangeCallback(endpointId, attributeId, value);
        break;

    case Clusters::Identify::Id:
        OnIdentifyPostAttributeChangeCallback(endpointId, attributeId, value);
        break;

    default:
        ChipLogProgress(Zcl, "Unknown cluster ID: " ChipLogFormatMEI, ChipLogValueMEI(clusterId));
        break;
    }
}

void AppDeviceCallbacks::OnOnOffPostAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    VerifyOrExit(attributeId == Clusters::OnOff::Attributes::OnOff::Id,
                 ChipLogError(DeviceLayer, "[%s] Unhandled Attribute ID: '0x%04lx", TAG, attributeId));
    VerifyOrExit(endpointId == 1 || endpointId == 2,
                 ChipLogError(DeviceLayer, "[%s] Unexpected EndPoint ID: `0x%02x'", TAG, endpointId));
    /* sonoff modify start */
    snfPlugOnOffRawSet(*value);
    /* sonoff modify end */

exit:
    return;
}

void IdentifyTimerHandler(Layer * systemLayer, void * appState)
{
    // statusLED1.Animate();

    if (identifyTimerCount)
    {
        systemLayer->StartTimer(Clock::Milliseconds32(kIdentifyTimerDelayMS), IdentifyTimerHandler, appState);
        // Decrement the timer count.
        identifyTimerCount--;
    }
}

void AppDeviceCallbacks::OnIdentifyPostAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    VerifyOrExit(attributeId == Clusters::Identify::Attributes::IdentifyTime::Id,
                 ChipLogError(DeviceLayer, "[%s] Unhandled Attribute ID: '0x%04lx", TAG, attributeId));
    VerifyOrExit(endpointId == 1, ChipLogError(DeviceLayer, "[%s] Unexpected EndPoint ID: `0x%02x'", TAG, endpointId));

    // timerCount represents the number of callback executions before we stop the timer.
    // value is expressed in seconds and the timer is fired every 250ms, so just multiply value by 4.
    // Also, we want timerCount to be odd number, so the ligth state ends in the same state it starts.
    identifyTimerCount = (*value) * 4;

    DeviceLayer::SystemLayer().CancelTimer(IdentifyTimerHandler, this);
    DeviceLayer::SystemLayer().StartTimer(Clock::Milliseconds32(kIdentifyTimerDelayMS), IdentifyTimerHandler, this);

exit:
    return;
}
#if 0
void emberAfScenesClusterEnhancedAddSceneCallback(chip::app::CommandHandler *, chip::app::ConcreteCommandPath const &,
                                                  chip::app::Clusters::Scenes::Commands::EnhancedAddScene::DecodableType const &)
{}
void emberAfScenesClusterEnhancedViewSceneCallback(chip::app::CommandHandler *, chip::app::ConcreteCommandPath const &,
                                                  chip::app::Clusters::Scenes::Commands::EnhancedViewScene::DecodableType const &)
{}
void emberAfScenesClusterCopySceneCallback(chip::app::CommandHandler *, chip::app::ConcreteCommandPath const &,
                                                  chip::app::Clusters::Scenes::Commands::CopyScene::DecodableType const &)
{}
#endif
