// SPDX-License-Identifier: GPL-2.0
/*
 * sc8545_i2c.c
 *
 * sc8545 i2c interface
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

#include "sc8545_i2c.h"
#include <chipset_common/hwpower/common_module/power_i2c.h>
#include <chipset_common/hwpower/common_module/power_printk.h>

#define HWLOG_TAG sc8545_i2c
HWLOG_REGIST();

void sc8545_fake_i2c_err_report(struct sc8545_device_info *di, unsigned int err_happen)
{
	struct nty_data *data = NULL;

	if (!di->mount_on_fake_i2c || !err_happen)
		return;

	data = &di->notify_data;
	data->event1 = (u8)di->device_id;
	data->addr = di->client->addr;

	power_event_anc_notify(POWER_ANT_SC_FAULT,
		POWER_NE_DC_FAULT_I2C_ERR, data);
}

int sc8545_write_byte(struct sc8545_device_info *di, u8 reg, u8 value)
{
	int ret;

	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	ret = power_i2c_u8_write_byte(di->client, reg, value);
	sc8545_fake_i2c_err_report(di, ret);

	return ret;
}

int sc8545_read_byte(struct sc8545_device_info *di, u8 reg, u8 *value)
{
	int ret;

	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	ret = power_i2c_u8_read_byte(di->client, reg, value);
	sc8545_fake_i2c_err_report(di, ret);

	return ret;
}

int sc8545_read_word(struct sc8545_device_info *di, u8 reg, s16 *value)
{
	u16 data = 0;
	int ret;

	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	ret = power_i2c_u8_read_word(di->client, reg, &data, true);
	sc8545_fake_i2c_err_report(di, ret);
	if (ret)
		return -EIO;

	*value = (s16)data;
	return 0;
}

int sc8545_write_mask(struct sc8545_device_info *di, u8 reg, u8 mask, u8 shift, u8 value)
{
	int ret;
	u8 val = 0;

	ret = sc8545_read_byte(di, reg, &val);
	if (ret < 0)
		return ret;

	val &= ~mask;
	val |= ((value << shift) & mask);

	return sc8545_write_byte(di, reg, val);
}
