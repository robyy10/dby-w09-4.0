/*
* ufs_wb_dynamic_switch.h
* ufs Write Boster feature
*
* Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
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
#ifndef _UFS_WB_DYNAMIC_SWITCH_H_
#define _UFS_WB_DYNAMIC_SWITCH_H_

#include <linux/types.h>
#include <linux/bootdevice.h>

struct ufs_hba;
#ifdef CONFIG_HUAWEI_UFS_DYNAMIC_SWITCH_WB

#define WB_T_BUFFER_THRESHOLD 0x3
extern struct device_attribute *ufs_host_attrs[];

enum {
	MASK_EE_WRITEBOOSTER_EVENT = (1 << 5),
};

struct ufs_wb_switch_info {
	struct ufs_hba *hba;
	u32  wb_shared_alloc_units;
	bool wb_work_sched;
	bool wb_permanent_disabled;
	bool wb_exception_enabled;
	struct delayed_work wb_toggle_work;
};

bool ufshcd_wb_is_permanent_disabled(struct ufs_hba *hba);
void ufshcd_wb_exception_event_handler(struct ufs_hba *hba);
void ufshcd_wb_toggle_cancel(struct ufs_hba *hba);
int ufs_dynamic_switching_wb_config(struct ufs_hba *hba);
int ufshcd_enable_wb_exception_event(struct ufs_hba *hba);
void ufs_dynamic_switching_wb_remove(struct ufs_hba *hba);
#else /* CONFIG_HUAWEI_UFS_DYNAMIC_SWITCH_WB */
static inline  __attribute__((unused)) bool
ufshcd_wb_is_permanent_disabled(struct ufs_hba *hba
	__attribute__((unused)))
{
	return 0;
}
static inline  __attribute__((unused)) void
ufshcd_wb_exception_event_handler(struct ufs_hba *hba
		__attribute__((unused)))
{
	return;
}
static inline  __attribute__((unused)) void
ufshcd_wb_toggle_cancel(struct ufs_hba *hba
		__attribute__((unused)))
{
	return;
}
static inline  __attribute__((unused)) int
ufs_dynamic_switching_wb_config(struct ufs_hba *hba
		__attribute__((unused)))
{
	return 0;
}
static inline  __attribute__((unused)) int
ufshcd_enable_wb_exception_event(struct ufs_hba *hba
		__attribute__((unused)))
{
	return 0;
}
static inline  __attribute__((unused)) int
ufs_dynamic_switching_wb_remove(struct ufs_hba *hba
		__attribute__((unused)))
{
	return 0;
}
#endif /* CONFIG_HUAWEI_UFS_DYNAMIC_SWITCH_WB */
#endif
