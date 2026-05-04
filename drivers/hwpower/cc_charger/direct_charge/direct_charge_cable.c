// SPDX-License-Identifier: GPL-2.0
/*
 * direct_charge_cable.c
 *
 * cable detect for direct charge module
 *
 * Copyright (c) 2020-2020 Huawei Technologies Co., Ltd.
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

#include <huawei_platform/usb/hw_pd_dev.h>
#include <huawei_platform/power/direct_charger/direct_charger.h>
#include <huawei_platform/power/battery_voltage.h>
#include <chipset_common/hwpower/common_module/power_common_macro.h>
#include <huawei_platform/hwpower/common_module/power_platform.h>
#include <chipset_common/hwpower/common_module/power_printk.h>

#define HWLOG_TAG direct_charge_cable
HWLOG_REGIST();

static struct resist_data g_resist_base_data;
struct dc_cable_ops *g_cable_ops;
static struct dc_cable_type_para g_cable_type_info;

/* for second resistance check */
#define SEC_RESIST_IBUS_TH        4000
#define CTC_EMARK_CURR_5A         5000
#define CTC_EMARK_CURR_6A         6000

int dc_cable_ops_register(struct dc_cable_ops *ops)
{
	int ret = 0;

	if (ops) {
		g_cable_ops = ops;
		hwlog_info("cable ops register ok\n");
	} else {
		hwlog_err("cable ops register fail\n");
		ret = -EINVAL;
	}

	return ret;
}

bool dc_is_support_cable_detect(void)
{
	if (!g_cable_ops || !g_cable_ops->detect)
		return false;
	return true;
}

int dc_cable_detect(void)
{
	if (!g_cable_ops || !g_cable_ops->detect)
		return -EINVAL;

	return g_cable_ops->detect();
}

unsigned int dc_get_cable_type_info(unsigned int type)
{
	switch (type) {
	case DC_CABLE_DETECT_OK:
		return (unsigned int)g_cable_type_info.cable_detect_ok;
	case DC_ORIG_CABLE_TYPE:
		return (unsigned int)g_cable_type_info.orig_cable_type;
	case DC_CABLE_TYPE:
		return (unsigned int)g_cable_type_info.cable_type;
	case DC_IS_CTC_CABLE:
		return (unsigned int)g_cable_type_info.is_ctc_cable;
	case DC_CTC_CABLE_TYPE:
		return (unsigned int)g_cable_type_info.ctc_cable_type;
	default:
		break;
	}

	return 0;
}

void dc_clear_cable_type_info(void)
{
	memset(&g_cable_type_info, 0, sizeof(g_cable_type_info));
}

static bool dc_ignore_cable_detect(int ratio)
{
	int ibus_limit_by_adapter;
	int adapter_type = dc_get_adapter_type();
	int bat_vol = hw_battery_get_series_num() * BAT_RATED_VOLT;

	/* get max current by adapter */
	ibus_limit_by_adapter = dc_get_adapter_max_current(bat_vol * ratio);
	if ((ibus_limit_by_adapter <= CABLE_DETECT_CURRENT_THLD) ||
		(adapter_type == ADAPTER_TYPE_10V2A) ||
		(adapter_type == ADAPTER_TYPE_10V2P25A) ||
		dc_is_undetach_cable())
		return true;

	return false;
}

static int dc_update_cable_type(int ratio)
{
	int ret;
	bool cc_moisture_status = false;
	int curr = 0;

	if (dc_ignore_cable_detect(ratio)) {
		g_cable_type_info.cable_type = DC_STD_CABLE;
		g_cable_type_info.cable_detect_ok = 1; /* detect success flag */
		return 0;
	}

	if (!dc_is_support_cable_detect()) {
		hwlog_err("does not support cable detect return\n");
		return -EPERM;
	}

	if (!dc_get_adapter_cable_info(&curr)) {
		g_cable_type_info.is_dpdm_cable = true;
		g_cable_type_info.cable_max_curr = curr;
		g_cable_type_info.cable_type = DC_STD_CABLE;
		g_cable_type_info.cable_detect_ok = 1; /* detect success flag */
		return 0;
	}

	g_cable_type_info.is_ctc_cable = pd_dpm_get_ctc_cable_flag();
	g_cable_type_info.ctc_cable_type = pd_dpm_get_cvdo_cur_cap();
	ret = dc_cable_detect();
	cc_moisture_status = pd_dpm_get_cc_moisture_status();
	if (!ret || (g_cable_type_info.ctc_cable_type >= PD_DPM_CURR_5A))
		g_cable_type_info.orig_cable_type = DC_STD_CABLE;
	if (ret && !cc_moisture_status && (g_cable_type_info.ctc_cable_type < PD_DPM_CURR_5A)) {
		hwlog_info("stdandard cable detect fail\n");
		g_cable_type_info.cable_detect_ok = 0;
	} else {
		hwlog_info("stdandard cable detect ok\n");
		g_cable_type_info.cable_type = DC_STD_CABLE;
		g_cable_type_info.cable_detect_ok = 1;
	}
	return 0;
}

void dc_update_cable_resistance_thld(struct dc_cable_info *info)
{
	if (g_cable_type_info.cable_detect_ok) {
		info->full_path_res_thld = info->std_cable_full_path_res_max;
		dc_send_icon_uevent();
		return;
	}

	if (g_cable_type_info.is_ctc_cable) {
		info->full_path_res_thld = info->ctc_cable_full_path_res_max;
		g_cable_type_info.cable_type = DC_STD_CABLE;
		dc_send_icon_uevent();
	} else {
		info->full_path_res_thld = info->nonstd_cable_full_path_res_max;
		g_cable_type_info.cable_type = DC_NONSTD_CABLE;
		if (info->is_show_ico_first)
			dc_send_icon_uevent();
	}
}

/* stdandard cable: double 56k cable */
void dc_detect_std_cable(void)
{
	struct direct_charge_device *l_di = direct_charge_get_di();

	if (!l_di || g_cable_type_info.cable_detect_ok)
		return;

	if (dc_update_cable_type(l_di->dc_volt_ratio)) {
		direct_charge_set_stage_status(DC_STAGE_DEFAULT);
		return;
	}
}

static int dc_get_emark_cable_max_current(unsigned int type)
{
	switch (type) {
	case PD_DPM_CURR_5A:
		return CTC_EMARK_CURR_5A;
	case PD_DPM_CURR_6A:
		return CTC_EMARK_CURR_6A;
	default:
		return 0;
	}
}

int dc_get_cable_max_current(int mode)
{
	int cable_limit = 0;
	struct direct_charge_device *l_di = direct_charge_get_di_by_mode(mode);

	if (!l_di)
		return 0;

	if (g_cable_type_info.is_dpdm_cable)
		return l_di->dc_volt_ratio * g_cable_type_info.cable_max_curr;

	if (g_cable_type_info.cable_detect_ok) {
		if (!g_cable_type_info.is_ctc_cable)
			return 0;
		cable_limit = dc_get_emark_cable_max_current(g_cable_type_info.ctc_cable_type) *
			l_di->dc_volt_ratio;
	} else if (g_cable_type_info.is_ctc_cable) {
		cable_limit = l_di->max_current_for_ctc_cable;
	} else {
		cable_limit = l_di->max_current_for_nonstd_cable;
	}

	return cable_limit;
}

static int dc_get_path_resistance_info(struct resist_data *data, int working_mode)
{
	struct adapter_source_info source_info = { 0 };
	unsigned int flag = BIT(ADAPTER_OUTPUT_VOLT) | BIT(ADAPTER_OUTPUT_CURR);

	if (dc_get_adapter_source_info(flag, &source_info))
		return -EPERM;
	data->vadapt = source_info.output_volt;
	data->iadapt = source_info.output_curr;

	if (direct_charge_get_device_vbus(&(data->vbus)))
		return -EPERM;

	if (direct_charge_get_device_ibus(&(data->ibus)))
		return -EPERM;
	data->ibus += dc_get_gain_ibus();
	if (data->ibus == 0) {
		hwlog_err("ibus is zero\n");
		return -EPERM;
	}

	if (dcm_use_two_stage() && (working_mode == SC4_MODE)) {
		data->vbus *= SC_IN_OUT_VOLT_RATE;
		data->ibus /= SC_IN_OUT_VOLT_RATE;
		hwlog_info("fix vbus and ibus\n");
	}

	return 0;
}

int dc_calculate_path_resistance(int *rpath)
{
	struct resist_data tmp;
	int sum = 0;
	int i, ruem;
	int retry = 3; /* 3 : retry times */
	struct direct_charge_device *l_di = direct_charge_get_di();

	if (!l_di)
		return 0;

	g_resist_base_data.iadapt = 0;
	g_resist_base_data.vadapt = 0;
	g_resist_base_data.ibus = 0;
	g_resist_base_data.vbus = 0;
	for (i = 0; i < retry; ++i) {
		if (dc_get_path_resistance_info(&tmp, l_di->working_mode))
			return -EPERM;

		/* r = v/i, unit is mohm */
		*rpath = (tmp.vadapt - tmp.vbus) * POWER_MO_PER_O / tmp.ibus;
		sum += *rpath;

		g_resist_base_data.ibus += tmp.ibus;
		g_resist_base_data.vbus += tmp.vbus;
		g_resist_base_data.vadapt += tmp.vadapt;

		hwlog_info("[%d]: Rpath=%d, Vapt=%d, Vbus=%d, Iapt=%d, Ibus=%d\n",
			i, *rpath, tmp.vadapt, tmp.vbus, tmp.iadapt, tmp.ibus);
	}

	g_resist_base_data.ibus /= retry;
	g_resist_base_data.vadapt /= retry;
	g_resist_base_data.vbus /= retry;

	*rpath = sum / retry;
	*rpath = *rpath > 0 ? *rpath : -*rpath;
	hwlog_info("Rpath=%d, Vapt=%d, Vbus=%d, Ibus=%d\n",
		*rpath, g_resist_base_data.vadapt, g_resist_base_data.vbus, g_resist_base_data.ibus);

	ruem = (int)power_platform_get_uem_resistance();
	*rpath -= ruem;
	if (ruem)
		hwlog_info("Ruem=%d, new Rpath=%d\n", ruem, *rpath);

	return 0;
}

int dc_calculate_second_path_resistance(void)
{
	struct resist_data tmp;
	struct resist_data resist = { 0 };
	int i, rpath, ruem;
	int retry = 5; /* 5 : retry times */
	struct direct_charge_device *l_di = direct_charge_get_di();
	char tmp_buf[ERR_NO_STRING_SIZE] = { 0 };
	int ibus_th = SEC_RESIST_IBUS_TH;

	if (!l_di)
		return -EPERM;

	if (dcm_use_two_stage() && (l_di->working_mode == SC4_MODE))
		ibus_th *= SC_IN_OUT_VOLT_RATE;
	if (l_di->ls_ibus < ibus_th)
		return -EPERM;

	for (i = 0; i < retry; ++i) {
		if (dc_get_path_resistance_info(&tmp, l_di->working_mode))
			return -EPERM;

		resist.ibus += tmp.ibus;
		resist.vbus += tmp.vbus;
		resist.vadapt += tmp.vadapt;

		hwlog_info("[%d]: Vapt=%d, Vbus=%d, Iapt=%d, Ibus=%d\n",
			i, tmp.vadapt, tmp.vbus, tmp.iadapt, tmp.ibus);
	}

	resist.ibus = resist.ibus / retry;
	resist.vadapt = resist.vadapt / retry;
	resist.vbus = resist.vbus / retry;

	if (resist.ibus - g_resist_base_data.ibus == 0)
		return -EPERM;

	/* r = v/i, unit is mohm */
	rpath = ((resist.vadapt - g_resist_base_data.vadapt) -
		(resist.vbus - g_resist_base_data.vbus)) * POWER_MO_PER_O /
		(resist.ibus - g_resist_base_data.ibus);
	rpath = rpath > 0 ? rpath : -rpath;

	snprintf(tmp_buf, sizeof(tmp_buf),
		"f_Rpath=%d,S_Rpath=%d,Vapt=%d,Vbus=%d,Ibus=%d,Vapt0=%d,Vbus0=%d,Ibus0=%d,adp_type=%d,ctc_cable=%d\n",
		l_di->full_path_resistance, rpath, resist.vadapt, resist.vbus,
		resist.ibus, g_resist_base_data.vadapt,
		g_resist_base_data.vbus, g_resist_base_data.ibus,
		dc_get_adapter_type(), pd_dpm_get_ctc_cable_flag());

	hwlog_info("%s\n", tmp_buf);

	ruem = (int)power_platform_get_uem_resistance();
	rpath -= ruem;
	if (ruem)
		hwlog_info("Ruem=%d, new S_Rpath=%d\n", ruem, rpath);

	if (rpath >= l_di->second_path_res_report_th)
		power_dsm_report_dmd(POWER_DSM_BATTERY,
			POWER_DSM_DIRECT_CHARGE_FULL_PATH_RESISTANCE_2ND, tmp_buf);

	l_di->second_path_resistance = rpath;
	l_di->second_resist_check_ok = true;

	return 0;
}

/* get the maximum current allowed by direct charging at specified resist */
int dc_resist_handler(int mode, int value)
{
	int i;
	struct direct_charge_device *l_di = direct_charge_get_di_by_mode(mode);
	struct direct_charge_resist_para *para = NULL;

	if (!l_di)
		return 0;

	if (g_cable_type_info.is_ctc_cable)
		para = l_di->ctc_resist_para;
	else if (g_cable_type_info.cable_detect_ok == CABLE_DETECT_NOK)
		para = l_di->nonstd_resist_para;
	else
		para = l_di->std_resist_para;

	for (i = 0; i < DC_RESIST_LEVEL; ++i) {
		if ((value >= para[i].resist_min) &&
			(value < para[i].resist_max))
			return para[i].resist_cur_max;
	}

	hwlog_err("current resist is illegal, cable_detect_ok=%u, resist=%d\n",
		g_cable_type_info.cable_detect_ok, value);
	return 0;
}

/* get the maximum current allowed by direct charging at specified secondary resist */
int dc_second_resist_handler(void)
{
	int i;
	int path_resist;
	struct direct_charge_device *l_di = direct_charge_get_di();
	struct direct_charge_resist_para *para = NULL;

	if (!l_di || !l_di->second_resist_check_ok)
		return 0;

	if (g_cable_type_info.is_ctc_cable)
		para = l_di->ctc_second_resist_para;
	else
		para = l_di->second_resist_para;

	path_resist = l_di->second_path_resistance;
	for (i = 0; i < DC_RESIST_LEVEL; ++i) {
		if ((path_resist >= para[i].resist_min) &&
			(path_resist < para[i].resist_max))
			return para[i].resist_cur_max;
	}

	hwlog_err("second path resist is illegal, resist=%d\n", path_resist);
	return 0;
}
