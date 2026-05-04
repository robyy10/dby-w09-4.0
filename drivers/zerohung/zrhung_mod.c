/*
 * zrhung_mod.c
 *
 * This file contains all of zrhung's tracepoint
 *
 * Copyright (c) 2017-2021 Huawei Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "watchpoint/wp_screen.h"
#ifdef CONFIG_DFX_ZEROHUNG_SOFTLOCKUP
#include "watchpoint/wp_softlockup.h"
#endif
#include <platform/trace/events/zerohung.h>

#ifdef CONFIG_DFX_ZEROHUNG_WP_SCREEN
static void hook_hung_wp_screen_getbl(void *ignore, int *screen_bl_level)
{
	hung_wp_screen_getbl(screen_bl_level);
}

static void hook_hung_wp_screen_setbl(void *ignore, int level)
{
	hung_wp_screen_setbl(level);
}

static void hook_hung_wp_screen_qcom_pkey_press(void *ignore, int type, int state)
{
	hung_wp_screen_qcom_pkey_press(type, state);
}

static void hook_hung_wp_screen_powerkey_ncb(void *ignore, unsigned long event)
{
	hung_wp_screen_powerkey_ncb(event);
}
#endif

#ifdef CONFIG_DFX_ZEROHUNG_SOFTLOCKUP
static void hook_dfx_watchdog_check_hung(void *ignore, int unused)
{
	dfx_watchdog_check_hung();
}

static void hook_dfx_watchdog_lockup_init(void *ignore, int unused)
{
	dfx_watchdog_lockup_init();
}

static void hook_dfx_watchdog_lockup_init_work(void *ignore, int unused)
{
	dfx_watchdog_lockup_init_work();
}
#endif

static int __init zrhung_init(void)
{
#ifdef CONFIG_DFX_ZEROHUNG_WP_SCREEN
	register_trace_hung_wp_screen_getbl(hook_hung_wp_screen_getbl, NULL);
	register_trace_hung_wp_screen_setbl(hook_hung_wp_screen_setbl, NULL);
	register_trace_hung_wp_screen_qcom_pkey_press(hook_hung_wp_screen_qcom_pkey_press, NULL);
	register_trace_hung_wp_screen_powerkey_ncb(hook_hung_wp_screen_powerkey_ncb, NULL);
#endif
#ifdef CONFIG_DFX_ZEROHUNG_SOFTLOCKUP
	register_trace_dfx_watchdog_check_hung(hook_dfx_watchdog_check_hung, NULL);
	register_trace_dfx_watchdog_lockup_init(hook_dfx_watchdog_lockup_init, NULL);
	register_trace_dfx_watchdog_lockup_init_work(hook_dfx_watchdog_lockup_init_work, NULL);
#endif
	return 0;
}
module_init(zrhung_init);

static void __exit zrhung_exit(void)
{
#ifdef CONFIG_DFX_ZEROHUNG_WP_SCREEN
	unregister_trace_hung_wp_screen_getbl(hook_hung_wp_screen_getbl, NULL);
	unregister_trace_hung_wp_screen_setbl(hook_hung_wp_screen_setbl, NULL);
	unregister_trace_hung_wp_screen_qcom_pkey_press(hook_hung_wp_screen_qcom_pkey_press, NULL);
	unregister_trace_hung_wp_screen_powerkey_ncb(hook_hung_wp_screen_powerkey_ncb, NULL);
#endif
#ifdef CONFIG_DFX_ZEROHUNG_SOFTLOCKUP
	unregister_trace_dfx_watchdog_check_hung(hook_dfx_watchdog_check_hung, NULL);
	unregister_trace_dfx_watchdog_lockup_init(hook_dfx_watchdog_lockup_init, NULL);
	unregister_trace_dfx_watchdog_lockup_init_work(hook_dfx_watchdog_lockup_init_work, NULL);
#endif
}
module_exit(zrhung_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
MODULE_DESCRIPTION("Huawei Zerohung Module");
