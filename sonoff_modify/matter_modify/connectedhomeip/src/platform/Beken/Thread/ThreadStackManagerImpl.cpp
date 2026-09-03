/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
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
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/OpenThread/OpenThreadUtils.h>
#include <platform/ThreadStackManager.h>

#include <platform/OpenThread/GenericThreadStackManagerImpl_OpenThread.hpp>

#include <lib/support/CHIPPlatformMemory.h>

#include <openthread/platform/entropy.h>

#include <mbedtls/platform.h>

otInstance *g_otInst = NULL;

namespace chip {
namespace DeviceLayer {

using namespace ::chip::DeviceLayer::Internal;

ThreadStackManagerImpl ThreadStackManagerImpl::sInstance;

CHIP_ERROR ThreadStackManagerImpl::_InitThreadStack(void)
{
    return InitThreadStack(g_otInst);
}

CHIP_ERROR ThreadStackManagerImpl::InitThreadStack(otInstance * otInst)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    // Initialize the generic implementation base classes.
    //err = GenericThreadStackManagerImpl_FreeRTOS<ThreadStackManagerImpl>::DoInit();
    //SuccessOrExit(err);
    err = GenericThreadStackManagerImpl_OpenThread<ThreadStackManagerImpl>::DoInit(otInst);
    SuccessOrExit(err);
    ChipLogProgress(Zcl, "Initializing GenericThreadStackManagerImpl_OpenThread");

exit:
    if (err != CHIP_NO_ERROR) {
        ChipLogError(Zcl, "Thread Stack initialized failed");
    }
    return err;
}
#if 0
bool ThreadStackManagerImpl::IsInitialized()
{
    return otGetInstance() != NULL;
}
#endif
CHIP_ERROR ThreadStackManagerImpl::_StartThreadTask(void)
{
    // Stubbed since our thread task is created in the InitThreadStack function and it will start once the scheduler starts.
    return CHIP_NO_ERROR;
}

void ThreadStackManagerImpl::_LockThreadStack(void)
{
   // sl_ot_rtos_acquire_stack_mutex();
   /* sonoff modify start */
   snfNetTestThreadLock();
   /* sonoff modify end */
}

bool ThreadStackManagerImpl::_TryLockThreadStack(void)
{
    // TODO: Implement a non-blocking version of the mutex lock
   // sl_ot_rtos_acquire_stack_mutex();
    /* sonoff modify start */
    return snfNetTestThreadTryLock();
    /* sonoff modify end */
}

void ThreadStackManagerImpl::_UnlockThreadStack(void)
{
    //sl_ot_rtos_release_stack_mutex();
    /* sonoff modify start */
    snfNetTestThreadUnlock();
    /* sonoff modify end */
}

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
void ThreadStackManagerImpl::_WaitOnSrpClearAllComplete()
{
    ChipLogProgress(Zcl, "_WaitOnSrpClearAllComplete");
    // Only 1 task can be blocked on a srpClearAll request
    if (mSrpClearAllRequester == nullptr)
    {
        mSrpClearAllRequester = xTaskGetCurrentTaskHandle();
        // Wait on OnSrpClientNotification which confirms the clearing is done.
        // It will notify this current task with NotifySrpClearAllComplete.
        // However, we won't wait more than 2s.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        ChipLogProgress(Zcl, "_WaitOnSrpClearAllComplete-1");
        mSrpClearAllRequester = nullptr;
    }
}

void ThreadStackManagerImpl::_NotifySrpClearAllComplete()
{
    ChipLogProgress(Zcl, "_NotifySrpClearAllComplete");
    if (mSrpClearAllRequester)
    {
        xTaskNotifyGive(mSrpClearAllRequester);
    }
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT

} // namespace DeviceLayer
} // namespace chip
