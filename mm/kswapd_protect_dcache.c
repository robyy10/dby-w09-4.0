 /*
  * Copyright (c) Huawei Technologies Co., Ltd. 2021-2022. All rights reserved.
  * Description: Protect some fs dcaches to speed up Kswapd
  * Author: Ye Wendong <yewendong1@huawei.com>
  * Create: 2023-01-18
  */
#include <linux/kswapd_protect_dcache.h>

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sysctl.h>
#include <linux/version.h>

static unsigned int procfs_cache_scan_ratio = 10;
static int one_hundred = 100;

#if !(defined(SYSCTL_ZERO) && defined(SYSCTL_ONE))
static int sysctl_vals[] = { 0, 1, INT_MAX };
#define SYSCTL_ZERO	((void *)&sysctl_vals[0])
#define SYSCTL_ONE	((void *)&sysctl_vals[1])
#define SYSCTL_INT_MAX	((void *)&sysctl_vals[2])
#endif

static struct ctl_table kswapd_protect_dcache_table[] = {
	{
		.procname	= "procfs_cache_scan_ratio",
		.data		= &procfs_cache_scan_ratio,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
		.extra2		= &one_hundred,
	},
	{ }
};

static struct ctl_table fs_table[] = {
	{
		.procname	= "fs",
		.mode		= 0555,
		.child		= kswapd_protect_dcache_table,
	},
	{ }
};

long vfs_scan_count(struct super_block *sb, struct shrinker *shrink, long total_objects)
{
	if (sb->s_type == get_fs_type("proc") && total_objects > shrink->batch)
		total_objects /= procfs_cache_scan_ratio;
	else
		total_objects = vfs_pressure_ratio(total_objects);
	return total_objects;
}

static int __init kswapd_protect_dcache_init(void)
{
	register_sysctl_table(fs_table);
	return 0;
}
module_init(kswapd_protect_dcache_init);
