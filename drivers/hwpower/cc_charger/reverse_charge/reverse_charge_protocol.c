// SPDX-License-Identifier: GPL-2.0
/*
 * reverse_charge_protocol.c
 *
 * reverse charge protocol api
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

#include <chipset_common/hwpower/reverse_charge/reverse_charge.h>
#include <huawei_platform/log/hw_log.h>

#define HWLOG_TAG reverse_charge_rprot
HWLOG_REGIST();

int rprot_ic_reset(void)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->ic_reset) {
		hwlog_err("ic_reset is null\n");
		return -EPERM;
	}

	hwlog_info("rprot_ic_reset\n");

	return l_ops->ic_reset(l_ops->dev_data);
}

int rprot_get_rt_ibus(void)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->get_rt_ibus) {
		hwlog_err("get_ibus is null\n");
		return -EPERM;
	}

	hwlog_info("rprot_get_rt_ibus\n");

	return l_ops->get_rt_ibus(l_ops->dev_data);
}

int rprot_update_vbus(int vbus)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->update_vbus || (vbus < 0)) {
		hwlog_err("update_vbus is null, or vbus invalid\n");
		return -EPERM;
	}

	hwlog_info("rprot_update_vbus\n");

	return l_ops->update_vbus(l_ops->dev_data, vbus);
}

int rprot_update_drop_cur(int ibus)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->update_drop_cur || (ibus < 0)) {
		hwlog_err("update_drop_cur is null, or ibus invalid\n");
		return -EPERM;
	}

	hwlog_info("rprot_update_drop_cur\n");

	return l_ops->update_drop_cur(l_ops->dev_data, ibus);
}

int rprot_enable_rscp(int enable)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->enable_rscp) {
		hwlog_err("enable_rscp is null\n");
		return -EPERM;
	}

	hwlog_info("rprot_enable_rscp:%d\n", enable);

	return l_ops->enable_rscp(l_ops->dev_data, enable);
}

int rprot_enable_sleep(int enable)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->enable_sleep) {
		hwlog_err("enable_sleep is null\n");
		return -EPERM;
	}

	hwlog_info("rprot_enable_sleep:%d\n", enable);

	return l_ops->enable_sleep(l_ops->dev_data, enable);
}

int rprot_get_request_vbus(void)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->get_request_vbus) {
		hwlog_err("get_request_vbus is null\n");
		return -EPERM;
	}

	hwlog_info("rprot_get_request_vbus\n");

	return l_ops->get_request_vbus(l_ops->dev_data);
}

int rprot_get_request_ibus(void)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->get_request_ibus) {
		hwlog_err("get_request_ibus is null\n");
		return -EPERM;
	}

	hwlog_info("rprot_get_request_ibus\n");

	return l_ops->get_request_ibus(l_ops->dev_data);
}

int rprot_check_protocol_state(void)
{
	struct rprotocol_ops *l_ops = NULL;

	l_ops = rprotocol_get_ops();
	if (!l_ops)
		return -EPERM;

	if (!l_ops->check_protocol_state) {
		hwlog_err("check_protocol_state is null\n");
		return -EPERM;
	}

	hwlog_info("check_protocol_state\n");

	return l_ops->check_protocol_state(l_ops->dev_data);
}

