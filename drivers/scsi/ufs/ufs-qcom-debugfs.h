/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, Linux Foundation. All rights reserved.
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

#ifndef QCOM_DEBUGFS_H_
#define QCOM_DEBUGFS_H_

#include "ufshcd.h"

#ifdef CONFIG_DEBUG_FS
void set_ufs_bootdevice_manfid(unsigned int manfid);
unsigned int get_ufs_bootdevice_manfid(void);
void set_ufs_bootdevice_hufs_dieid(char *hufs_dieid);
void set_ufs_bootdevice_product_name(char *product_name);
void set_ufs_bootdevice_size(sector_t size);
void ufs_qcom_dbg_add_debugfs(struct ufs_hba *hba, struct dentry *root);
void set_ufs_fw_version(char *fw_version);
int ufs_debug_proc_init_bootdevice(void);
#endif

#endif /* End of Header */
