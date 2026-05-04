// SPDX-License-Identifier: GPL-2.0
/*
 * buck_charge_vote.h
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

#ifndef _BUCK_CHARGE_VOTE_H_
#define _BUCK_CHARGE_VOTE_H_

#include <linux/module.h>
#include <chipset_common/hwpower/common_module/power_vote.h>

#define CHARGE_THERMAL_VOTER  "CHARGE_THERMAL_VOTER"
#define CHARGE_USER_VOTER     "CHARGE_USER_VOTER"
#define CHARGE_JEITA_VOTER    "CHARGE_JEITA_VOTER"
#define CHARGE_BASP_VOTER     "CHARGE_BASP_VOTER"
#define CHARGE_WARM_WR_VOTER  "CHARGE_WARM_WR_VOTER"
#define CHARGE_USER_VOTER     "CHARGE_USER_VOTER"
#define CHARGE_RT_VOTER       "CHARGE_RT_VOTER"
#define CHARGE_FCP_VOTER      "CHARGE_FCP_VOTER"
#define CHARGE_WLS_VOTER      "CHARGE_WLS_VOTER"
#define CHARGE_FFC_VOTER      "CHARGE_FFC_VOTER"
#define IBUS_DETECT_VOTER     "IBUS_DETECT_VOTER"
#define CHARGE_POWER_IF_VOTER "CHARGE_POWER_IF_VOTER"
#define DIRECT_CHARGER_VOTER  "DIRECT_CHARGER_VOTER"
/* rsmc:halifa */
#define CHARGE_RSMC_VOTER     "CHARGE_RSMC_VOTER"

#define USB_ICL_VOTE_OBJECT  "BATT: usb_icl"
#define FCC_VOTE_OBJECT      "BATT: fcc"
#define VTERM_VOTE_OBJECT    "BATT: vterm"
#define ITERM_VOTE_OBJECT    "BATT: iterm"
#define DIS_CHG_VOTE_OBJECT  "BATT: dis_chg"

struct buck_charge_vote_dev {
	struct device *dev;
};

#endif /* _BUCK_CHARGE_VOTE_H_ */
