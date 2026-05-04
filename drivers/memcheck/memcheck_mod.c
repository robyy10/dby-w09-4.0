/*
 * memcheck_mod.c
 *
 * memory leak detect
 *
 * Copyright (c) 2021-2022 Huawei Technologies Co., Ltd.
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
#include <linux/version.h>
#ifdef CONFIG_ANDROID_VENDOR_HOOKS
#include <platform/trace/hooks/memcheck.h>
#else
#include <platform/trace/events/memcheck.h>
#endif
#include "memcheck_account.h"
#include "memcheck_log_cma.h"

static void hook_mm_mem_stats_show(void *ignore, int unused)
{
	memcheck_stats_show();
}

static void hook_cma_report(void *ignore, char *name, unsigned long total,
			    unsigned long req)
{
	memcheck_cma_report(name, total, req);
}

static void hook_slub_obj_report(void *ignore, struct kmem_cache *s)
{
	memcheck_slub_obj_report(s);
}

static void hook_lowmem_report(void *ignore, struct task_struct *p,
				 unsigned long points)
{
	memcheck_lowmem_report(p, points);
}

static int __init dfx_memcheck_init(void)
{
	memcheck_createfs();
	register_trace_mm_mem_stats_show(hook_mm_mem_stats_show, NULL);
	register_trace_cma_report(hook_cma_report, NULL);
	register_trace_slub_obj_report(hook_slub_obj_report, NULL);
	register_trace_lowmem_report(hook_lowmem_report, NULL);
	return 0;
}
module_init(dfx_memcheck_init);

static void __exit dfx_memcheck_exit(void)
{
#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
	unregister_trace_mm_mem_stats_show(hook_mm_mem_stats_show, NULL);
	unregister_trace_cma_report(hook_cma_report, NULL);
	unregister_trace_slub_obj_report(hook_slub_obj_report, NULL);
	unregister_trace_lowmem_report(hook_lowmem_report, NULL);
#endif
}
module_exit(dfx_memcheck_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
MODULE_DESCRIPTION("DFX Memcheck Module");
