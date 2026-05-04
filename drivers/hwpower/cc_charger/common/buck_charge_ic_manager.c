// SPDX-License-Identifier: GPL-2.0
/*
 * buck_charge_ic_manager.c
 *
 * buck charge ic management interface
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

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/math64.h>
#include <chipset_common/hwpower/common_module/power_common_macro.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/buck_charge/buck_charge_ic_manager.h>
#include <huawei_platform/hwpower/common_module/power_platform.h>

#define HWLOG_TAG buck_charge_ic_manager
HWLOG_REGIST();

int charge_init_chip(struct charge_init_data *data)
{
	int ret;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->chip_init)
		return -EINVAL;

	ret = temp_ops->chip_init(data);
	hwlog_info("init_chip: charger_type=%u vbus=%d ret=%d\n",
		data->charger_type, data->vbus, ret);
	return ret;
}

int charger_set_hiz(int enable)
{
	int ret;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->set_charger_hiz)
		return -EINVAL;

	ret = temp_ops->set_charger_hiz(enable);
	hwlog_info("set_hiz: enable=%d ret=%d\n", enable, ret);
	return ret;
}

int charge_set_vbus_vset(u32 volt)
{
	int ret;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!charge_support_thirdparty_buck())
		return -EINVAL;

	if (!temp_ops || !temp_ops->set_vbus_vset)
		return -EINVAL;

	ret = temp_ops->set_vbus_vset(volt);
	hwlog_info("set_vbus_vset: volt=%u ret=%d\n", volt, ret);
	return ret;
}

int charge_set_mivr(u32 volt)
{
	int ret;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!charge_support_thirdparty_buck())
		return -EINVAL;

	if (!temp_ops || !temp_ops->set_mivr)
		return -EINVAL;

	ret = temp_ops->set_mivr(volt);
	hwlog_info("set_mivr: volt=%u ret=%d\n", volt, ret);
	return ret;
}

#if IS_ENABLED(CONFIG_QTI_PMIC_GLINK)
static int charge_set_batfet_disable_1(int val)
{
	u32 id = POWER_GLINK_PROP_ID_SET_SHIP_MODE;
	u32 value = 0;

	if (val == 0)
		return -EINVAL;

	(void)power_glink_set_property_value(id, &value, GLINK_DATA_ONE);
	hwlog_info("set_batfet_disable: val=%d\n", val);
	return 0;
}
#else
static int charge_set_batfet_disable_1(int val)
{
	return -EINVAL;
}
#endif /* IS_ENABLED(CONFIG_QTI_PMIC_GLINK) */

int charge_set_batfet_disable(int val)
{
	int ret;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!charge_support_thirdparty_buck())
		return charge_set_batfet_disable_1(val);

	if (!temp_ops || !temp_ops->set_batfet_disable)
		return -EINVAL;

	ret = temp_ops->set_batfet_disable(val);
	hwlog_info("set_batfet_disable: val=%d ret=%d\n", val, ret);
	return ret;
}

int charge_set_watchdog(int time)
{
	int ret;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->set_watchdog_timer)
		return -EINVAL;

	ret = temp_ops->set_watchdog_timer(time);
	hwlog_info("set_watchdog: time=%d ret=%d\n", time, ret);
	return ret;
}

int charge_reset_watchdog(void)
{
	int ret;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->reset_watchdog_timer)
		return -EINVAL;

	ret = temp_ops->reset_watchdog_timer();
	hwlog_info("reset_watchdog: ret=%d\n", ret);
	return ret;
}

void charge_kick_watchdog(void)
{
	if (charge_reset_watchdog())
		hwlog_err("kick watchdog timer fail\n");
	else
		hwlog_info("kick watchdog timer ok\n");
	power_platform_charge_feed_sys_wdt(CHARGE_SYS_WDT_TIMEOUT);
}

void charge_disable_watchdog(void)
{
	if (charge_set_watchdog(CHAGRE_WDT_DISABLE))
		hwlog_err("disable watchdog timer fail\n");
	else
		hwlog_info("disable watchdog timer ok\n");
	power_platform_charge_stop_sys_wdt();
}

int charge_get_vusb(void)
{
	int vusb_vol = 0;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!charge_support_thirdparty_buck())
		return -EINVAL;

	if (!temp_ops || !temp_ops->get_vusb) {
		hwlog_err("g_charge_ops or get_vusb is null\n");
		return -EINVAL;
	}

	if (temp_ops->get_vusb(&vusb_vol))
		return -EINVAL;

	return vusb_vol;
}

static int charge_get_vwls(void)
{
	u32 vwls = 0;

#if IS_ENABLED(CONFIG_HUAWEI_POWER_GLINK)
	(void)power_glink_get_property_value(POWER_GLINK_PROP_ID_SET_WLSBST, &vwls, GLINK_DATA_ONE);
#endif
	return vwls;
}

static int charge_get_vbus_by_usb(void)
{
	int vbus = power_supply_app_get_usb_voltage_now();
	int vwls = charge_get_vwls();

	return vbus >= vwls ? vbus : vwls;
}

int charge_get_vbus(void)
{
	int ret;
	unsigned int vbus = 0;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!charge_support_thirdparty_buck())
		return charge_get_vbus_by_usb();

	if (!temp_ops || !temp_ops->get_vbus) {
		hwlog_err("g_charge_ops or get_vbus is null\n");
		return 0;
	}

	ret = temp_ops->get_vbus(&vbus);
	hwlog_info("get_vbus: vbus=%u ret=%d\n", vbus, ret);
	return vbus;
}

static int charge_get_ibus_by_usb(void)
{
	u32 ibus_curr = 0;

	(void)charger_dev_get_ibus(&ibus_curr);
	return ibus_curr / POWER_UA_PER_MA;
}

int charge_get_ibus(void)
{
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!charge_support_thirdparty_buck())
		return charge_get_ibus_by_usb();

	if (!temp_ops || !temp_ops->get_ibus) {
		hwlog_err("g_charge_ops or get_ibus is null\n");
		return -1;
	}

	return temp_ops->get_ibus();
}

int charge_get_iin_set(void)
{
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->get_iin_set) {
		hwlog_err("g_charge_ops or get_iin_set is null\n");
		return CHARGE_CURRENT_0500_MA;
	}
	return temp_ops->get_iin_set();
}

unsigned int charge_get_charging_state(void)
{
	unsigned int state = CHAGRE_STATE_NORMAL;
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->get_charge_state) {
		hwlog_err("g_charge_ops or get_charge_state is null\n");
		return CHAGRE_STATE_NORMAL;
	}

	temp_ops->get_charge_state(&state);
	return state;
}

int charge_set_dev_iin(int iin)
{
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->set_input_current) {
		hwlog_err("g_charge_ops or set_dev_iin is null\n");
		return -1;
	}

	return temp_ops->set_input_current(iin);
}

int charge_check_input_dpm_state(void)
{
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->check_input_dpm_state) {
		hwlog_err("g_charge_ops or check_input_dpm_state is null\n");
		return -1;
	}

	return temp_ops->check_input_dpm_state();
}

int charge_check_charger_plugged(void)
{
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!temp_ops || !temp_ops->check_charger_plugged)
		return -1;

	return temp_ops->check_charger_plugged();
}
int charge_get_vsys(int *vsys_vol)
{
	struct charge_device_ops *temp_ops = bc_ic_get_ic_ops();

	if (!vsys_vol || !temp_ops || !temp_ops->get_vsys)
		return -EINVAL;

	return temp_ops->get_vsys(vsys_vol);
}
