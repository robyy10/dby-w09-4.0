// SPDX-License-Identifier: GPL-2.0
/*
 * sc8565_ovp_switch.c
 *
 * sc8565 ovp switch driver
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

#include "sc8562.h"
#include "sc8562_i2c.h"
#include <chipset_common/hwpower/hardware_channel/wired_channel_switch.h>
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <chipset_common/hwpower/common_module/power_printk.h>

#define HWLOG_TAG sc8565_ovp_switch
HWLOG_REGIST();

#define SC8565_WAIT_I2C_RESUME_TIMES     10
#define SC8565_SLEEP_TIME                50

static struct sc8562_device_info *g_sc8565_ovp_switch_di;

static int sc8565_chsw_set_wired_channel_off(struct sc8562_device_info *di, int channel_type)
{
	int ret;
	u8 value = 1 << SC8562_CTRL1_ACDRV_MANUAL_EN_SHIFT;
	u8 mask = SC8562_CTRL1_ACDRV_MANUAL_EN_MASK;

	switch (channel_type) {
	case WIRED_CHANNEL_BUCK:
	case WIRED_CHANNEL_ALL:
		mask |= SC8562_CTRL1_OVPGATE_EN_MASK;
		break;
	case WIRED_CHANNEL_WLSC:
		mask |= SC8562_CTRL1_WPCGATE_EN_MASK;
		break;
	default:
		return -EINVAL;
	}

	ret = sc8562_write_multi_mask(di, SC8562_CTRL1_REG, mask, value);
	hwlog_info("%s ic_%u channel:%d\n", __func__, di->ic_role, channel_type);
	return ret;
}

static int sc8565_chsw_set_wired_channel_on(struct sc8562_device_info *di, int channel_type)
{
	int ret;
	u8 value = SC8562_CTRL1_ACDRV_MANUAL_EN_MASK;
	u8 mask;

	switch (channel_type) {
	case WIRED_CHANNEL_BUCK:
	case WIRED_CHANNEL_ALL:
		value |= SC8562_CTRL1_OVPGATE_EN_MASK;
		break;
	case WIRED_CHANNEL_WLSC:
		value |= SC8562_CTRL1_WPCGATE_EN_MASK;
		break;
	default:
		return -EINVAL;
	}

	mask = SC8562_CTRL1_ACDRV_MANUAL_EN_MASK | SC8562_CTRL1_WPCGATE_EN_MASK |
		SC8562_CTRL1_OVPGATE_EN_MASK;
	ret = sc8562_write_multi_mask(di, SC8562_CTRL1_REG, mask, value);
	hwlog_info("%s ic_%u channel=%d\n", __func__, di->ic_role, channel_type);
	return ret;
}

static int sc8565_chsw_set_wired_channel(int channel_type, int flag)
{
	u8 val = 0;
	int ret;
	int i;
	struct sc8562_device_info *di = g_sc8565_ovp_switch_di;

	if (!di)
		return -ENODEV;

	if ((channel_type != WIRED_CHANNEL_BUCK) && (channel_type != WIRED_CHANNEL_ALL) &&
		(channel_type != WIRED_CHANNEL_WLSC))
		return 0;

	for (i = 0; i < SC8565_WAIT_I2C_RESUME_TIMES; i++) {
		if (!di->i2c_is_working) {
			hwlog_info("%s ic_%u i2c is not working\n", __func__, di->ic_role);
			power_msleep(SC8565_SLEEP_TIME, 0, NULL);
			continue;
		}
		break;
	}

	switch (flag) {
	case WIRED_CHANNEL_CUTOFF:
		ret = sc8565_chsw_set_wired_channel_off(di, channel_type);
		break;
	case WIRED_CHANNEL_RESTORE:
		ret = sc8565_chsw_set_wired_channel_on(di, channel_type);
		break;
	default:
		return -EINVAL;
	}

	ret += sc8562_read_byte(di, SC8562_CTRL1_REG, &val);
	hwlog_info("%s ic_%u reg[0x%x]=0x%x\n", __func__, di->ic_role, SC8562_CTRL1_REG, val);
	return ret;
}

static int sc8565_chsw_set_other_wired_channel(int channel_type, int flag)
{
	int ret;
	u8 val = 0;
	struct sc8562_device_info *di = g_sc8565_ovp_switch_di;

	if (!di)
		return -ENODEV;

	if ((channel_type <= WIRED_CHANNEL_BEGIN) || (channel_type >= WIRED_CHANNEL_END))
		return 0;

	ret = sc8562_read_byte(di, SC8562_CTRL5_REG, &val);
	hwlog_info("ic_%u reg[0x0f]=0x%x\n", di->ic_role, val);

	if (ret)
		hwlog_err("ic_%u %s failed\n", di->ic_role, __func__);
	return ret;
}

static int sc8565_chsw_get_wired_channel(int channel_type)
{
	u8 val = 0;
	int ret;
	int gate_state;
	struct sc8562_device_info *di = g_sc8565_ovp_switch_di;

	if (!di)
		return WIRED_CHANNEL_RESTORE;

	if ((channel_type != WIRED_CHANNEL_BUCK) &&
		(channel_type != WIRED_CHANNEL_ALL) &&
		(channel_type != WIRED_CHANNEL_WLSC))
		return WIRED_CHANNEL_RESTORE;

	ret = sc8562_read_byte(di, SC8562_CTRL5_REG, &val);
	if (ret)
		return WIRED_CHANNEL_RESTORE;

	hwlog_info("%s ic_%u reg[0x%x]=0x%x\n", __func__, di->ic_role, SC8562_CTRL5_REG, val);
	switch (channel_type) {
	case WIRED_CHANNEL_BUCK:
	case WIRED_CHANNEL_ALL:
		gate_state = (val & SC8562_CTRL5_OVPGATE_STAT_MASK) ?
			WIRED_CHANNEL_RESTORE : WIRED_CHANNEL_CUTOFF;
		break;
	case WIRED_CHANNEL_WLSC:
		gate_state = (val & SC8562_CTRL5_WPCGATE_STAT_MASK) ?
			WIRED_CHANNEL_RESTORE : WIRED_CHANNEL_CUTOFF;
		break;
	default:
		return WIRED_CHANNEL_RESTORE;
	}

	return gate_state;
}

static struct wired_chsw_device_ops sc8565_chsw_ops = {
	.set_wired_channel = sc8565_chsw_set_wired_channel,
	.set_other_wired_channel = sc8565_chsw_set_other_wired_channel,
	.get_wired_channel = sc8565_chsw_get_wired_channel,
	.set_wired_reverse_channel = NULL,
};

int sc8565_wired_chsw_ops_register(struct sc8562_device_info *di)
{
	int ret;

	if (!di || !di->wired_channel_switch)
		return 0;

	ret = wired_chsw_ops_register(&sc8565_chsw_ops);
	if (!ret)
		g_sc8565_ovp_switch_di = di;

	return ret;
}
