/*
 * microphone_debug.c
 *
 * microphone debug driver
 *
 * Copyright (c) 2012-2019 Huawei Technologies Co., Ltd.
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
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <securec.h>
#include <huawei_platform/log/hw_log.h>
#include "microphone_debug.h"

#define HWLOG_TAG microphone_debug
HWLOG_REGIST();

#define INT_HEX_STR_SIZE 5
struct microphone_debug {
	struct dentry *df_dir;
	atomic_t state;
};

static struct microphone_debug g_microphone_debug;

void microphone_debug_set_state(int state)
{
	switch (state) {
	case 0:
		atomic_set(&g_microphone_debug.state, MICROPHONE_DEBUG_BUILTIN_MIC_GOOD);
		break;
	case 1:
		atomic_set(&g_microphone_debug.state, MICROPHONE_DEBUG_BUILTIN_MIC_BAD);
		break;
	default:
		hwlog_err("state error %d\n", state);
		break;
	}
}
EXPORT_SYMBOL_GPL(microphone_debug_set_state);//lint !e580

void microphone_debug_input_set_state(int state)
{
	microphone_debug_set_state(state);
}
EXPORT_SYMBOL_GPL(microphone_debug_input_set_state);//lint !e580

static ssize_t microphone_debug_state_read(struct file *file,
		char __user *user_buf, size_t count,
		loff_t *ppos)
{
	char kn_buf[INT_HEX_STR_SIZE] = { 0 };
	ssize_t byte_read;

	snprintf_s(kn_buf, INT_HEX_STR_SIZE, INT_HEX_STR_SIZE - 1, "%d",
			atomic_read(&g_microphone_debug.state));
	byte_read = simple_read_from_buffer(user_buf, count, ppos,
			kn_buf, strlen(kn_buf));

	return byte_read;
}

static ssize_t microphone_debug_state_write(struct file *file,
		const char __user *user_buf,
		size_t count, loff_t *ppos)
{
	char kn_buf[INT_HEX_STR_SIZE] = { 0 };
	ssize_t byte_writen;
	int status;
	int ret;

	byte_writen = simple_write_to_buffer(kn_buf,
			INT_HEX_STR_SIZE - 1, ppos,
			user_buf, count);
	if (byte_writen != count) {
		hwlog_err("simple_write_to_buffer err:%zd\n", byte_writen);
		return -ENOMEM;
	}

	ret = kstrtouint(kn_buf, 0, &status);
	if (ret) {
		hwlog_err("kstrtouint error %d\n", ret);
		return -EINVAL;
	}
	switch (status) {
	case MICROPHONE_DEBUG_BUILTIN_MIC_GOOD:
	case MICROPHONE_DEBUG_BUILTIN_MIC_BAD:
		atomic_set(&g_microphone_debug.state, status);
		break;
	default:
		hwlog_err("error code %d\n", status);
		break;
	}

	return byte_writen;
}

/*lint -e785 */
static const struct file_operations microphone_debug_state_fops = {
	.read = microphone_debug_state_read,
	.write = microphone_debug_state_write,
};

static void microphone_debug_init_fs(void)
{
	g_microphone_debug.df_dir = debugfs_create_dir("microphone", NULL);
	if (!g_microphone_debug.df_dir) {
		hwlog_err("create microphone debugfs dir\n");
		return;
	}

	if (!debugfs_create_file("state", 0644, g_microphone_debug.df_dir,
				NULL, &microphone_debug_state_fops)) {
		hwlog_err("create microphone debugfs file\n");
		debugfs_remove_recursive(g_microphone_debug.df_dir);
		return;
	}
	atomic_set(&g_microphone_debug.state, 0);
}

static int microphone_debug_init(void)
{
	microphone_debug_init_fs();
	return 0;
}
EXPORT_SYMBOL_GPL(microphone_debug_init);

static void microphone_debug_uninit(void)
{
	// debugfs_remove_recursive(NULL) is safe, check not required
	debugfs_remove_recursive(g_microphone_debug.df_dir);
	g_microphone_debug.df_dir = NULL;
}
EXPORT_SYMBOL_GPL(microphone_debug_uninit);//lint !e580

subsys_initcall_sync(microphone_debug_init);
module_exit(microphone_debug_uninit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("microphone debug driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
