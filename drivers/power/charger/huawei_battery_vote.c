// SPDX-License-Identifier: GPL-2.0
/*
 * huawei_battery_vote.c
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

#include <linux/power_supply.h>
#include <linux/power/huawei_battery.h>
#include <chipset_common/hwpower/common_module/power_common_macro.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/common_module/power_supply_interface.h>
#include <huawei_platform/hwpower/common_module/power_glink.h>
#include <huawei_platform/power/huawei_battery_vote.h>

#define HWLOG_TAG huawei_battery_vote
HWLOG_REGIST();

#if IS_ENABLED(CONFIG_QTI_PMIC_GLINK)
int huawei_battery_vote_for_fcc(struct power_vote_object *obj, void *data, int fcc_ma, const char *client)
{
	u32 id = POWER_GLINK_PROP_ID_SET_IBAT;
	u32 glink_data[GLINK_DATA_TWO];

	if (fcc_ma < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}

	glink_data[GLINK_DATA_ZERO] = AGGREGATOR_VOTE_ENABLE;
	glink_data[GLINK_DATA_ONE] = fcc_ma;

	return power_glink_set_property_value(id, glink_data, GLINK_DATA_TWO);
}

int huawei_battery_vote_for_usb_icl(struct power_vote_object *obj, void *data, int icl_ma, const char *client)
{
	u32 id = POWER_GLINK_PROP_ID_SET_USB_ICL;
	u32 glink_data[GLINK_DATA_TWO];

	if (icl_ma < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}

	glink_data[GLINK_DATA_ZERO] = AGGREGATOR_VOTE_ENABLE;
	glink_data[GLINK_DATA_ONE] = icl_ma;

	return power_glink_set_property_value(id, glink_data, GLINK_DATA_TWO);
}

int huawei_battery_vote_for_vterm(struct power_vote_object *obj, void *data, int vterm_mv, const char *client)
{
	u32 id = POWER_GLINK_PROP_ID_SET_VTERM;
	u32 glink_data[GLINK_DATA_TWO];

	if (vterm_mv < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}

	glink_data[GLINK_DATA_ZERO] = AGGREGATOR_VOTE_ENABLE;
	glink_data[GLINK_DATA_ONE] = vterm_mv;

	return power_glink_set_property_value(id, glink_data, GLINK_DATA_TWO);
}

int huawei_battery_vote_for_iterm(struct power_vote_object *obj, void *data, int iterm_ma, const char *client)
{
	if (iterm_ma < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}

	return power_glink_set_property_value(POWER_GLINK_PROP_ID_SET_FFC_ITERM, &iterm_ma, GLINK_DATA_ONE);
}

int huawei_battery_vote_for_dis_chg(struct power_vote_object *obj, void *data, int dis_chg, const char *client)
{
	return 0;
}
#else
int huawei_battery_vote_for_fcc(struct power_vote_object *obj, void *data, int fcc_ma, const char *client)
{
	if (fcc_ma < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}

	return power_supply_set_int_property_value("bk_battery",
		POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, fcc_ma * POWER_UV_PER_MV);
}

int huawei_battery_vote_for_usb_icl(struct power_vote_object *obj, void *data, int icl_ma, const char *client)
{
	if (icl_ma < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}

	if (huawei_battery_get_chg_usb_current() > icl_ma)
		icl_ma = huawei_battery_get_chg_usb_current();

	return power_supply_set_int_property_value("bk_battery", POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, icl_ma);
}

int huawei_battery_vote_for_vterm(struct power_vote_object *obj, void *data, int vterm_mv, const char *client)
{
	if (vterm_mv < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}

	return power_supply_set_int_property_value("bk_battery",
		POWER_SUPPLY_PROP_VOLTAGE_MAX, vterm_mv * POWER_UV_PER_MV);
}

int huawei_battery_vote_for_iterm(struct power_vote_object *obj, void *data, int iterm_ma, const char *client)
{
	if (iterm_ma < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		return 0;
	}
	return power_supply_set_int_property_value("bk_battery",
		POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, iterm_ma);
}

int huawei_battery_vote_for_dis_chg(struct power_vote_object *obj, void *data, int dis_chg, const char *client)
{
	if (dis_chg < 0) {
		hwlog_err("%s invalid vote\n", __func__);
		dis_chg = false;
	}

	return power_supply_set_int_property_value("bk_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, !dis_chg);
}
#endif /* IS_ENABLED(CONFIG_QTI_PMIC_GLINK) */
