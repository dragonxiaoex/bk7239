/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include "DeviceCallbacks.h"
#include "Plug.h"

#include <common/BekenAppServer.h>
#include <common/CHIPDeviceManager.h>

#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <DeviceInfoProviderImpl.h>
#include <platform/CHIPDeviceLayer.h>
#include <lib/support/CHIPMem.h>

#include <app/clusters/identify-server/identify-server.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <lib/core/ErrorStr.h>
#include <platform/Beken/BekenConfig.h>
#include <platform/Beken/NetworkCommissioningDriver.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <platform/Beken/FactoryDataProvider.h>
#include <inet/BasicPacketFilters.h>

#include <ota/OTAHelper.h>

using chip::ByteSpan;
using chip::EndpointId;
using chip::FabricIndex;
using chip::NodeId;
using chip::OnDeviceConnected;
using chip::OnDeviceConnectionFailure;
using chip::PeerId;
using chip::Server;
using chip::VendorId;
using chip::Callback::Callback;
using chip::System::Layer;
using chip::Transport::PeerAddress;
using namespace chip::Messaging;

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceManager;
using namespace ::chip::DeviceLayer;

static AppDeviceCallbacks EchoCallbacks;

void OnIdentifyStart(Identify *)
{
    ChipLogProgress(Zcl, "OnIdentifyStart");
}

void OnIdentifyStop(Identify *)
{
    ChipLogProgress(Zcl, "OnIdentifyStop");
}

void OnTriggerEffect(Identify * identify)
{
    switch (identify->mCurrentEffectIdentifier)
    {
    case app::Clusters::Identify::EffectIdentifierEnum::kBlink:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BLINK");
        break;
    case app::Clusters::Identify::EffectIdentifierEnum::kBreathe:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BREATHE");
        break;
    case app::Clusters::Identify::EffectIdentifierEnum::kOkay:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_OKAY");
        break;
    case app::Clusters::Identify::EffectIdentifierEnum::kChannelChange:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_CHANNEL_CHANGE");
        break;
    default:
        ChipLogProgress(Zcl, "No identifier effect");
        return;
    }
}

static Identify gIdentify1 = {
    chip::EndpointId{ 1 }, OnIdentifyStart, OnIdentifyStop, app::Clusters::Identify::IdentifyTypeEnum::kNone, OnTriggerEffect,
};

// Warkaround for ld error:undefined reference to '__sync_synchronize'
// refer to https://stackoverflow.com/questions/64658430/gnu-arm-embedded-toolchain-undefined-reference-to-sync-synchronize
extern "C" void __sync_synchronize() {}

extern "C" unsigned int __atomic_fetch_add_4(volatile void * ptr, unsigned int val, int memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    unsigned int tmp = (*(unsigned int *) ptr + val);
    *(unsigned int *) ptr = tmp;
    GLOBAL_INT_RESTORE();
    return tmp;
}

extern "C" bool __atomic_compare_exchange_4(volatile void * pulDestination, void * ulComparand, unsigned int desired, bool weak,
                                            int success_memorder, int failure_memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    bool ulReturnValue;
    if (*(unsigned int *) pulDestination == *(unsigned int *) ulComparand)
    {
        *(unsigned int *) pulDestination = desired;
        ulReturnValue                    = true;
    }
    else
    {
        *(unsigned int *) ulComparand = *(unsigned int *) pulDestination;
        ulReturnValue                 = false;
    }
    GLOBAL_INT_RESTORE();
    return ulReturnValue;
}

extern "C" unsigned int __atomic_fetch_sub_4(volatile void * ptr, unsigned int val, int memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    unsigned int tmp = (*(unsigned int *) ptr - val);
    *(unsigned int *) ptr = tmp;
    GLOBAL_INT_RESTORE();
    return tmp;
}
extern "C" bool __atomic_compare_exchange_1(volatile void * pulDestination, void * ulComparand, unsigned char desired, bool weak,
                                            int success_memorder, int failure_memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    bool ulReturnValue;
    if (*(unsigned char *) pulDestination == *(unsigned char *) ulComparand)
    {
        *(unsigned char *) pulDestination = desired;
        ulReturnValue                     = true;
    }
    else
    {
        *(unsigned char *) ulComparand = *(unsigned char *) pulDestination;
        ulReturnValue                  = false;
    }
    GLOBAL_INT_RESTORE();
    return ulReturnValue;
}

extern "C" unsigned int __atomic_fetch_and_4(volatile void * pulDestination, unsigned int ulValue, int memorder)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    unsigned int ulCurrent;

    ulCurrent = *(unsigned int *) pulDestination;
    *(unsigned int *) pulDestination &= ulValue;
    GLOBAL_INT_RESTORE();
    return ulCurrent;
}

extern "C" bool __sync_bool_compare_and_swap_4(volatile void * ptr, unsigned int oldval, unsigned int newval)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    if (*(unsigned int *) ptr == oldval)
    {
        *(unsigned int *) ptr = newval;
        GLOBAL_INT_RESTORE();
        return true;
    }
    else
    {
        GLOBAL_INT_RESTORE();
        return false;
    }
}

extern "C" bool __sync_bool_compare_and_swap_1(volatile void * ptr, unsigned char oldval, unsigned char newval)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    if (*(unsigned char *) ptr == oldval)
    {
        *(unsigned char *) ptr = newval;
        GLOBAL_INT_RESTORE();
        return true;
    }
    else
    {
        GLOBAL_INT_RESTORE();
        return false;
    }
}

/* stub for __libc_init_array */
extern "C" void _fini(void) {}
extern "C" void _init(void)
{
    ;
}
#if CHIP_DEVICE_CONFIG_ENABLE_TEST_SETUP_PARAMS
chip::DeviceLayer::FactoryDataProviderInRAM mFactoryDataProvider;
#else
chip::DeviceLayer::FactoryDataProvider mFactoryDataProvider;
#endif
chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

static void InitServer(intptr_t context)
{
    chip::BekenAppServer::Init();
    gExampleDeviceInfoProvider.SetStorageDelegate(&Server::GetInstance().GetPersistentStorage());
    chip::DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);
#if CHIP_DEVICE_CONFIG_ENABLE_OTA_REQUESTOR    
    OTAHelpers::Instance().InitOTARequestor();
#endif    
    PlugMgr().StartUpInit();
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
}
extern "C" void matter_factory_reset(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv ){
    Server::GetInstance().ScheduleFactoryReset();
}

extern "C" void matter_software_reset(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t c) {
            using chip::DeviceLayer::Internal::BekenConfig;
            BekenConfig::WriteConfigValue(BekenConfig::kConfigKey_SoftwareVersion, (uint16_t) 0);
            BekenConfig::WriteConfigValueStr(BekenConfig::kConfigKey_SoftwareVersionString, "0.0");
            bk_reboot();
        },
        0);
}

extern "C" void matter_show(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv ){
    uint16_t discriminator = 0;
    uint32_t passcode = 0;
    uint16_t product_id = 0;
    uint16_t vendor_id = 0;
    char serial_number[21] = {0};
    GetCommissionableDataProvider()->GetSetupDiscriminator(discriminator);
    GetCommissionableDataProvider()->GetSetupPasscode(passcode);
    GetDeviceInstanceInfoProvider()->GetProductId(product_id);
    GetDeviceInstanceInfoProvider()->GetVendorId(vendor_id);
    GetDeviceInstanceInfoProvider()->GetSerialNumber(serial_number, sizeof(serial_number));
    bk_printf("Device Configuration:\r\n");
    bk_printf("\tSerial Number: %s\r\n", serial_number);
    bk_printf("\tVendor ID: %d (0x%04x)\r\n", vendor_id, vendor_id);
    bk_printf("\tProduct ID: %d (0x%04x)\r\n", product_id, product_id);
    bk_printf("\tSetup Passcode: %08ld\r\n", passcode);
    bk_printf("\tSetup Discriminator: %d (0x%04x)\r\n", discriminator, discriminator);
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
}

extern "C" void ChipTest(void)
{
    ChipLogProgress(DeviceLayer, "on-off-plug!");
    CHIP_ERROR err = CHIP_NO_ERROR;
    PlugMgr().Init();

    // initPref();
    SetCommissionableDataProvider(&mFactoryDataProvider);
    SetDeviceInstanceInfoProvider(&mFactoryDataProvider);
#if CONFIG_ENABLE_BEKEN_DEVICE_INFO
    SetDeviceAttestationCredentialsProvider(&mFactoryDataProvider);
#else
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();
    err                           = deviceMgr.Init(&EchoCallbacks); // start the CHIP task
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "DeviceManagerInit() - ERROR!\r\n");
    }
    else
    {
        ChipLogProgress(DeviceLayer, "DeviceManagerInit() - OK\r\n");
    }
    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitServer, 0);
}

bool lowPowerClusterSleep()
{
    return true;
}
