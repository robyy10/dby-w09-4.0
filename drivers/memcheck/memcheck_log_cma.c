/*
 * memcheck_log_cma.c
 *
 * Get CMA memory info function
 *
 * Copyright(C) 2022 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/cma.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include "cma.h"
#include "huawei_platform/log/hiview_hievent.h"
#include "memcheck_ioctl.h"

#define CMA_DOMAIN "KERNEL_VENDOR"
#define CMA_STRING "CMA"
#define MAX_MSG_LEN 256

struct event_info {
	pid_t pid;
	pid_t tgid;
	char comm[TASK_COMM_LEN];
	char *name;
	unsigned long total;
	unsigned long req;
};

static struct work_struct cma_report_work;
static struct event_info event;

static void cma_report_work_func(struct work_struct *work)
{
	int ret;
	char msg[MAX_MSG_LEN] = { 0 };

	ret = snprintf(msg, MAX_MSG_LEN, "cma_area:%s alloc %ld failed, total is %ld",
		       event.name, event.req * PAGE_SIZE, event.total * PAGE_SIZE);
	if (ret <= 0)
		return;

	memcheck_info("domain: %s, stringid: %s, pid: %d, tgid: %d, name: %s",
		      CMA_DOMAIN, CMA_STRING, event.pid, event.tgid, event.comm);
	ret = hiview_send_hisysevent(CMA_DOMAIN, CMA_STRING, FAULT,
				     "%s,%d,%s,%d,%s,%s,%s,%s,%s,%s",
				     "PID", event.pid,
				     "UID", event.tgid,
				     "PACKAGE_NAME", event.comm,
				     "PROCESS_NAME", event.comm,
				     "MSG", msg);
	if (ret < 0)
		memcheck_err("send hisysevent fail, domain:%s, stringid:%s",
			     CMA_DOMAIN, CMA_STRING);
}

void memcheck_cma_report(char *name, unsigned long total, unsigned long req)
{
	static DEFINE_RATELIMIT_STATE(cma_report, 60 * 60 * HZ, 1); // report once maximum in one hour

	if (!__ratelimit(&cma_report))
		return;

	event.pid = current->pid;
	event.tgid = current->tgid;
	memcpy(event.comm, current->comm, TASK_COMM_LEN);
	event.name = name;
	event.total = total;
	event.req = req;
	schedule_work(&cma_report_work);
}

static unsigned long cma_get_used(struct cma *cma)
{
	unsigned long used;

	mutex_lock(&cma->lock);
	used = bitmap_weight(cma->bitmap, (int)cma_bitmap_maxno(cma));
	mutex_unlock(&cma->lock);

	return ((unsigned long)used << cma->order_per_bit) * PAGE_SIZE;
}

static int cma_area_cb(struct cma *cma, void *data)
{
	struct seq_file *s = (struct seq_file *)data;
	const char *name = cma_get_name(cma);
	unsigned long total = cma_get_size(cma);
	unsigned long used = cma_get_used(cma);

	if (s)
		seq_printf(s, "%30s %15lu %15lu %10d\n",
			   name, total, used, used * 100 / total);
	else
		memcheck_info("%30s %15lu %15lu %10d\n",
			   name, total, used, used * 100 / total);

	return 0;
}

static int cma_info_show(struct seq_file *s, void *d)
{
	unsigned long old_ns = ktime_get();

	if (s) {
		seq_puts(s, "CMA info:\n");
		seq_puts(s, "----------------------------------------------------\n");
		seq_printf(s, "%30s %15s %15s %10s\n",
			   "cma_area", "Total", "Used", "Percent");
	} else {
		memcheck_info("CMA info:\n");
		memcheck_info("----------------------------------------------------\n");
		memcheck_info("%30s %15s %15s %10s\n",
			"cma_area", "Total", "Used", "Percent");
	}

	cma_for_each_area(cma_area_cb, s);

	if (s)
		seq_puts(s, "----------------------------------------------------\n");
	else
		memcheck_info("----------------------------------------------------\n");

	memcheck_info("take %ld ns", ktime_get() - old_ns);

	return 0;
}

void memcheck_cma_info_show(void)
{
	cma_info_show(NULL, NULL);
}
EXPORT_SYMBOL(memcheck_cma_info_show);

static int cma_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, cma_info_show, PDE_DATA(file_inode(file)));
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops cma_info_fops = {
	.proc_open = cma_info_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations cma_info_fops = {
	.open = cma_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

int memcheck_cma_createfs(void)
{
	struct proc_dir_entry *entry = NULL;

	entry = proc_create_data("cma_info", 0444,
		NULL, &cma_info_fops, NULL);
	if (!entry)
		memcheck_err("Failed to create cma debug info\n");

	INIT_WORK(&cma_report_work, cma_report_work_func);

	return (!entry ? -EFAULT : 0);
}
