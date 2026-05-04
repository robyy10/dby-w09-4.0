/*
 * walt_hw.c
 *
 * common platform WALT source file
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
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

#ifdef CONFIG_HW_SCHED_WALT

#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/version.h>

#ifdef CONFIG_HW_SCHED_CHECK_IRQLOAD
int walt_cpu_overload_irqload(int cpu)
{
	return walt_irqload(cpu) >= sysctl_sched_walt_cpu_overload_irqload;
}
#else
int walt_cpu_overload_irqload(int cpu)
{
	return 0;
}
#endif

int sysctl_sched_walt_init_task_load_pct_sysctl_handler(struct ctl_table *table,
	int write, void __user *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec(table, write, buffer, length, ppos);
	if (rc)
		return rc;

#ifdef CONFIG_HAVE_QCOM_SCHED_WALT
	sysctl_sched_init_task_load_pct = sysctl_sched_walt_init_task_load_pct;
#endif

	return 0;
}


ssize_t walt_init_task_load_pct_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
#ifdef CONFIG_HAVE_QCOM_SCHED_WALT
	sysctl_sched_walt_init_task_load_pct = sysctl_sched_init_task_load_pct;
#endif
	return (ssize_t)sprintf_s(buf, PAGE_SIZE, "%u\n", sysctl_sched_walt_init_task_load_pct);
}

ssize_t walt_init_task_load_pct_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	sysctl_sched_walt_init_task_load_pct = (unsigned int)val;
#ifdef CONFIG_HAVE_QCOM_SCHED_WALT
	sysctl_sched_init_task_load_pct = sysctl_sched_walt_init_task_load_pct;
#endif
	return count;
}

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) && defined(CONFIG_HW_TASK_RAVG_SUM) && defined(CONFIG_ARCH_QCOM)
static bool account_busy_for_task_ravg_sum(struct task_struct *p, int event)
{
	/* No need to bother updating task demand for exiting tasks
	 * or the idle task. */
	if (exiting_task(p) || is_idle_task(p))
		return false;

	/* When a task is waking up it is completing a segment of non-busy
	 * time. Likewise, if wait time is not treated as busy time, then
	 * when a task begins to run or is migrated, it is not running and
	 * is completing a segment of non-busy time. */
	if (event == TASK_WAKE || event == PICK_NEXT_TASK || event == TASK_MIGRATE)
		return false;

	return true;
}

static void add_to_task_ravg_sum(struct rq *rq, struct task_struct *p,
				u64 delta, int event)
{
	if (!account_busy_for_task_ravg_sum(p, event))
		return;

	delta = scale_exec_time(delta, rq);
	p->ravg.ravg_sum += delta;
}
#endif
