/*
 * rainbow_mod.c
 *
 * This file contains all of rainbow's tracepoint
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd.
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
#include <platform/trace/events/rainbow.h>
#include <platform/linux/rainbow.h>

#include "rainbow.h"
#ifdef CONFIG_DFX_RAINBOW_HIMNTN
#include "rainbow_himntn.h"
#endif
#ifdef CONFIG_DFX_RAINBOW_TRACE
#include "rainbow_trace.h"
#endif

static void hook_rb_trace_irq_write(void *ignore, unsigned int dir, unsigned int new_vec)
{
#ifdef CONFIG_DFX_RAINBOW_TRACE
	rb_trace_irq_write(dir, new_vec);
#endif
}

static void hook_rb_trace_task_write(void *ignore, void *next_task)
{
#ifdef CONFIG_DFX_RAINBOW_TRACE
	rb_trace_task_write(next_task);
#endif
}

static void hook_rb_mreason_set(void *ignore, uint32_t reason)
{
#ifdef CONFIG_BLACKBOX
	rb_mreason_set(reason);
#endif
}

static void hook_rb_sreason_set(void *ignore, char *fmt)
{
#ifdef CONFIG_BLACKBOX
	rb_sreason_set(fmt);
#endif
}

static void hook_rb_attach_info_set(void *ignore, char *fmt)
{
#ifdef CONFIG_BLACKBOX
	rb_attach_info_set(fmt);
#endif
}

static void hook_rb_kallsyms_set(void *ignore, const char *fmt)
{
#ifdef CONFIG_BLACKBOX
	rb_kallsyms_set(fmt);
#endif
}

static void hook_cmd_himntn_item_switch(void *ignore, unsigned int index, bool *rtn)
{
#ifdef CONFIG_DFX_RAINBOW_HIMNTN
	cmd_himntn_item_switch(index, rtn);
#endif
}

static int __init rb_mod_init(void)
{
	register_trace_rb_trace_irq_write(hook_rb_trace_irq_write, NULL);
	register_trace_rb_trace_task_write(hook_rb_trace_task_write, NULL);
	register_trace_rb_mreason_set(hook_rb_mreason_set, NULL);
	register_trace_rb_sreason_set(hook_rb_sreason_set, NULL);
	register_trace_rb_attach_info_set(hook_rb_attach_info_set, NULL);
	register_trace_rb_kallsyms_set(hook_rb_kallsyms_set, NULL);
	register_trace_cmd_himntn_item_switch(hook_cmd_himntn_item_switch, NULL);

	return 0;
}
module_init(rb_mod_init);

static void __exit rb_mod_exit(void)
{
	unregister_trace_rb_trace_irq_write(hook_rb_trace_irq_write, NULL);
	unregister_trace_rb_trace_task_write(hook_rb_trace_task_write, NULL);
	unregister_trace_rb_mreason_set(hook_rb_mreason_set, NULL);
	unregister_trace_rb_sreason_set(hook_rb_sreason_set, NULL);
	unregister_trace_rb_attach_info_set(hook_rb_attach_info_set, NULL);
	unregister_trace_rb_kallsyms_set(hook_rb_kallsyms_set, NULL);
	unregister_trace_cmd_himntn_item_switch(hook_cmd_himntn_item_switch, NULL);
}
module_exit(rb_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
MODULE_DESCRIPTION("Huawei Rainbow Module");
