/*
 * rtg_cgroup.h
 *
 * Default cgroup rtg declaration
 *
 * Copyright (c) 2019-2020 Huawei Technologies Co., Ltd.
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
#ifndef HW_CGROUP_RTG_H
#define HW_CGROUP_RTG_H
#include <linux/types.h>

#ifdef CONFIG_HW_CGROUP_RTG
#include "rtg.h"
#ifdef CONFIG_ARM
#include <linux/version.h>
#endif

void _do_update_preferred_cluster(struct related_thread_group *grp);
void do_update_preferred_cluster(struct related_thread_group *grp);
int call_create_default_coloc_group(void);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && \
        (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
int preferred_skip_min(struct walt_sched_cluster *cluster, struct task_struct *p);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
int preferred_skip_min(struct sched_cluster *cluster, struct task_struct *p);
#endif

#else

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && \
        (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static inline int
preferred_skip_min(struct walt_sched_cluster *cluster, struct task_struct *p)
{
	return 1;
}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static inline int
int preferred_skip_min(struct sched_cluster *cluster, struct task_struct *p)
{
	return 1;
}
#endif

#endif

void set_cgroup_rtg_switch(bool enable);
bool get_cgroup_rtg_switch(void);
#endif
