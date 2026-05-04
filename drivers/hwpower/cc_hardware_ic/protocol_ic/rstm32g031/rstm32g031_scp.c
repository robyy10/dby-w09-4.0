// SPDX-License-Identifier: GPL-2.0
/*
 * rstm32g031_protocol.c
 *
 * rstm32g031 protocol driver
 *
 * Copyright (c) 2023-2023 Huawei Technologies Co., Ltd.
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

#include "rstm32g031_scp.h"
#include "rstm32g031_i2c.h"
#include "rstm32g031_fw.h"
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/protocol/adapter_protocol.h>
#include <chipset_common/hwpower/protocol/adapter_protocol_scp.h>
#include <chipset_common/hwpower/protocol/adapter_protocol_fcp.h>
#include <chipset_common/hwpower/common_module/power_common_macro.h>
#include <chipset_common/hwpower/reverse_charge/reverse_charge.h>

#define HWLOG_TAG rstm32g031_scp
HWLOG_REGIST();

int rstm32g031_pre_init_check(struct rstm32g031_device_info *di)
{
	int ret;
	u8 reg_val = 0;

	if (!di)
		return -ENODEV;

	ret = rstm32g031_read_byte(di, RSTM32G031_ID_REG, &reg_val);
	if (ret) {
		hwlog_err("read id err, ret:%d\n", ret);
		return ret;
	}

	if (reg_val != RSTM32G031_ID_REG_VALUE)
		return -EINVAL;

	return 0;
}

int rstm32g031_enable_sleep(void *dev_data, int enable)
{
	int ret;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = rstm32g031_write_mask(di, RSTM32G031_RSCP_CTL_REG,
		RSTM32G031_RSCP_CTL_REG_SLEEP_EN_MASK,
		RSTM32G031_RSCP_CTL_REG_SLEEP_EN_SHIFT, enable);
	if (ret) { /* ensure sleep succ, or dpdm will be hold by mcu */
		rstm32g031_ic_reset(di);
		ret = rstm32g031_write_mask(di, RSTM32G031_RSCP_CTL_REG,
			RSTM32G031_RSCP_CTL_REG_SLEEP_EN_MASK,
			RSTM32G031_RSCP_CTL_REG_SLEEP_EN_SHIFT, enable);
	}

	di->in_sleep = 1;
	return ret;
}

static int rstm32g031_enable_rscp(void *dev_data, int enable)
{
	int ret;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = rstm32g031_write_mask(di, RSTM32G031_RSCP_CTL_REG,
		RSTM32G031_RSCP_CTL_REG_RSCP_EN_MASK,
		RSTM32G031_RSCP_CTL_REG_RSCP_EN_SHIFT, enable);
	if (ret)
		hwlog_err("enable rscp fail\n");

	return ret;
}

static int rstm32g031_get_request_ibus(void *dev_data)
{
	int ret;
	u8 reg_val;
	int request_ibus;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = rstm32g031_read_byte(di, RSTM32G031_REG_DATA_REG_CURRENT_SET, &reg_val);
	if (ret) {
		hwlog_err("read request ibus err, ret:%d\n", ret);
		return ret;
	}

	request_ibus = (int)(s16)reg_val * RSTM32G031_REG_DATA_REG_CURRENT_STEP;
	hwlog_info("request ibus is %d", request_ibus);
	return request_ibus;
}

static int rstm32g031_get_request_vbus(void *dev_data)
{
	int ret;
	u8 value[BYTE_TWO] = { 0 };
	int request_vbus;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = rstm32g031_read_block(di,
		RSTM32G031_REG_DATA_REG_VOLTAGE_SET, value, BYTE_TWO);
	if (ret) {
		hwlog_err("read request vbus err, ret:%d\n", ret);
		return ret;
	}

	request_vbus = (value[0] << POWER_BITS_PER_BYTE) | value[1];
	hwlog_info("request vbus is %d", request_vbus);
	return request_vbus;
}

static int rstm32g031_update_drop_cur(void *dev_data, int ibus)
{
	int ret;
	int drop_val;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	if (ibus >= di->param_dts.max_design_ibus)
		return rstm32g031_write_mask(di, RSTM32G031_REG_DATA_REG_SSTS_POWER,
			RSTM32G031_REG_SSTS_POWER_ENABLE_MASK,
			RSTM32G031_REG_SSTS_POWER_ENABLE_SHIFT,
			RSTM32G031_REG_SSTS_POWER_DISABLE);

	ret = rstm32g031_write_mask(di, RSTM32G031_REG_DATA_REG_SSTS_POWER,
		RSTM32G031_REG_SSTS_POWER_ENABLE_MASK,
		RSTM32G031_REG_SSTS_POWER_ENABLE_SHIFT,
		RSTM32G031_REG_SSTS_POWER_ENABLE);

	drop_val = (ibus * RSTM32G031_REG_SSTS_POWER_DROP_FACTOR) /
		di->param_dts.max_design_ibus;
	hwlog_info("drop val is %d\n", drop_val);
	ret = rstm32g031_write_mask(di, RSTM32G031_REG_DATA_REG_SSTS_POWER,
		RSTM32G031_REG_SSTS_POWER_DPARTO_MASK,
		RSTM32G031_REG_SSTS_POWER_DPARTO_SHIFT,
		drop_val);

	return ret;
}

static int rstm32g031_get_rt_ibus(void *dev_data)
{
	int ret;
	u8 reg_val;
	int ibus;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = rstm32g031_read_byte(di, RSTM32G031_REG_DATA_REG_CURRENT_GET, &reg_val);
	if (ret) {
		hwlog_err("read ibus err, ret:%d\n", ret);
		return ret;
	}

	ibus = (int)(s16)reg_val * RSTM32G031_REG_DATA_REG_CURRENT_STEP;
	hwlog_info("ibus is %d", ibus);
	return ibus;
}

static int rstm32g031_update_vbus(void *dev_data, int vbus)
{
	int ret;
	u8 value[BYTE_TWO] = { 0 };
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	value[0] = (vbus >> POWER_BITS_PER_BYTE) & POWER_MASK_BYTE;
	value[1] = vbus & POWER_MASK_BYTE;
	ret = rstm32g031_write_block(di,
		RSTM32G031_REG_DATA_REG_VOLTAGE_GET, value, BYTE_TWO);

	return ret;
}

int rstm32g031_ic_reset(void *dev_data)
{
	int ret;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	/* 0:enable pin pull low */
	ret = gpio_direction_output(di->gpio_enable, 0);
	power_usleep(DT_USLEEP_2MS);

	/* 0:reset pin pull low */
	ret += gpio_direction_output(di->gpio_reset, 0);
	power_usleep(DT_USLEEP_2MS);

	/* 1:reset pin pull high */
	ret += gpio_direction_output(di->gpio_reset, 1);
	power_usleep(DT_USLEEP_50MS);

	if (ret)
		hwlog_err("hard reset fail\n");

	di->in_sleep = 0;
	return ret;
}

static int rstm32g031_check_protocol_state(void *dev_data)
{
	int ret;
	u8 reg_val;
	int output_mode;
	struct rstm32g031_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = rstm32g031_read_byte(di, RSTM32G031_REG_CTRL_BYTE0, &reg_val);
	if (ret) {
		hwlog_err("check protocol state err, ret:%d\n", ret);
		return ret;
	}

	output_mode = (reg_val & RSTM32G031_REG_OUTPUT_MODE_MASK) >>
		RSTM32G031_REG_OUTPUT_MODE_SHIFT;
	hwlog_info("output_mode is %d", output_mode);
	return output_mode;
}

static struct rprotocol_ops rstm32g031_hwscp_ops = {
	.chip_name = "rstm32g031",
	.ic_reset = rstm32g031_ic_reset,
	.update_vbus = rstm32g031_update_vbus,
	.get_rt_ibus = rstm32g031_get_rt_ibus,
	.update_drop_cur = rstm32g031_update_drop_cur,
	.enable_rscp = rstm32g031_enable_rscp,
	.enable_sleep = rstm32g031_enable_sleep,
	.get_request_vbus = rstm32g031_get_request_vbus,
	.get_request_ibus = rstm32g031_get_request_ibus,
	.check_protocol_state = rstm32g031_check_protocol_state,
};

int rstm32g031_hwscp_register(struct rstm32g031_device_info *di)
{
	rstm32g031_hwscp_ops.dev_data = (void *)di;
	return rprotocol_ops_register(&rstm32g031_hwscp_ops);
}
