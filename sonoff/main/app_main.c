#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include "bk_openthread.h"
#include "modules/pm.h"

extern void user_app_main(void);
extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern void bk_set_jtag_mode(uint32_t cpu_id, uint32_t group_id);

void user_app_main(void)
{
    /* sonoff modify start */
    rtos_delay_milliseconds(2000);
    extern void sonoffEntry(void);
    sonoffEntry();
    //bk_openthread_init();
    rtos_delete_thread(NULL);
    /* sonoff modify end */
}

int main(void)
{
#if (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7236)
    STARTUP_PERF(14);
#endif
    bk_init();
#if CONFIG_SYS_CPU0
    bk_pm_module_vote_cpu_freq(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_240M);
#endif
#if (CONFIG_SYS_CPU0)
    /* sonoff modify start */
    //rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
    // bk_set_printf_sync(true);
    // shell_set_log_level(BK_LOG_WARN);

    rtos_create_sram_thread(NULL,
                            BEKEN_APPLICATION_PRIORITY,
                            "sonoff app",
                            (beken_thread_function_t)user_app_main,
                            CONFIG_APP_MAIN_TASK_STACK_SIZE,
                            (beken_thread_arg_t)0);
    /* sonoff modify end */
#endif

    

    return 0;
}
