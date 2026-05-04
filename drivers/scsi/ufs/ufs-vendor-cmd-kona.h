/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: ufs command header file
 * Author: wengyang
 * Create: 2021-5-25
 */

#ifndef LINUX_UFS_VENOR_CMD_H
#define LINUX_UFS_VENOR_CMD_H

#include <scsi/ufs/ioctl.h>
#include <scsi/ufs/ufs.h>
#include "ufshcd.h"

struct ufs_query_vcmd {
	enum query_opcode opcode;
	u8 idn;
	u8 index;
	u8 selector;
	u8 query_func;
	u8 lun;
	bool has_data;
	__be32 value;
	__be16 reserved_osf;
	__be32 reserved[2];

	u8 *desc_buf;
	int buf_len;
	struct ufs_query_res *response;
};

int ufshcd_ioctl_query_vcmd(struct ufs_hba *hba,
			    struct ufs_ioctl_query_data *ioctl_data,
			    void __user *buffer);

#if defined(CONFIG_SCSI_UFS_HI1861_VCMD) && defined(CONFIG_PLATFORM_DIEID)
void hufs_bootdevice_get_dieid(struct ufs_hba *hba,
				struct ufs_dev_desc *dev_desc);
#else
static inline  __attribute__((unused)) void
hufs_bootdevice_get_dieid(struct ufs_hba *hba __attribute__((unused)),
				struct ufs_dev_desc *dev_desc __attribute__((unused)))
{
	return;
}
#endif

#endif