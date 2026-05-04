// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, 2017-2018, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include "ufs-qcom.h"
#include "ufs-qcom-debugfs.h"
#include "ufs-debugfs.h"
#include <linux/proc_fs.h>

#define TESTBUS_CFG_BUFF_LINE_SIZE	sizeof("0xXY, 0xXY")
#define MAX_NAME_LEN 32
#define MAX_PRL_LEN 5
#define MAX_DIEID_LEN 800

struct __bootdevice {
	char product_name[MAX_NAME_LEN + 1];
	sector_t size;
	unsigned int manfid;
	char fw_version[MAX_PRL_LEN + 1];
	char hufs_dieid[MAX_DIEID_LEN + 1];
};

static struct __bootdevice bootdevice;

static void ufs_qcom_dbg_remove_debugfs(struct ufs_qcom_host *host);

static int ufs_qcom_dbg_print_en_read(void *data, u64 *attr_val)
{
	struct ufs_qcom_host *host = data;

	if (!host)
		return -EINVAL;

	*attr_val = (u64)host->dbg_print_en;
	return 0;
}

static int ufs_qcom_dbg_print_en_set(void *data, u64 attr_id)
{
	struct ufs_qcom_host *host = data;

	if (!host)
		return -EINVAL;

	if (attr_id & ~UFS_QCOM_DBG_PRINT_ALL)
		return -EINVAL;

	host->dbg_print_en = (u32)attr_id;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(ufs_qcom_dbg_print_en_ops,
			ufs_qcom_dbg_print_en_read,
			ufs_qcom_dbg_print_en_set,
			"%llu\n");

static int ufs_qcom_dbg_testbus_en_read(void *data, u64 *attr_val)
{
	struct ufs_qcom_host *host = data;
	bool enabled;

	if (!host)
		return -EINVAL;

	enabled = !!(host->dbg_print_en & UFS_QCOM_DBG_PRINT_TEST_BUS_EN);
	*attr_val = (u64)enabled;
	return 0;
}

static int ufs_qcom_dbg_testbus_en_set(void *data, u64 attr_id)
{
	struct ufs_qcom_host *host = data;
	int ret = 0;

	if (!host)
		return -EINVAL;

	if (!!attr_id)
		host->dbg_print_en |= UFS_QCOM_DBG_PRINT_TEST_BUS_EN;
	else
		host->dbg_print_en &= ~UFS_QCOM_DBG_PRINT_TEST_BUS_EN;

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);
	ret = ufs_qcom_testbus_config(host);
	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(ufs_qcom_dbg_testbus_en_ops,
			ufs_qcom_dbg_testbus_en_read,
			ufs_qcom_dbg_testbus_en_set,
			"%llu\n");

static int ufs_qcom_dbg_testbus_cfg_show(struct seq_file *file, void *data)
{
	struct ufs_qcom_host *host = (struct ufs_qcom_host *)file->private;

	seq_printf(file, "Current configuration: major=%d, minor=%d\n\n",
			host->testbus.select_major, host->testbus.select_minor);

	/* Print usage */
	seq_puts(file,
		"To change the test-bus configuration, write 'MAJ,MIN' where:\n"
		"MAJ - major select\n"
		"MIN - minor select\n\n");
	return 0;
}

static ssize_t ufs_qcom_dbg_testbus_cfg_write(struct file *file,
				const char __user *ubuf, size_t cnt,
				loff_t *ppos)
{
	struct ufs_qcom_host *host = file->f_mapping->host->i_private;
	char configuration[TESTBUS_CFG_BUFF_LINE_SIZE] = {'\0'};
	loff_t buff_pos = 0;
	char *comma;
	int ret = 0;
	int major;
	int minor;
	unsigned long flags;
	struct ufs_hba *hba = host->hba;


	ret = simple_write_to_buffer(configuration,
		TESTBUS_CFG_BUFF_LINE_SIZE - 1,

		&buff_pos, ubuf, cnt);
	if (ret < 0) {
		dev_err(host->hba->dev, "%s: failed to read user data\n",
			__func__);
		goto out;
	}
	configuration[ret] = '\0';

	comma = strnchr(configuration, TESTBUS_CFG_BUFF_LINE_SIZE, ',');
	if (!comma || comma == configuration) {
		dev_err(host->hba->dev,
			"%s: error in configuration of testbus\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	if (sscanf(configuration, "%i,%i", &major, &minor) != 2) {
		dev_err(host->hba->dev,
			"%s: couldn't parse input to 2 numeric values\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}

	if (!ufs_qcom_testbus_cfg_is_ok(host, major, minor)) {
		ret = -EPERM;
		goto out;
	}

	spin_lock_irqsave(hba->host->host_lock, flags);
	host->testbus.select_major = (u8)major;
	host->testbus.select_minor = (u8)minor;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	/*
	 * Sanity check of the {major, minor} tuple is done in the
	 * config function
	 */
	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);
	ret = ufs_qcom_testbus_config(host);
	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);
	if (!ret)
		dev_dbg(host->hba->dev,
				"%s: New configuration: major=%d, minor=%d\n",
				__func__, host->testbus.select_major,
				host->testbus.select_minor);

out:
	return ret ? ret : cnt;
}

static int ufs_qcom_dbg_testbus_cfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_qcom_dbg_testbus_cfg_show,
				inode->i_private);
}

static const struct file_operations ufs_qcom_dbg_testbus_cfg_desc = {
	.open		= ufs_qcom_dbg_testbus_cfg_open,
	.read		= seq_read,
	.write		= ufs_qcom_dbg_testbus_cfg_write,
};

static int ufs_qcom_dbg_testbus_bus_read(void *data, u64 *attr_val)
{
	struct ufs_qcom_host *host = data;

	if (!host)
		return -EINVAL;

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);
	*attr_val = (u64)ufshcd_readl(host->hba, UFS_TEST_BUS);
	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(ufs_qcom_dbg_testbus_bus_ops,
			ufs_qcom_dbg_testbus_bus_read,
			NULL,
			"%llu\n");

static int ufs_qcom_dbg_dbg_regs_show(struct seq_file *file, void *data)
{
	struct ufs_qcom_host *host = (struct ufs_qcom_host *)file->private;
	bool dbg_print_reg = !!(host->dbg_print_en &
				UFS_QCOM_DBG_PRINT_REGS_EN);

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);

	/* Temporarily override the debug print enable */
	host->dbg_print_en |= UFS_QCOM_DBG_PRINT_REGS_EN;
	ufs_qcom_print_hw_debug_reg_all(host->hba, file, ufsdbg_pr_buf_to_std);
	/* Restore previous debug print enable value */
	if (!dbg_print_reg)
		host->dbg_print_en &= ~UFS_QCOM_DBG_PRINT_REGS_EN;

	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);

	return 0;
}

static int ufs_qcom_dbg_dbg_regs_open(struct inode *inode,
					      struct file *file)
{
	return single_open(file, ufs_qcom_dbg_dbg_regs_show,
				inode->i_private);
}

static const struct file_operations ufs_qcom_dbg_dbg_regs_desc = {
	.open		= ufs_qcom_dbg_dbg_regs_open,
	.read		= seq_read,
};

static int ufs_qcom_dbg_pm_qos_show(struct seq_file *file, void *data)
{
	struct ufs_qcom_host *host = (struct ufs_qcom_host *)file->private;
	unsigned long flags;
	int i;

	spin_lock_irqsave(host->hba->host->host_lock, flags);

	seq_printf(file, "enabled: %d\n", host->pm_qos.is_enabled);
	for (i = 0; i < host->pm_qos.num_groups && host->pm_qos.groups; i++)
		seq_printf(file,
			"CPU Group #%d(mask=0x%lx): active_reqs=%d, state=%d, latency=%d\n",
			i, host->pm_qos.groups[i].mask.bits[0],
			host->pm_qos.groups[i].active_reqs,
			host->pm_qos.groups[i].state,
			host->pm_qos.groups[i].latency_us);

	spin_unlock_irqrestore(host->hba->host->host_lock, flags);

	return 0;
}

static int ufs_qcom_dbg_pm_qos_open(struct inode *inode,
					      struct file *file)
{
	return single_open(file, ufs_qcom_dbg_pm_qos_show, inode->i_private);
}

static const struct file_operations ufs_qcom_dbg_pm_qos_desc = {
	.open		= ufs_qcom_dbg_pm_qos_open,
	.read		= seq_read,
};

#define UFS_PROC_SHOW(name, fmt, args...) \
static int ufs_##name##_show(struct seq_file *m, void *v) \
{ \
	if (ufs_qcom_hba) \
		seq_printf(m, fmt, args); \
	return 0; \
} \
static int ufs_##name##_open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, ufs_##name##_show, inode->i_private); \
} \
static const struct file_operations name##_fops = { \
	.open = ufs_##name##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

/*These two TYPES are requested by HUIWEI normalized project*/
#define  EMMC_TYPE      0
#define  UFS_TYPE       1
#define BOOTTYPE UFS_TYPE

static int ufs_cid_show(struct seq_file *m, void *v)
{
	u32 cid[4];
	int i;

	if (ufs_qcom_hba) {
		memcpy(cid, (u32 *)&ufs_qcom_hba->unique_number, sizeof(cid));
		for (i = 0; i < 3; i++)
			cid[i] = be32_to_cpu(cid[i]);
		cid[3] = (((cid[3]) & 0xffff) << 16) | (((cid[3]) >> 16) & 0xffff);
		seq_printf(m, "%08x%08x%08x%08x\n", cid[0], cid[1], cid[2], cid[3]);
	}
	return 0;
}
static int ufs_cid_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_cid_show, inode->i_private);
}
static const struct file_operations cid_fops = {
	.open = ufs_cid_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void set_ufs_bootdevice_manfid(unsigned int manfid)
{
	bootdevice.manfid = manfid;
}

unsigned int get_ufs_bootdevice_manfid(void)
{
	return bootdevice.manfid;
}

static int ufs_manfid_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%06x\n", bootdevice.manfid);
	return 0;
}

static int ufs_manfid_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_manfid_show, inode->i_private);
}

static const struct file_operations manfid_fops = {
	.open		= ufs_manfid_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void set_ufs_bootdevice_size(sector_t size)
{
	bootdevice.size = size;
}

sector_t get_ufs_bootdevice_size()
{
	return bootdevice.size;
}

static int ufs_size_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%llu\n", (unsigned long long)bootdevice.size);
	return 0;
}

static int ufs_size_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_size_show, inode->i_private);
}

static const struct file_operations size_fops = {
	.open		= ufs_size_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ufs_name_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", "qcom-platform");
	return 0;
}

static int ufs_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_name_show, inode->i_private);
}

static const struct file_operations name_fops = {
	.open = ufs_name_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void set_ufs_bootdevice_hufs_dieid(char *hufs_dieid)
{
	strlcpy(bootdevice.hufs_dieid, hufs_dieid, sizeof(bootdevice.hufs_dieid));
}

static int hufs_dieid_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", bootdevice.hufs_dieid);
	return 0;
}

static int hufs_dieid_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, hufs_dieid_proc_show, inode->i_private);
}

static const struct file_operations hufs_dieid_proc_fops = {
	.open		= hufs_dieid_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void set_ufs_bootdevice_product_name(char *product_name)
{
	strlcpy(bootdevice.product_name,
		product_name,
		sizeof(bootdevice.product_name));
}

void get_ufs_bootdevice_product_name(char* product_name, u32 len)
{
	strlcpy(product_name, bootdevice.product_name, len); /* [false alarm] */
}

static int ufs_product_name_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s", bootdevice.product_name);
	return 0;
}

static int ufs_product_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_product_name_show, inode->i_private);
}

static const struct file_operations product_name_fops = {
	.open		= ufs_product_name_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void set_ufs_fw_version(char *fw_version)
{
	strlcpy(bootdevice.fw_version, fw_version, sizeof(bootdevice.fw_version));
}

static int ufs_fw_version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s", bootdevice.fw_version);
	return 0;
}
static int ufs_fw_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_fw_version_show, inode->i_private);
}
static const struct file_operations fw_version_fops = {
	.open = ufs_fw_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ufs_pre_eol_info_show(struct seq_file *m, void *v)
{
	int err;
	int buff_len = QUERY_DESC_HEALTH_DEF_SIZE;
	u8 desc_buf[QUERY_DESC_HEALTH_DEF_SIZE];

	if (ufs_qcom_hba) {
		pm_runtime_get_sync(ufs_qcom_hba->dev);
		err = ufshcd_read_health_desc(ufs_qcom_hba, desc_buf, buff_len);
		pm_runtime_put_sync(ufs_qcom_hba->dev);
		if (err) {
			seq_printf(m, "Reading Health Descriptor failed. err = %d\n",
				err);
			return err;
		}
		seq_printf(m, "0x%02x\n", (u8)desc_buf[2]);
	}
	return 0;
}
static int ufs_pre_eol_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_pre_eol_info_show, inode->i_private);
}
static const struct file_operations pre_eol_info_fops = {
	.open = ufs_pre_eol_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ufs_life_time_est_typ_a_show(struct seq_file *m, void *v)
{
	int err;
	int buff_len = QUERY_DESC_HEALTH_DEF_SIZE;
	u8 desc_buf[QUERY_DESC_HEALTH_DEF_SIZE];

	if (ufs_qcom_hba) {
		pm_runtime_get_sync(ufs_qcom_hba->dev);
		err = ufshcd_read_health_desc(ufs_qcom_hba, desc_buf, buff_len);
		pm_runtime_put_sync(ufs_qcom_hba->dev);
		if (err) {
			seq_printf(m, "Reading Health Descriptor failed. err = %d\n",
				err);
			return err;
		}
		seq_printf(m, "0x%02x\n", (u8)desc_buf[3]);
	}
	return 0;
}
static int ufs_life_time_est_typ_a_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_life_time_est_typ_a_show, inode->i_private);
}
static const struct file_operations life_time_est_typ_a_fops = {
	.open = ufs_life_time_est_typ_a_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ufs_life_time_est_typ_b_show(struct seq_file *m, void *v)
{
	int err;
	int buff_len = QUERY_DESC_HEALTH_DEF_SIZE;
	u8 desc_buf[QUERY_DESC_HEALTH_DEF_SIZE];

	if (ufs_qcom_hba) {
		pm_runtime_get_sync(ufs_qcom_hba->dev);
		err = ufshcd_read_health_desc(ufs_qcom_hba, desc_buf, buff_len);
		pm_runtime_put_sync(ufs_qcom_hba->dev);
		if (err) {
			seq_printf(m, "Reading Health Descriptor failed. err = %d\n",
				err);
			return err;
		}
		seq_printf(m, "0x%02x\n", (u8)desc_buf[4]);
	}
	return 0;
}
static int ufs_life_time_est_typ_b_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_life_time_est_typ_b_show, inode->i_private);
}
static const struct file_operations life_time_est_typ_b_fops = {
	.open = ufs_life_time_est_typ_b_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

UFS_PROC_SHOW(type, "%d\n", BOOTTYPE);

static const char * const ufs_proc_list[] = {
	"cid",
	"type",
	"manfid",
	"size",
	"product_name",
	"rev",
	"pre_eol_info",
	"life_time_est_typ_a",
	"life_time_est_typ_b",
	"name",
	"hufs_dieid",
};
static const struct file_operations *proc_fops_list[] = {
	&cid_fops,
	&type_fops,
	&manfid_fops,
	&size_fops,
	&product_name_fops,
	&fw_version_fops,
	&pre_eol_info_fops,
	&life_time_est_typ_a_fops,
	&life_time_est_typ_b_fops,
	&name_fops,
	&hufs_dieid_proc_fops,
};

int ufs_debug_proc_init_bootdevice(void)
{
	struct proc_dir_entry *prEntry;
	struct proc_dir_entry *bootdevice_dir;
	int i, num;

	bootdevice_dir = proc_mkdir("bootdevice", NULL);

	if (!bootdevice_dir) {
		pr_notice("[%s]: failed to create /proc/bootdevice\n",
			__func__);
		return -1;
	}

	num = ARRAY_SIZE(ufs_proc_list);
	for (i = 0; i < num; i++) {
		prEntry = proc_create(ufs_proc_list[i], 0,
			bootdevice_dir, proc_fops_list[i]);
		if (prEntry)
			continue;
		pr_notice(
			"[%s]: failed to create /proc/bootdevice/%s\n",
			__func__, ufs_proc_list[i]);
	}

	return 0;
}
void ufs_qcom_dbg_add_debugfs(struct ufs_hba *hba, struct dentry *root)
{
	struct ufs_qcom_host *host;

	if (!hba || !hba->priv) {
		pr_err("%s: NULL host, exiting\n", __func__);
		return;
	}

	host = hba->priv;
	host->debugfs_files.debugfs_root = debugfs_create_dir("qcom", root);
	if (IS_ERR(host->debugfs_files.debugfs_root))
		/* Don't complain -- debugfs just isn't enabled */
		goto err_no_root;
	if (!host->debugfs_files.debugfs_root) {
		/*
		 * Complain -- debugfs is enabled, but it failed to
		 * create the directory
		 */
		dev_err(host->hba->dev,
			"%s: NULL debugfs root directory, exiting\n", __func__);
		goto err_no_root;
	}

	host->debugfs_files.dbg_print_en =
		debugfs_create_file("dbg_print_en", 0600,
				    host->debugfs_files.debugfs_root, host,
				    &ufs_qcom_dbg_print_en_ops);
	if (!host->debugfs_files.dbg_print_en) {
		dev_err(host->hba->dev,
			"%s: failed to create dbg_print_en debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus = debugfs_create_dir("testbus",
					host->debugfs_files.debugfs_root);
	if (!host->debugfs_files.testbus) {
		dev_err(host->hba->dev,
			"%s: failed create testbus directory\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus_en =
		debugfs_create_file("enable", 0600,
				    host->debugfs_files.testbus, host,
				    &ufs_qcom_dbg_testbus_en_ops);
	if (!host->debugfs_files.testbus_en) {
		dev_err(host->hba->dev,
			"%s: failed create testbus_en debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus_cfg =
		debugfs_create_file("configuration", 0600,
				    host->debugfs_files.testbus, host,
				    &ufs_qcom_dbg_testbus_cfg_desc);
	if (!host->debugfs_files.testbus_cfg) {
		dev_err(host->hba->dev,
			"%s: failed create testbus_cfg debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus_bus =
		debugfs_create_file("TEST_BUS", 0400,
				    host->debugfs_files.testbus, host,
				    &ufs_qcom_dbg_testbus_bus_ops);
	if (!host->debugfs_files.testbus_bus) {
		dev_err(host->hba->dev,
			"%s: failed create testbus_bus debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.dbg_regs =
		debugfs_create_file("debug-regs", 0400,
				    host->debugfs_files.debugfs_root, host,
				    &ufs_qcom_dbg_dbg_regs_desc);
	if (!host->debugfs_files.dbg_regs) {
		dev_err(host->hba->dev,
			"%s: failed create dbg_regs debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.pm_qos =
		debugfs_create_file("pm_qos", 0400,
				host->debugfs_files.debugfs_root, host,
				&ufs_qcom_dbg_pm_qos_desc);
		if (!host->debugfs_files.dbg_regs) {
			dev_err(host->hba->dev,
				"%s: failed create dbg_regs debugfs entry\n",
				__func__);
			goto err;
		}

	return;

err:
	ufs_qcom_dbg_remove_debugfs(host);
err_no_root:
	dev_err(host->hba->dev, "%s: failed to initialize debugfs\n", __func__);
}

static void ufs_qcom_dbg_remove_debugfs(struct ufs_qcom_host *host)
{
	debugfs_remove_recursive(host->debugfs_files.debugfs_root);
	host->debugfs_files.debugfs_root = NULL;
}
