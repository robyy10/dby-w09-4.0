// SPDX-License-Identifier: GPL-2.0
/*
 * huawei_battery_vote.h
 *
 * huawei battery vote interface
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

#ifndef _HUAWEI_BATTERY_VOTE_H_
#define _HUAWEI_BATTERY_VOTE_H_

#include <chipset_common/hwpower/common_module/power_vote.h>

#ifdef CONFIG_HUAWEI_CHARGER
int huawei_battery_vote_for_fcc(struct power_vote_object *obj, void *data, int fcc_ma, const char *client);
int huawei_battery_vote_for_usb_icl(struct power_vote_object *obj, void *data, int icl_ma, const char *client);
int huawei_battery_vote_for_vterm(struct power_vote_object *obj, void *data, int vterm_mv, const char *client);
int huawei_battery_vote_for_iterm(struct power_vote_object *obj, void *data, int iterm_ma, const char *client);
int huawei_battery_vote_for_dis_chg(struct power_vote_object *obj, void *data, int dis_chg, const char *client);
#else
static inline int huawei_battery_vote_for_fcc(struct power_vote_object *obj, void *data, int fcc_ma, const char *client)
{
	return 0;
}

static inline int huawei_battery_vote_for_usb_icl(struct power_vote_object *obj, void *data, int icl_ma, const char *client);
{
	return 0;
}

static inline int huawei_battery_vote_for_vterm(struct power_vote_object *obj, void *data, int vterm_mv, const char *client);
{
	return 0;
}

static inline int huawei_battery_vote_for_iterm(struct power_vote_object *obj, void *data, int iterm_ma, const char *client)
{
	return 0;
}

static inline int huawei_battery_vote_for_dis_chg(struct power_vote_object *obj, void *data, int dis_chg, const char *client);
{
	return 0;
}
#endif /* CONFIG_HUAWEI_CHARGER */

#endif /* _HUAWEI_BATTERY_VOTE_H_ */
