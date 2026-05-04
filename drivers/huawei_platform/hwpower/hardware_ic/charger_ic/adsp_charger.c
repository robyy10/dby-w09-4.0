// SPDX-License-Identifier: GPL-2.0
/*
 * adsp_charger.c
 *
 * virtual buck charger ic driver for qcom platforms which have power glink
 *
 * Copyright (c) 2022-2022 Huawei Technologies Co., Ltd.
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

#include <chipset_common/hwpower/buck_charge/buck_charge_ic.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <huawei_platform/hwpower/common_module/power_glink.h>
#include <huawei_platform/hwpower/common_module/power_platform.h>

#define HWLOG_TAG adsp_charger
HWLOG_REGIST();

static int adsp_charger_set_hiz(int enable)
{
	int ret;
	u32 value;
	u32 id = POWER_GLINK_PROP_ID_SET_INPUT_SUSPEND;

	if (enable)
		value = 1; /* Hiz enable */
	else
		value = 0;

	hwlog_info("set_hiz: val=%d\n", value);
	ret = power_glink_set_property_value(id, &value, GLINK_DATA_ONE);
	if (ret)
		hwlog_err("set_hiz failed\n");

	power_msleep(DT_MSLEEP_200MS, 0, NULL);
	return ret;
}

static int adsp_charger_set_batfet_disable(int enable)
{
	u32 id = POWER_GLINK_PROP_ID_SET_SHIP_MODE;
	u32 value = 0;

	if (enable == 0)
		return -EINVAL;

	(void)power_glink_set_property_value(id, &value, GLINK_DATA_ONE);
	hwlog_info("set_batfet_disable: val=%d\n", enable);
	return 0;
}

static int adsp_charger_get_vbus(u32 *vbus)
{
	u32 vwls = 0;
	int ret = 0;
	int temp_vbus = power_supply_app_get_usb_voltage_now();

	ret = power_glink_get_property_value(POWER_GLINK_PROP_ID_SET_WLSBST, &vwls, GLINK_DATA_ONE);
	if (ret) {
		*vbus = 0;
		return -EINVAL;
	}

	*vbus = temp_vbus >= vwls ? temp_vbus : vwls;
	return 0;
}

static int adsp_charger_get_vsys(int *vsys)
{
	int ret;

	if(!vsys)
		return -EINVAL;

	ret = power_glink_get_property_value(POWER_GLINK_PROP_ID_GET_VOLTAGE_SYS, (unsigned int *)vsys, GLINK_DATA_ONE);
	if (ret) {
		*vsys = 0;
		return -EINVAL;
	}
	hwlog_info("get vsys val=%d\n", *vsys);
	return 0;
}

static struct charge_device_ops adsp_charger_ops = {
	.set_charger_hiz = adsp_charger_set_hiz,
	.set_batfet_disable = adsp_charger_set_batfet_disable,
	.get_vbus = adsp_charger_get_vbus,
	.get_vsys = adsp_charger_get_vsys,
};

static int __init adsp_charger_init(void)
{
	return charge_ops_register(&adsp_charger_ops, BUCK_IC_TYPE_PLATFORM);
}

static void __exit adsp_charger_exit(void)
{
	return;
}

module_init(adsp_charger_init);
module_exit(adsp_charger_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("adsp charger driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
