/*
 *
 *    Copyright (c) 2023 Project CHIP Authors
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


#include "matter_pal.h"
#include "Plug.h"
#include <app/server/Server.h>
#include <app/util/attribute-table.h>
#include <app-common/zap-generated/attribute-type.h>
#include <system/SystemClock.h>
#include <lib/support/logging/CHIPLogging.h>


Plug Plug::sPlug;
using namespace chip;
using namespace ::chip::Logging;

#define KEY_GPIO        GPIO_21
#define CONTROL_GPIO    GPIO_20
#define LED_GPIO        GPIO_9
#define LED_FLASHING_TIMER_DELAY_MS 1000
#define LED_FAST_FLASHING_TIMER_DELAY_MS 200
#define KEY_DOWN_TIMER_DELAY_MS 20
#define KEY_LONG_DOWN_TIME_MS 6000

static void key_callback(gpio_id_t id)
{
    PlugMgr().KeyDown();
}

void Plug::Init()
{
    ChipLogDetail(DeviceLayer,"Plug Init");
    gpio_config_t cfg;
	gpio_int_type_t int_type = GPIO_INT_TYPE_MAX;

    bk_gpio_disable_input(CONTROL_GPIO);
    bk_gpio_enable_output(CONTROL_GPIO);

    bk_gpio_disable_input(LED_GPIO);
    bk_gpio_enable_output(LED_GPIO);

    cfg.io_mode =GPIO_INPUT_ENABLE;
	int_type = GPIO_INT_TYPE_FALLING_EDGE;
	cfg.pull_mode = GPIO_PULL_UP_EN;
	bk_gpio_set_config(KEY_GPIO, &cfg);
	bk_gpio_register_isr(KEY_GPIO ,key_callback);
    bk_gpio_enable_interrupt(KEY_GPIO);

    bk_gpio_set_output_high(CONTROL_GPIO);
    bk_gpio_set_output_high(LED_GPIO);
    isOn = false;
    mFlashingFreq = 0;
    initOver = false;
}

void Plug::StartUpInit()
{
    ChipLogDetail(DeviceLayer,"Plug StartUpInit");
    if (Server::GetInstance().GetFabricTable().FabricCount() == 0)
    {
        //LedStartFlashing(false);
    }
    initOver = true;
}

void Plug::SetOnOff(bool on)
{
    ChipLogDetail(DeviceLayer,"Plug SetOnOff");
    if(!initOver || mFlashingFreq != 0 || on == isOn)
    {
        return;
    }
    isOn = on;
    if(isOn)
    {
        bk_gpio_set_output_high(CONTROL_GPIO);
        bk_gpio_set_output_high(LED_GPIO);
    }
    else
    {
        bk_gpio_set_output_low(CONTROL_GPIO);
        bk_gpio_set_output_low(LED_GPIO);
    }
}

static void KeyDownHandler(chip::System::Layer * systemLayer, void * c)
{
    uint16_t *count = static_cast<uint16_t*>(c);
    if(!bk_gpio_get_input(KEY_GPIO))
    {
        (*count)++;
        DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(KEY_DOWN_TIMER_DELAY_MS), KeyDownHandler, c);
    }
    else
    {
        if((*count)*KEY_DOWN_TIMER_DELAY_MS > KEY_LONG_DOWN_TIME_MS)
        {
            // factory reset
            ChipLogDetail(DeviceLayer,"Plug Key Down Long, factory reset");
            Server::GetInstance().ScheduleFactoryReset();
        }
        else
        {
            // change on off
            ChipLogDetail(DeviceLayer,"Plug Key Down, change on off");
            bool value = !PlugMgr().IsOn();
            emberAfWriteAttribute(1, app::Clusters::OnOff::Id, app::Clusters::OnOff::Attributes::OnOff::Id, (uint8_t *)&value, ZCL_BOOLEAN_ATTRIBUTE_TYPE);
        }
        // BkGpioEnableIRQ(KEY_GPIO, IRQ_TRIGGER_FALLING_EDGE, key_callback, NULL);
    }
}

void Plug::KeyDown()
{
    ChipLogDetail(DeviceLayer,"Plug Key Down");
    static uint16_t count;
    count = 0;
    DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(KEY_DOWN_TIMER_DELAY_MS), KeyDownHandler, &count);
}

static void LedFlashingHandler(chip::System::Layer * systemLayer, void * c)
{
    ChipLogDetail(DeviceLayer,"Plug Led Flashing");
    static bool state = false;
    if(PlugMgr().IsFlashing())
    {
        if(state)
        {
            bk_gpio_set_output_low(LED_GPIO);
        }
        else
        {
            bk_gpio_set_output_high(LED_GPIO);
        }
        state = !state;
        uint16_t *flashFreq = static_cast<uint16_t*>(c);
        DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(*flashFreq), LedFlashingHandler, c);
    }
}

void Plug::LedStartFlashing(bool fast)
{
    ChipLogDetail(DeviceLayer,"Plug Led Start Flashing");
    mFlashingFreq = fast ? LED_FAST_FLASHING_TIMER_DELAY_MS : LED_FLASHING_TIMER_DELAY_MS;
    DeviceLayer::SystemLayer().CancelTimer(LedFlashingHandler, &mFlashingFreq);
    DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(mFlashingFreq), LedFlashingHandler, &mFlashingFreq);
}

void Plug::LedStopFlashing()
{
    ChipLogDetail(DeviceLayer,"Plug Led Stop Flashing");
    if(mFlashingFreq != 0)
    {
        DeviceLayer::SystemLayer().CancelTimer(LedFlashingHandler, &mFlashingFreq);
        mFlashingFreq = 0;
        bool value = false;
        emberAfWriteAttribute(1, app::Clusters::OnOff::Id, app::Clusters::OnOff::Attributes::OnOff::Id, (uint8_t *)&value, ZCL_BOOLEAN_ATTRIBUTE_TYPE);
    }
}
