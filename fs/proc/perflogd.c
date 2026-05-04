#include <linux/cpufreq_times.h>
#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/stat.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/irqnr.h>
#include <linux/sched/cputime.h>
#include <linux/tick.h>
#include <linux/uaccess.h>
#include "securec.h"

#define DMIPS_NUM 16
#define MIN_CPUTIME_UID 1000
#define MIN_CPUTIME 2000
#define INIT_PID 1
#define KTHREADD_PID 2

char dmips_value_buffer[DMIPS_NUM];

static int show_perflogd(struct seq_file *p, void *v)
{
	unsigned long proc_id;
	unsigned long proc_cputime_total;
	struct task_struct *task;

	proc_cputime_total = 0;
	rcu_read_lock();
	task = &init_task;
	for_each_process(task) {
		if (task->tgid == INIT_PID || task->tgid == KTHREADD_PID)
			continue;
		if (task->real_parent && task->real_parent->tgid == KTHREADD_PID && !strstr(task->comm, "swapd"))
			continue;
		proc_cputime_total = get_proc_cpu_load(task, dmips_value_buffer, DMIPS_NUM);
		if (proc_cputime_total > 0) {
			proc_id = task->pid;
			seq_printf(p, "%lu ", proc_id);
			seq_printf(p, "%lu\n", proc_cputime_total);
		}
	}
	rcu_read_unlock();
	return 0;
}

static int perflogd_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_perflogd, NULL);
}

static ssize_t perflogd_write(struct file *filp, const char __user *userbuf, size_t count, loff_t *ppos)
{
	int ret;
	ret = memset_s(dmips_value_buffer, DMIPS_NUM, 0, sizeof(dmips_value_buffer));
	if (ret != 0) {
		printk(KERN_ERR "perflogd memset_s failed\n");
		return -EFAULT;
	}
	if ((count < 0) || (count > DMIPS_NUM)) {
		printk(KERN_ERR "perflogd invalid count\n");
		return -EFAULT;
	}
	if (copy_from_user(dmips_value_buffer, userbuf, count)) {
		printk(KERN_ERR "perflogd copy_from_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static const struct file_operations proc_perflogd_operations = {
	.open		= perflogd_open,
	.write		= perflogd_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int show_perflogd_cputime(struct seq_file *p, void *v)
{
	unsigned long proc_id;
	u64 clock_time;
	struct task_struct *task;
	u64 utime;
	u64 stime;
	u32 uid;

	rcu_read_lock();
	task = &init_task;
	for_each_process(task) {
		uid = task_uid(task).val;
		if (uid < MIN_CPUTIME_UID)
			continue;
		utime = 0;
		stime = 0;
		proc_id = task->pid;
		thread_group_cputime_adjusted(task, &utime, &stime);
		clock_time = nsec_to_clock_t(utime + stime);
		if (clock_time < MIN_CPUTIME)
			continue;
		seq_printf(p, "%lu ", proc_id);
		seq_printf(p, "%lu ", uid);
		seq_printf(p, "%llu\n", clock_time);
	}
	rcu_read_unlock();
	return 0;
}

static int perflogd_cputime_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_perflogd_cputime, NULL);
}

static const struct file_operations proc_perflogd_cputime_operations = {
	.open		= perflogd_cputime_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_perflogd_init(void)
{
	proc_create("perflogd", 0446, NULL, &proc_perflogd_operations);
	proc_create("perflogd_cputime", 0446, NULL, &proc_perflogd_cputime_operations);
	return 0;
}
module_init(proc_perflogd_init);

