// SPDX-License-Identifier: GPL-2.0
/*
 * buck_charge_vote.c
 *
 * buck charge vote driver
 *
 * Copyright (c) 2021-2021 Huawei Technologies Co., Ltd.
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

#include <linux/init.h>
#include <chipset_common/hwpower/buck_charge/buck_charge_vote.h>
#include <chipset_common/hwpower/charger/charger_common_interface.h>
#include <chipset_common/hwpower/common_module/power_printk.h>

#define HWLOG_TAG buck_charge_vote
HWLOG_REGIST();

static struct buck_charge_vote_dev *g_bc_vote_dev;

static int buck_charge_vote_create_object(struct buck_charge_vote_dev *di)
{
	int ret;

	if (!di)
		return -EINVAL;

	ret = power_vote_create_object(FCC_VOTE_OBJECT, POWER_VOTE_SET_MIN, charge_vote_for_fcc, di);
	ret += power_vote_create_object(USB_ICL_VOTE_OBJECT, POWER_VOTE_SET_MIN, charge_vote_for_usb_icl, di);
	ret += power_vote_create_object(VTERM_VOTE_OBJECT, POWER_VOTE_SET_MIN, charge_vote_for_vterm, di);
	ret += power_vote_create_object(ITERM_VOTE_OBJECT, POWER_VOTE_SET_MAX, charge_vote_for_iterm, di);
	ret += power_vote_create_object(DIS_CHG_VOTE_OBJECT, POWER_VOTE_SET_ANY, charge_vote_for_dis_chg, di);

	return ret;
}

static void buck_charge_vote_destroy_obejct(void)
{
	power_vote_destroy_object(FCC_VOTE_OBJECT);
	power_vote_destroy_object(USB_ICL_VOTE_OBJECT);
	power_vote_destroy_object(VTERM_VOTE_OBJECT);
	power_vote_destroy_object(ITERM_VOTE_OBJECT);
	power_vote_destroy_object(DIS_CHG_VOTE_OBJECT);
}

static int __init buck_charge_vote_init(void)
{
	struct buck_charge_vote_dev *l_dev = NULL;

	l_dev = kzalloc(sizeof(*l_dev), GFP_KERNEL);
	if (!l_dev)
		return -ENOMEM;

	if (buck_charge_vote_create_object(l_dev))
		hwlog_err("buck charge vote create object fail\n");

	g_bc_vote_dev = l_dev;
	hwlog_info("%s ok\n", __func__);
	return 0;
}

static void __exit buck_charge_vote_exit(void)
{
	if (!g_bc_vote_dev)
		return;

	buck_charge_vote_destroy_obejct();
	kfree(g_bc_vote_dev);
	g_bc_vote_dev = NULL;
}

device_initcall(buck_charge_vote_init);
module_exit(buck_charge_vote_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("buck charge vote driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
