// SPDX-License-Identifier: GPL-2.0
/*
 * buck_charge.c
 *
 * buck charge driver
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

#include <chipset_common/hwpower/battery/battery_temp.h>
#include <chipset_common/hwpower/buck_charge/buck_charge.h>
#include <chipset_common/hwpower/buck_charge/buck_charge_vote.h>
#include <chipset_common/hwpower/charger/charger_common_interface.h>
#include <chipset_common/hwpower/common_module/power_dts.h>
#include <chipset_common/hwpower/common_module/power_gpio.h>
#include <chipset_common/hwpower/common_module/power_event_ne.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/hardware_monitor/ffc_control.h>
#include <huawei_platform/power/direct_charger/direct_charger.h>

#define HWLOG_TAG buck_charge
HWLOG_REGIST();

#define BATTERY_DEFAULT_VTERM           4450
#define BATTERY_DEFAULT_ITERM           160
#define BUCK_CHARGE_FULL_CHECK_TIMIES   3
#define BATTERY_FULL_DELTA_VOTAGE       20
#define BATTERY_MAX_ITERM               750
#define CHARGING_CURRENT_OFFSET         (-10)
#define FFC_VTERM_DEFAULT_VALUE         40
#define FFC_VTERM_DELAY_MAX_CNT         3
static struct buck_charge_dev *g_buck_charge_dev;

static void buck_charge_set_ffc_result(struct buck_ffc_charge_info *data,
	bool flag, int iterm)
{
	struct buck_charge_dev *l_dev = g_buck_charge_dev;

	if (!l_dev || !data)
		return;

	data->ffc_charge_flag = flag;
	if (iterm)
		data->iterm = iterm;
	else
		data->iterm = l_dev->iterm;
}

static void buck_charge_update_ffc_info(struct buck_ffc_charge_info *data)
{
	unsigned int vol = 0;
	int iterm = 0;
	struct buck_charge_dev *l_dev = g_buck_charge_dev;

	if (!l_dev || !data)
		return;

	if (direct_charge_in_charging_stage() == DC_NOT_IN_CHARGING_STAGE) {
		if (l_dev->ffc_vterm_flag == FFC_VTERM_START_FLAG)
			buck_charge_set_ffc_result(data, true, ffc_get_buck_ichg_th());
		else if (l_dev->ffc_vterm_flag & FFC_VETRM_END_FLAG)
			buck_charge_set_ffc_result(data, false, ffc_get_buck_iterm());
		else
			buck_charge_set_ffc_result(data, false, l_dev->iterm);
		data->dc_mode = false;
		return;
	}

	data->dc_mode = true;
	direct_charge_get_vmax(&vol);
	hwlog_info("vol_max=%u, vterm=%u\n", vol, l_dev->vterm);
	if (vol > l_dev->vterm) {
		direct_charge_get_iterm(&iterm);
		buck_charge_set_ffc_result(data, true, iterm);
		return;
	}

	buck_charge_set_ffc_result(data, false, l_dev->iterm);
}

static void buck_charge_ffc_update_iterm(int iterm)
{
	power_vote_set(ITERM_VOTE_OBJECT, CHARGE_FFC_VOTER, true, iterm);
	hwlog_info("buck charge set iterm=%d\n", iterm);
}

static int buck_charge_ffc_get_incr_vterm(struct buck_charge_dev *di)
{
	static int cnt = 0;
	static int last_vterm;
	int ffc_vterm = ffc_get_buck_vterm();

	if (!di->dc_adp)
		return 0;

	if (di->ffc_only_chr_done) {
		if (!direct_charge_check_charge_done()) {
			hwlog_info("not sc siwtch to buck, no need ffc\n");
			return 0;
		}

		if (di->ffc_delay_cnt < FFC_VTERM_DELAY_MAX_CNT) {
			ffc_vterm = FFC_VTERM_DEFAULT_VALUE;
			di->ffc_delay_cnt++;
		}
	}

	if (di->ffc_vterm_flag & FFC_VETRM_END_FLAG) {
		last_vterm = 0;
		buck_charge_ffc_update_iterm(ffc_get_buck_iterm());
		return 0;
	}

	if (ffc_vterm) {
		cnt = 0;
		last_vterm = ffc_vterm;
		di->ffc_vterm_flag |= FFC_VTERM_START_FLAG;
		return ffc_vterm;
	}

	if (di->ffc_vterm_flag & FFC_VTERM_START_FLAG) {
		cnt++;
		if (cnt < FFC_CHARGE_EXIT_TIMES) {
			return last_vterm;
		} else {
			di->ffc_vterm_flag |= FFC_VETRM_END_FLAG;
			cnt = 0;
		}
	}

	return 0;
}

static int buck_charge_get_iterm_th(struct buck_charge_dev *di)
{
	if (di->ffc_vterm_flag & FFC_VETRM_END_FLAG)
		return ffc_get_buck_iterm();
	else
		return di->iterm;
}

static bool buck_charge_need_check_charging_full(struct buck_charge_dev *di)
{
	int vbat;
	int charge_enable = 0;
	unsigned int vterm_dec = 0;

	charge_get_vterm_dec(&vterm_dec);
	vbat = power_supply_app_get_bat_voltage_now();
	hwlog_info("%s vterm_dec=%u, vbat=%d\n", __func__, vterm_dec, vbat);
	if (vbat < (di->vterm - vterm_dec - BATTERY_FULL_DELTA_VOTAGE))
		return false;

	charge_get_charge_enable_status(&charge_enable);
	hwlog_info("%s charge_enable=%d\n", __func__, charge_enable);
	if (!charge_enable)
		return false;

	return true;
}

static bool buck_charge_is_multi_batt_charging_full(int ichg)
{
	int battery_gauge_cur = 0;
	int battery_gauge_aux_cur = 0;
	struct power_supply *psy = NULL;

	/* 300: total current full threshold */
	if (!power_supply_check_psy_available("battery_gauge_aux", &psy) || (ichg > 300))
		return false;

	power_supply_get_int_property_value("battery_gauge",
		POWER_SUPPLY_PROP_CURRENT_NOW, &battery_gauge_cur);

	power_supply_get_int_property_value("battery_gauge_aux",
		POWER_SUPPLY_PROP_CURRENT_NOW, &battery_gauge_aux_cur);

	/* 60: single current full threshold */
	if ((battery_gauge_cur < 60) || (battery_gauge_aux_cur < 60))
		return true;

	return false;
}

static bool buck_charge_is_charging_full(struct buck_charge_dev *di)
{
	int ichg, ichg_avg, iterm_th;
	bool term_allow = false;
	bool charge_full = false;

	if (!power_platform_is_battery_exit())
		return false;

	if (!buck_charge_need_check_charging_full(di))
		return false;

	ichg = -power_platform_get_battery_current();
	ichg_avg = charge_get_battery_current_avg();
	if ((ichg > CHARGING_CURRENT_OFFSET) && (ichg_avg > CHARGING_CURRENT_OFFSET))
		term_allow = true;

	iterm_th = buck_charge_get_iterm_th(di);
	hwlog_info("%s ichg=%d, ichg_avg=%d, iterm_th=%d, capacity=%d\n", __func__, ichg, ichg_avg, iterm_th,
		power_supply_app_get_bat_capacity());
	if (term_allow && (((ichg < iterm_th) && (ichg_avg < iterm_th)) ||
		buck_charge_is_multi_batt_charging_full(ichg))) {
		di->check_full_count++;
		if (di->check_full_count >= BUCK_CHARGE_FULL_CHECK_TIMIES) {
			di->check_full_count = BUCK_CHARGE_FULL_CHECK_TIMIES;
			charge_full = true;
		}
	} else {
		di->check_full_count = 0;
	}

	return charge_full;
}

static void buck_charge_force_termination(struct buck_charge_dev *di)
{
	bool flag;

	if (!di || !di->force_term_support)
		return;

	flag = buck_charge_is_charging_full(di);
	if (flag)
		power_vote_set(ITERM_VOTE_OBJECT, CHARGE_USER_VOTER, true, BATTERY_MAX_ITERM);
	else
		power_vote_set(ITERM_VOTE_OBJECT, CHARGE_USER_VOTER, true, 0);
}

static void buck_charge_monitor_work(struct work_struct *work)
{
	int increase_volt;
	int tbat = 0;
	struct buck_ffc_charge_info data = { 0 };
	struct buck_charge_dev *l_dev = container_of(work, struct buck_charge_dev, buck_charge_work.work);

	if (!l_dev)
		return;

	hwlog_info("%s enter\n", __func__);

	if (!l_dev->charging_on)
		return;

	bat_temp_get_temperature(BAT_TEMP_MIXED, &tbat);

	increase_volt = buck_charge_ffc_get_incr_vterm(l_dev);
	charge_set_buck_fv_delta(increase_volt);
	hwlog_info("%s increase_volt=%d\n", __func__, increase_volt);

	if (l_dev->jeita_support) {
		buck_charge_jeita_tbatt_handler(tbat, l_dev->jeita_table, &l_dev->jeita_result);
		power_vote_set(FCC_VOTE_OBJECT, CHARGE_JEITA_VOTER, true, l_dev->jeita_result.ichg);
		power_vote_set(VTERM_VOTE_OBJECT, CHARGE_JEITA_VOTER, true, l_dev->jeita_result.vterm);
	}
	charge_update_buck_iin_thermal();

	buck_charge_update_ffc_info(&data);
	power_event_bnc_notify(POWER_BNT_BUCK_CHARGE, POWER_NE_BUCK_FFC_CHARGE, &data);
	buck_charge_force_termination(l_dev);

	schedule_delayed_work(&l_dev->buck_charge_work, msecs_to_jiffies(BUCK_CHARGE_WORK_TIMEOUT));
	hwlog_info("%s end\n", __func__);
}

static void buck_charge_stop_monitor_work(struct work_struct *work)
{
	struct buck_charge_dev *l_dev = container_of(work, struct buck_charge_dev, stop_charge_work);

	if (!l_dev)
		return;

	charge_set_buck_fv_delta(0);
	power_vote_set(ITERM_VOTE_OBJECT, CHARGE_FFC_VOTER, true, l_dev->iterm);
	power_vote_set(USB_ICL_VOTE_OBJECT, CHARGE_FCP_VOTER, false, 0);
	power_vote_set(USB_ICL_VOTE_OBJECT, CHARGE_RT_VOTER, false, 0);
	power_vote_set(USB_ICL_VOTE_OBJECT, CHARGE_USER_VOTER, false, 0);
}

static int buck_charge_event_notifier_call(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct buck_charge_dev *l_dev = g_buck_charge_dev;

	if (!l_dev)
		return NOTIFY_OK;

	switch (event) {
	case POWER_NE_CHARGING_START:
		l_dev->charging_on = true;
		buck_charge_enable_equal_curr_load(true);
		schedule_delayed_work(&l_dev->buck_charge_work, msecs_to_jiffies(0));
		break;
	case POWER_NE_CHARGING_STOP:
		l_dev->charging_on = false;
		l_dev->ffc_vterm_flag = 0;
		l_dev->dc_adp = false;
		l_dev->ffc_delay_cnt = 0;
		buck_charge_enable_equal_curr_load(false);
		schedule_work(&l_dev->stop_charge_work);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int buck_charge_dc_event_notifier_call(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct buck_charge_dev *l_dev = g_buck_charge_dev;

	if (!l_dev)
		return NOTIFY_OK;

	switch (event) {
	case POWER_NE_DC_CHECK_START:
		l_dev->dc_adp = true;
		hwlog_info("dc check start\n");
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int buck_charge_chg_event_notifier_call(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct buck_charge_dev *l_dev = g_buck_charge_dev;

	if (!l_dev)
		return NOTIFY_OK;

	switch (event) {
	case POWER_NE_CHG_CHARGING_DONE:
		if (l_dev->ibus_limit_after_chg_done)
			power_vote_set(USB_ICL_VOTE_OBJECT, CHARGE_USER_VOTER, true, l_dev->ibus_limit_after_chg_done);
		break;
	case POWER_NE_CHG_CHARGING_RECHARGE:
		if (l_dev->ibus_limit_after_chg_done)
			power_vote_set(USB_ICL_VOTE_OBJECT, CHARGE_USER_VOTER, false, 0);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

void buck_charge_enable_equal_curr_load(bool enable)
{
	int i;
	struct buck_charge_dev *l_dev = g_buck_charge_dev;

	if (!l_dev || (l_dev->equal_cur_gpio.count <= 0))
		return;

	for (i = 0; i < l_dev->equal_cur_gpio.count; i++) {
		if (enable && l_dev->charging_on)
			gpio_set_value(l_dev->equal_cur_gpio.no[i], l_dev->equal_cur_gpio.en_status[i]);
		else
			gpio_set_value(l_dev->equal_cur_gpio.no[i], !l_dev->equal_cur_gpio.en_status[i]);

		hwlog_info("[enable_equal_curr_load] enable:%d, charging_on:%d, index:%d, value:%d\n",
			enable, l_dev->charging_on, i, gpio_get_value(l_dev->equal_cur_gpio.no[i]));
	}
}

static void buck_charge_parse_equal_curr_load_para(struct device_node *np, struct buck_charge_dev *di)
{
	int i;
	const char *gpio_tag = NULL;

	di->equal_cur_gpio.count = of_gpio_count(np);
	if (di->equal_cur_gpio.count <= 0)
		return;

	if (di->equal_cur_gpio.count > BUCK_EQUAL_CUR_GPIO_MAX) {
		hwlog_err("gpio number is invalid\n");
		goto err_out;
	}

	for (i = 0; i < di->equal_cur_gpio.count; i++) {
		di->equal_cur_gpio.no[i] = of_get_gpio(np, i);
		if (!gpio_is_valid(di->equal_cur_gpio.no[i])) {
			hwlog_err("gpio %d is not valid\n", di->equal_cur_gpio.no[i]);
			goto err_out;
		}
		if (power_dts_read_string_index(power_dts_tag(HWLOG_TAG),
			np, "gpio_types", i, &gpio_tag))
			goto err_out;

		power_dts_read_u32_index(power_dts_tag(HWLOG_TAG),
			np, "gpio_en_status", i, &di->equal_cur_gpio.en_status[i]);

		if (gpio_request(di->equal_cur_gpio.no[i], gpio_tag))
			hwlog_err("gpio %d request failed\n", di->equal_cur_gpio.no[i]);

		if (gpio_direction_output(di->equal_cur_gpio.no[i], !di->equal_cur_gpio.en_status[i]))
			hwlog_err("gpio %d direction_output failed\n", di->equal_cur_gpio.no[i]);
	}

	return;

err_out:
	di->equal_cur_gpio.count = 0;
}

static int buck_charge_parse_dts(struct device_node *np, struct buck_charge_dev *di)
{
	if (!np || !di)
		return -EINVAL;

	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "vterm", &di->vterm, BATTERY_DEFAULT_VTERM);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "iterm", &di->iterm, BATTERY_DEFAULT_ITERM);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "jeita_support", &di->jeita_support, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "force_term_support", &di->force_term_support, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "ffc_only_chr_done", &di->ffc_only_chr_done, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "ibus_limit_after_chg_done",
		&di->ibus_limit_after_chg_done, 0);
	if (di->jeita_support)
		buck_charge_jeita_parse_jeita_table(np, (void *)di);

	buck_charge_parse_equal_curr_load_para(np, di);

	return 0;
}

static int buck_charge_probe(struct platform_device *pdev)
{
	int ret;
	struct buck_charge_dev *l_dev = NULL;
	struct device_node *np = NULL;

	if (!pdev || !pdev->dev.of_node)
		return -ENODEV;

	l_dev = kzalloc(sizeof(*l_dev), GFP_KERNEL);
	if (!l_dev)
		return -ENOMEM;

	l_dev->dev = &pdev->dev;
	np = l_dev->dev->of_node;

	ret = buck_charge_parse_dts(np, l_dev);
	if (ret)
		goto fail_free_mem;

	INIT_DELAYED_WORK(&l_dev->buck_charge_work, buck_charge_monitor_work);
	INIT_WORK(&l_dev->stop_charge_work, buck_charge_stop_monitor_work);

	l_dev->event_nb.notifier_call = buck_charge_event_notifier_call;
	ret = power_event_bnc_register(POWER_BNT_CHARGING, &l_dev->event_nb);
	if (ret)
		goto fail_free_mem;

	l_dev->dc_event_nb.notifier_call = buck_charge_dc_event_notifier_call;
	ret = power_event_bnc_register(POWER_BNT_DC, &l_dev->dc_event_nb);
	if (ret)
		goto fail_bnc_register;

	l_dev->chg_event_nb.notifier_call = buck_charge_chg_event_notifier_call;
	ret = power_event_bnc_register(POWER_BNT_CHG, &l_dev->chg_event_nb);
	if (ret)
		goto fail_bnc_register1;

	g_buck_charge_dev = l_dev;
	platform_set_drvdata(pdev, l_dev);
	return 0;

fail_bnc_register1:
	power_event_bnc_unregister(POWER_BNT_DC, &l_dev->dc_event_nb);
fail_bnc_register:
	power_event_bnc_unregister(POWER_BNT_CHARGING, &l_dev->event_nb);
fail_free_mem:
	kfree(l_dev);
	g_buck_charge_dev = NULL;
	return ret;
}

static int buck_charge_remove(struct platform_device *pdev)
{
	struct buck_charge_dev *l_dev = platform_get_drvdata(pdev);

	if (!l_dev)
		return -ENODEV;

	cancel_delayed_work(&l_dev->buck_charge_work);
	power_event_bnc_unregister(POWER_BNT_CHARGING, &l_dev->event_nb);
	power_event_bnc_unregister(POWER_BNT_DC, &l_dev->dc_event_nb);
	power_event_bnc_unregister(POWER_BNT_CHG, &l_dev->chg_event_nb);
	platform_set_drvdata(pdev, NULL);
	kfree(l_dev);
	g_buck_charge_dev = NULL;
	return 0;
}

static const struct of_device_id buck_charge_match_table[] = {
	{
		.compatible = "huawei,buck_charge",
		.data = NULL,
	},
	{},
};

static struct platform_driver buck_charge_driver = {
	.probe = buck_charge_probe,
	.remove = buck_charge_remove,
	.driver = {
		.name = "huawei,buck_charge",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(buck_charge_match_table),
	},
};

static int __init buck_charge_init(void)
{
	return platform_driver_register(&buck_charge_driver);
}

static void __exit buck_charge_exit(void)
{
	platform_driver_unregister(&buck_charge_driver);
}

device_initcall_sync(buck_charge_init);
module_exit(buck_charge_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("buck charge driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
