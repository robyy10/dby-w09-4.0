/* SPDX-License-Identifier: GPL-2.0 */
/*
 * buck_charge.h
 *
 * buck charge module
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

#ifndef _BUCK_CHARGE_H_
#define _BUCK_CHARGE_H_

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <chipset_common/hwpower/buck_charge/buck_charge_jeita.h>

#define FFC_VTERM_START_FLAG                BIT(0)
#define FFC_VETRM_END_FLAG                  BIT(1)
#define BUCK_CHARGE_WORK_TIMEOUT            10000
#define FFC_CHARGE_EXIT_TIMES               2
#define BUCK_EQUAL_CUR_GPIO_MAX             2

struct buck_ffc_charge_info {
	bool ffc_charge_flag;
	bool dc_mode;
	int iterm;
};

struct buck_equal_cur_gpio_info {
	int count;
	int no[BUCK_EQUAL_CUR_GPIO_MAX];
	int en_status[BUCK_EQUAL_CUR_GPIO_MAX];
};

struct buck_charge_dev {
	struct device *dev;
	u32 ffc_vterm_flag;
	u32 jeita_support;
	u32 force_term_support;
	u32 check_full_count;
	u32 vterm;
	u32 iterm;
	bool charging_on;
	bool dc_adp;
	u32 ffc_only_chr_done;
	int ffc_delay_cnt;
	u32 ibus_limit_after_chg_done;
	struct delayed_work buck_charge_work;
	struct work_struct stop_charge_work;
	struct notifier_block event_nb;
	struct notifier_block chg_event_nb;
	struct notifier_block dc_event_nb;
	struct bc_jeita_para jeita_table[BC_JEITA_PARA_LEVEL];
	struct bc_jeita_result jeita_result;
	struct buck_equal_cur_gpio_info equal_cur_gpio;
};

void buck_charge_enable_equal_curr_load(bool enable); /* to do wls if support */

#endif /* _BUCK_CHARGE_H_ */
