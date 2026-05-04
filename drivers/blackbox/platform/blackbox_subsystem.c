/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Huawei Technologies Co., Ltd. All rights reserved.
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

#include <platform/linux/blackbox.h>
#include <platform/linux/blackbox_subsystem_def.h>
#include <platform/linux/blackbox_subsystem.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#define MAX_SSR_REASON_LEN 256U
struct crash_data {
	bool is_data_valid;
	char crash_reason[MAX_SSR_REASON_LEN];
	char log_dir[MAX_SSR_REASON_LEN];
};

struct subsys_list {
	char *subsys_name;
	unsigned int subsys_num;
	struct crash_data *crash_data;
};

static bool init_flag;
static struct crash_data adsp_data;

struct subsys_list find_subsys_number[] = {
	{"slpi", SLPI_SUBSYS_NUMBER, NULL},
	{"adsp", ADSP_SUBSYS_NUMBER, &adsp_data},
	{"cdsp", CDSP_SUBSYS_NUMBER, NULL},
	{"modem", MODEM_SUBSYS_NUMBER, NULL},
	{"wpss", WLAN_SUBSYS_NUMBER, NULL},
	{"bt", BT_SUBSYS_NUMBER, NULL},
	{"venus", VENUS_SUBSYS_NUMBER, NULL},
};

static struct work_struct log_work;
static struct workqueue_struct *log_work_queue;


static int bbox_find_subsys_number(const char *name)
{
	unsigned int i;
	unsigned int number = 0;
	for (i = 0; i < sizeof(find_subsys_number) / sizeof(struct subsys_list); i++) {
		if (!strcmp(name, find_subsys_number[i].subsys_name)) {
			number = find_subsys_number[i].subsys_num;
			break;
		}
	}
	bbox_print_info("bbox_find_subsys_number: %d\n", number);
	return number;
}

void save_crash_reason_data(const char *module_name, const char *reason, unsigned int len)
{
	if (!module_name || !reason)
		return;
	if (strcmp(module_name, "adsp") == 0) {
		adsp_data.is_data_valid = true;
		snprintf(adsp_data.crash_reason,
			((len > MAX_SSR_REASON_LEN) ? MAX_SSR_REASON_LEN : len), reason);
	}
	bbox_print_info("Bbox_subsys:save %s crash reason\n", module_name);
}
EXPORT_SYMBOL(save_crash_reason_data);

int bbox_subsystem_crash_notify(const char *name)
{
	char event[EVENT_MAX_LEN] = "NULL";
	char module[MODULE_MAX_LEN] = "NULL";
	char error_desc[ERROR_DESC_MAX_LEN] = "NULL";

	unsigned int number = bbox_find_subsys_number(name);

	switch (number) {
	case SLPI_SUBSYS_NUMBER:
		strncpy(event, EVENT_SLPI_CRASH, sizeof(event) - 1);
		strncpy(module, MODULE_SLPI, sizeof(module) - 1);
		strncpy(error_desc, ERROR_DESC_SLPI_CRASH, sizeof(error_desc) - 1);
		break;
	case ADSP_SUBSYS_NUMBER:
		strncpy(event, EVENT_ADSP_CRASH, sizeof(event) - 1);
		strncpy(module, MODULE_ADSP, sizeof(module) - 1);
		strncpy(error_desc, ERROR_DESC_ADSP_CRASH, sizeof(error_desc) - 1);
		break;
	case CDSP_SUBSYS_NUMBER:
		strncpy(event, EVENT_CDSP_CRASH, sizeof(event) - 1);
		strncpy(module, MODULE_CDSP, sizeof(module) - 1);
		strncpy(error_desc, ERROR_DESC_CDSP_CRASH, sizeof(error_desc) - 1);
		break;
	case MODEM_SUBSYS_NUMBER:
		strncpy(event, EVENT_MSS_CRASH, sizeof(event) - 1);
		strncpy(module, MODULE_MSS, sizeof(module) - 1);
		strncpy(error_desc, ERROR_DESC_MSS_CRASH, sizeof(error_desc) - 1);
		break;
	case WLAN_SUBSYS_NUMBER:
		strncpy(event, EVENT_WLAN_CRASH, sizeof(event) - 1);
		strncpy(module, MODULE_WLAN, sizeof(module) - 1);
		strncpy(error_desc, ERROR_DESC_WLAN_CRASH, sizeof(error_desc) - 1);
		break;
	case BT_SUBSYS_NUMBER:
		strncpy(event, EVENT_BT_CRASH, sizeof(event) - 1);
		strncpy(module, MODULE_BT, sizeof(module) - 1);
		strncpy(error_desc, ERROR_DESC_BT_CRASH, sizeof(error_desc) - 1);
		break;
	case VENUS_SUBSYS_NUMBER:
		strncpy(event, EVENT_VENUS_CRASH, sizeof(event) - 1);
		strncpy(module, MODULE_VENUS, sizeof(module) - 1);
		strncpy(error_desc, ERROR_DESC_VENUS_CRASH, sizeof(error_desc) - 1);
		break;
	default:
		bbox_print_err("bbox_cp:invalid event code: %lu!\n", number);
		break;
	}

	bbox_notify_error(event, module, error_desc, false);
	bbox_print_err("bbox_cp:bbox_notify_error %s\n", error_desc);

	return 0;
}
EXPORT_SYMBOL(bbox_subsystem_crash_notify);

static void dump(const char *log_dir, struct error_info *info)
{
	int ret;

	if (!init_flag)
		return;
	if (strcmp(info->module, MODULE_ADSP) == 0) {
		memset(adsp_data.log_dir, 0, MAX_SSR_REASON_LEN);
		ret = snprintf(adsp_data.log_dir, MAX_SSR_REASON_LEN, "%s", log_dir);
		queue_work(log_work_queue, &(log_work));
		bbox_print_info("Bbox_subsys: adsp_data.log_dir = %s, ret =%d\n",
			adsp_data.log_dir, ret);
	}
	bbox_print_info("Bbox_subsys: %s dump out\n", info->module);
}

static void reset(struct error_info *info)
{
	bbox_print_err("Bbox_subsys: Reset the cp!");
}

static void save_crash_data_file(int num, struct crash_data *crash_data)
{
	int ret;
	static mm_segment_t oldfs;
	struct file *fp = NULL;
	char file_path[MAX_SSR_REASON_LEN] = {0};
	char data[MAX_SSR_REASON_LEN] = {0};

	snprintf(file_path, MAX_SSR_REASON_LEN, "%s/exception", crash_data->log_dir);
	bbox_print_info("%s: file_path = %s\n", __func__, file_path);

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(file_path, O_CREAT | O_WRONLY | O_APPEND, 660);
	if (IS_ERR(fp)) {
		bbox_print_err("%s:filp_open failed:%s\n", __func__, file_path);
		set_fs(oldfs);
		return;
	}
	bbox_print_info("%s:start write log\n", __func__);
	vfs_llseek(fp, 0L, SEEK_END);
	ret = snprintf(data, MAX_SSR_REASON_LEN, "%s:%s\n",
		find_subsys_number[num].subsys_name, crash_data->crash_reason);
	if (ret > 0)
		(void)kernel_write(fp, data, ret, &(fp->f_pos));

	bbox_print_info("%s:write log end ret = %d\n", __func__, ret);
	filp_close(fp, NULL);
	set_fs(oldfs);

	memset(crash_data->crash_reason, 0, sizeof(crash_data->crash_reason));
	memset(crash_data->log_dir, 0, sizeof(crash_data->log_dir));
}

static void save_log_work_func(struct work_struct *work)
{
	struct crash_data *crash_data = NULL;
	unsigned int i;

	bbox_print_info("%s:in\n", __func__);
	for (i = 0; i < sizeof(find_subsys_number) / sizeof(struct subsys_list); i++) {
		if (!find_subsys_number[i].crash_data ||
			!find_subsys_number[i].crash_data->is_data_valid)
			continue;
		crash_data = find_subsys_number[i].crash_data;
		find_subsys_number[i].crash_data->is_data_valid = false;
		save_crash_data_file(i, crash_data);
	}
	bbox_print_info("%s:out\n", __func__);
}

static void init_save_log_work(void)
{
	if (init_flag)
		return;
	bbox_print_info("init log log_work\n");
	INIT_WORK(&log_work, save_log_work_func);
	log_work_queue = create_singlethread_workqueue("save_crash_log");
	if (!log_work_queue)
		return;
	init_flag = true;
}

void subsystem_register_module_ops(void)
{
#define subsys_module_ops(name) \
		struct module_ops ops_##name = { \
			.module = #name, \
			.dump = dump, \
			.reset = reset, \
			.get_last_log_info = NULL, \
			.save_last_log = NULL, \
		};
	subsys_module_ops(CP)
	subsys_module_ops(SLPI)
	subsys_module_ops(ADSP)
	subsys_module_ops(CDSP)
	subsys_module_ops(MODEM)
	subsys_module_ops(WLAN)
	subsys_module_ops(BT)
	subsys_module_ops(VENUS)

	init_save_log_work();
	if (!bbox_register_module_ops(&ops_CP))
		bbox_print_info("bbox_cp register ops succ");
	else
		bbox_print_info("bbox_cp register ops failed");

	if (!bbox_register_module_ops(&ops_SLPI))
		bbox_print_info("bbox_slpi register ops succ");
	else
		bbox_print_info("bbox_slpi register ops failed");

	if (!bbox_register_module_ops(&ops_ADSP))
		bbox_print_info("bbox_adsp register ops succ");
	else
		bbox_print_info("bbox_adsp register ops failed");

	if (!bbox_register_module_ops(&ops_CDSP))
		bbox_print_info("bbox_cdsp register ops succ");
	else
		bbox_print_info("bbox_cdsp register ops failed");

	if (!bbox_register_module_ops(&ops_MODEM))
		bbox_print_info("bbox_modem register ops succ");
	else
		bbox_print_info("bbox_modem register ops failed");

	if (!bbox_register_module_ops(&ops_WLAN))
		bbox_print_info("bbox_wlan register ops succ");
	else
		bbox_print_info("bbox_wlan register ops failed");

	if (!bbox_register_module_ops(&ops_BT))
		bbox_print_info("bbox_bt register ops succ");
	else
		bbox_print_info("bbox_bt register ops failed");

	if (!bbox_register_module_ops(&ops_VENUS))
		bbox_print_info("bbox_venus register ops succ");
	else
		bbox_print_info("bbox_venus register ops failed");
}
EXPORT_SYMBOL(subsystem_register_module_ops);

