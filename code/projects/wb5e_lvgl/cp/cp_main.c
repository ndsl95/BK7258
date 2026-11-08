#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include <modules/pm.h>
#include <driver/pwr_clk.h>
#include "bk_api_ipc_test.h"

extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern void bk_set_jtag_mode(uint32_t cpu_id, uint32_t group_id);

/* Local credentials are optional and never committed to the repository. */
#if __has_include("wifi_local.h")
#include "wifi_local.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

extern int demo_sta_app_init(char *oob_ssid, char *connect_key);

static void wifi_auto_connect_task(void *arg)
{
    /* 等 wifi 栈、netif 起来 */
    rtos_delay_milliseconds(4000);
    BK_LOGI("WB5E", "auto connect to %s ...\r\n", WIFI_SSID);
    int ret = demo_sta_app_init(WIFI_SSID, WIFI_PASS);
    BK_LOGI("WB5E", "demo_sta_app_init ret=%d\r\n", ret);
    rtos_delete_thread(NULL);
}

static void start_wifi_auto_connect(void)
{
    beken_thread_t th = NULL;
    rtos_create_thread(&th, BEKEN_DEFAULT_WORKER_PRIORITY, "wifi_auto",
                       (beken_thread_function_t)wifi_auto_connect_task,
                       4096, 0);
}

void user_app_main(void) {
    // start smp(cpu1, cpu2)
    bk_pm_module_vote_boot_cp1_ctrl(PM_BOOT_CP1_MODULE_NAME_APP,PM_POWER_MODULE_STATE_ON);
    if (WIFI_SSID[0] != '\0') {
        start_wifi_auto_connect();
    }
}

int main(void)
{
    rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
    bk_init();

#if (BK_IPC_UT_TEST)
    bk_ipc_test_init();
#endif

    return 0;
}
