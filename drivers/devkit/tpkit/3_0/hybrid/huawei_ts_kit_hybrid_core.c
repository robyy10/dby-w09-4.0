// SPDX-License-Identifier: GPL-2.0
/*
 * huawei_ts_kit_hybrid_core.c
 *
 * source file for hybrid ts control
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
#include "huawei_ts_kit_hybrid_core.h"
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "securec.h"
#include "huawei_ts_kit_api.h"
#include "lcd_kit_hybrid_swctrl.h"
#if defined(CONFIG_HUAWEI_DSM)
#include <dsm/dsm_pub.h>
#include "huawei_ts_kit.h"
#endif
#include "hybrid_keypad.h"

#define MAX_I2C_DEV_NUM 2
/* lock for initial */
static DEFINE_MUTEX(init_mutex);

struct cmd_handle {
	enum ts_kit_hybrid_cmd ts_cmd;
	int (*hybrid_cmd_handle)(void);
};

struct ts_hybrid_ctrl {
	/* lock for hybrid ts cmd */
	struct mutex ts_lock;
	struct hybrid_sw_ops *i2c_sw_ops;
	/* lock for enable/disable ts irq */
	struct mutex irq_lock;
	u8 enable_irq_depth;
	/* lock for devices ops */
	struct mutex chip_ops_lock;
	struct ts_hybrid_chip_ops *chip_ops[MAX_I2C_DEV_NUM];

	u8 skip_state;
	u32 last_skip_cmd;
};

enum input_state {
	INPUT_ON = 1,
	INPUT_AOD = 2,
};

static struct ts_hybrid_ctrl *ts_ctrl;
struct ts_kit_platform_data __attribute__((weak)) g_ts_kit_platform_data;

static int ts_hybrid_suspend(void);
static int ts_hybrid_resume(void);
static int ts_hybrid_normal2mcu(void);
static int ts_hybrid_normal2ap(void);
static int ts_hybrid_idle(void);
static int ts_hybrid_force_idle(void);
static int ts_hybrid_idle2mcu(void);

static struct cmd_handle cmd_map[] = {
	{ TS_KIT_HYBRID_SUSPEND, ts_hybrid_suspend },
	{ TS_KIT_HYBRID_RESUME, ts_hybrid_resume },
	{ TS_KIT_HYBRID_NORMAL_TO_MCU, ts_hybrid_normal2mcu },
	{ TS_KIT_HYBRID_NORMAL_TO_AP, ts_hybrid_normal2ap },
	{ TS_KIT_HYBRID_IDLE, ts_hybrid_idle },
	{ TS_KIT_HYBRID_IDLE_TO_MCU, ts_hybrid_idle2mcu },
	{ TS_KIT_HYBRID_FORCE_IDLE, ts_hybrid_force_idle },
};

static int get_ts_irq_depth(int irq_id, unsigned int *depth)
{
	struct irq_desc *desc = irq_to_desc(irq_id);

	if (!desc) {
		TS_LOG_ERR("ts_kit get irq desc err\n");
		return -EINVAL;
	}
	if (desc->status_use_accessors & IRQ_PER_CPU_DEVID) {
		TS_LOG_ERR("ts_kit irq is _IRQ_PER_CPU_DEVID\n");
		return -EINVAL;
	}

	*depth = desc->depth;
	TS_LOG_INFO("ts_kit get irq depth:%lu\n", *depth);

	return 0;
}

static void enable_ts_irq(void)
{
	unsigned int i;
	unsigned int depth = 0;

	if (!ts_ctrl)
		return;

	mutex_lock(&ts_ctrl->irq_lock);
	if (ts_ctrl->enable_irq_depth == 0) {
		mutex_unlock(&ts_ctrl->irq_lock);
		return;
	}

	if (get_ts_irq_depth(g_ts_kit_platform_data.irq_id, &depth) != 0)
		depth = ts_ctrl->enable_irq_depth;

	for (i = 0; i < depth; i++)
		enable_irq(g_ts_kit_platform_data.irq_id);

	ts_ctrl->enable_irq_depth = 0;
	mutex_unlock(&ts_ctrl->irq_lock);
}

static void disable_ts_irq(void)
{
	if (!ts_ctrl)
		return;

	mutex_lock(&ts_ctrl->irq_lock);
	disable_irq(g_ts_kit_platform_data.irq_id);
	ts_ctrl->enable_irq_depth++;
	mutex_unlock(&ts_ctrl->irq_lock);
}

static void ts_kit_suspend(void)
{
	struct ts_cmd_node *in_cmd = NULL;
	struct ts_cmd_node *out_cmd = NULL;

	TS_LOG_INFO("%s get in\n", __func__);
	/* suspend ts */
	in_cmd = kzalloc(sizeof(*in_cmd), GFP_KERNEL);
	if (!in_cmd)
		goto out;
	in_cmd->command = TS_POWER_CONTROL;
	in_cmd->cmd_param.pub_params.pm_type = TS_BEFORE_SUSPEND;
	if (ts_cmd_need_process(in_cmd)) {
		out_cmd = kzalloc(sizeof(*out_cmd), GFP_KERNEL);
		if (!out_cmd)
			goto out;
		ts_power_control(g_ts_kit_platform_data.irq_id, in_cmd, out_cmd);
		in_cmd->cmd_param.pub_params.pm_type = TS_SUSPEND_DEVICE;
		ts_power_control(g_ts_kit_platform_data.irq_id, in_cmd, out_cmd);
	}
out:
	kfree(in_cmd);
	kfree(out_cmd);
}

static void ts_kit_resume(void)
{
	struct ts_cmd_node *in_cmd = NULL;
	struct ts_cmd_node *out_cmd = NULL;

	TS_LOG_INFO("%s get in\n", __func__);
	/* suspend ts */
	in_cmd = kzalloc(sizeof(*in_cmd), GFP_KERNEL);
	if (!in_cmd)
		goto out;
	in_cmd->command = TS_POWER_CONTROL;
	in_cmd->cmd_param.pub_params.pm_type = TS_RESUME_DEVICE;
	if (atomic_read(&g_ts_kit_platform_data.state) != TS_WORK) {
		out_cmd = kzalloc(sizeof(*out_cmd), GFP_KERNEL);
		if (!out_cmd)
			goto out;
		ts_power_control(g_ts_kit_platform_data.irq_id, in_cmd, out_cmd);
		in_cmd->cmd_param.pub_params.pm_type = TS_AFTER_RESUME;
		ts_power_control(g_ts_kit_platform_data.irq_id, in_cmd, out_cmd);
	}
out:
	kfree(in_cmd);
	kfree(out_cmd);
}

static void input_chips_suspend(void)
{
	int i;
	int ret;

	if (!ts_ctrl)
		return;
	/* suspend all registered devices */
	mutex_lock(&ts_ctrl->chip_ops_lock);
	for (i = 0; i < ARRAY_SIZE(ts_ctrl->chip_ops); ++i) {
		if (ts_ctrl->chip_ops[i] && ts_ctrl->chip_ops[i]->hybrid_suspend) {
			ret = ts_ctrl->chip_ops[i]->hybrid_suspend();
			/* repot dmd */
			if (ret) {
#if defined(CONFIG_HUAWEI_DSM)
				ts_dmd_report(DSM_TP_DEV_STATUS_ERROR_NO,
					"%s:suspend return err, i = %d\n", __func__, i);
#endif
				TS_LOG_ERR("%s ret = %d, i = %d\n", __func__, ret, i);
			}
		}
	}
	mutex_unlock(&ts_ctrl->chip_ops_lock);
}

static void input_chips_resume(void)
{
	int i;
	int ret;

	if (!ts_ctrl)
		return;
	/* resume all registered devices */
	mutex_lock(&ts_ctrl->chip_ops_lock);
	for (i = 0; i < ARRAY_SIZE(ts_ctrl->chip_ops); ++i) {
		if (ts_ctrl->chip_ops[i] && ts_ctrl->chip_ops[i]->hybrid_resume) {
			ret = ts_ctrl->chip_ops[i]->hybrid_resume();
			/* repot dmd */
			if (ret) {
#if defined(CONFIG_HUAWEI_DSM)
				ts_dmd_report(DSM_TP_DEV_STATUS_ERROR_NO,
					"%s:resume return err, i = %d\n", __func__, i);
#endif
				TS_LOG_ERR("%s ret = %d, i = %d\n", __func__, ret, i);
			}
		}
	}
	mutex_unlock(&ts_ctrl->chip_ops_lock);
}

static void input_chips_idle(void)
{
	int i;

	if (!ts_ctrl)
		return;
	/* idle all registered devices */
	mutex_lock(&ts_ctrl->chip_ops_lock);
	for (i = 0; i < ARRAY_SIZE(ts_ctrl->chip_ops); ++i) {
		if (ts_ctrl->chip_ops[i] && ts_ctrl->chip_ops[i]->hybrid_idle)
			ts_ctrl->chip_ops[i]->hybrid_idle();
	}
	mutex_unlock(&ts_ctrl->chip_ops_lock);
}

static int ts_hybrid_idle(void)
{
	TS_LOG_INFO("%s get in\n", __func__);
	input_chips_idle();
	enable_ts_irq();
	return 0;
}

static int ts_hybrid_force_idle(void)
{
	TS_LOG_INFO("%s get in\n", __func__);
	hybrid_i2c_request(1);
	input_chips_idle();
	enable_ts_irq();
	return 0;
}

static int ts_hybrid_suspend(void)
{
	TS_LOG_INFO("%s get in\n", __func__);
	disable_ts_irq();
	ts_kit_suspend();
	input_chips_suspend();
	hybrid_i2c_request(0);
	return 0;
}

static int ts_hybrid_resume(void)
{
	TS_LOG_INFO("%s get in\n", __func__);
	hybrid_i2c_request(1);
	input_chips_resume();
	/* if disable irq in hybrid, try to enable in resume */
	enable_ts_irq();
	return 0;
}

static int ts_hybrid_normal2ap(void)
{
	TS_LOG_INFO("%s get in\n", __func__);
	/* switch i2c to AP */
	hybrid_i2c_request(1);
	/* resume ts kit framework */
	ts_kit_resume();
	input_chips_resume();
	enable_ts_irq();
	return 0;
}

static int ts_hybrid_normal2mcu(void)
{
	TS_LOG_INFO("%s get in\n", __func__);

	ts_kit_suspend();
	input_chips_suspend();

	/* send aim state message to MCU then switch i2c to MCU */
	if (ts_ctrl && ts_ctrl->i2c_sw_ops)
		ts_ctrl->i2c_sw_ops->send_hybrid_state(ts_ctrl->i2c_sw_ops, INPUT_ON);
	hybrid_i2c_request(0);
	return 0;
}

void send_hybrid_ts_cmd(enum ts_kit_hybrid_cmd sub_cmd)
{
	struct ts_cmd_node cmd = {0};

	switch (sub_cmd) {
	case TS_KIT_HYBRID_NORMAL_TO_MCU:
	case TS_KIT_HYBRID_SUSPEND:
	case TS_KIT_HYBRID_IDLE_TO_MCU:
		/* disable irq */
		disable_ts_irq();
		break;
	default:
		break;
	}

	cmd.command = TS_HYBRID_SWITCH_CONTROL;
	cmd.cmd_param.prv_params = (void *)sub_cmd;
	/* put one hybrid command for ts thread */
	ts_kit_put_one_cmd(&cmd, 0);
}

void ts_kit_hybrid_switch_control(struct ts_cmd_node *proc_cmd)
{
	int i;
	enum ts_kit_hybrid_cmd cmd;

	TS_LOG_INFO("%s get in\n", __func__);
	if (!proc_cmd)
		return;

	cmd = (enum ts_kit_hybrid_cmd)proc_cmd->cmd_param.prv_params;
	TS_LOG_INFO("%s get hybrid cmd:%u\n", __func__, cmd);
	if (!ts_ctrl)
		return;

	mutex_lock(&ts_ctrl->ts_lock);
	if (ts_ctrl->skip_state) {
		switch (cmd) {
		case TS_KIT_HYBRID_RESUME:
		case TS_KIT_HYBRID_NORMAL_TO_AP:
		case TS_KIT_HYBRID_FORCE_IDLE:
			ts_ctrl->last_skip_cmd = cmd;
			mutex_unlock(&ts_ctrl->ts_lock);
			return;
		case TS_KIT_HYBRID_SUSPEND:
		case TS_KIT_HYBRID_NORMAL_TO_MCU:
		case TS_KIT_HYBRID_IDLE_TO_MCU:
			ts_ctrl->last_skip_cmd = TS_KIT_HYBRID_INVALID_CMD;
			ts_ctrl->skip_state = 0;
			break;
		default:
			break;
		}
	}
	/* match command and handle */
	for (i = 0; i < ARRAY_SIZE(cmd_map); ++i) {
		if (cmd != cmd_map[i].ts_cmd)
			continue;
		if (cmd_map[i].hybrid_cmd_handle)
			cmd_map[i].hybrid_cmd_handle();
		mutex_unlock(&ts_ctrl->ts_lock);
		return;
	}
	mutex_unlock(&ts_ctrl->ts_lock);
}

void hybrid_i2c_request(int value)
{
	int ret;

	/* get i2c switch interface to request i2c */
	if (!ts_ctrl || !ts_ctrl->i2c_sw_ops)
		return;

	ret = ts_ctrl->i2c_sw_ops->request_sync(ts_ctrl->i2c_sw_ops, value);
	if (ret == -ETIME) {
#if defined CONFIG_HUAWEI_DSM
		ts_dmd_report(DSM_SHB_ERR_TP_I2C_ERROR, "i2c request:%d timeout\n", value);
#endif
		TS_LOG_ERR("%s:request i2c timeout\n", __func__);
	}
}

bool hybrid_i2c_check(void)
{
	/* get i2c switch interface to check i2c status */
	if (ts_ctrl && ts_ctrl->i2c_sw_ops)
		return ts_ctrl->i2c_sw_ops->sw_status_check(ts_ctrl->i2c_sw_ops);

	return false;
}

static void i2c_switch_handle(int sw, int req)
{
	TS_LOG_INFO("%s get in\n", __func__);
}

int ts_hybrid_ops_register(struct ts_hybrid_chip_ops *ops)
{
	int i;

	if (!ops) {
		TS_LOG_ERR("%s param error\n", __func__);
		return -EFAULT;
	}

	if (!ts_ctrl)
		ts_kit_hybrid_init();
	if (!ts_ctrl)
		return -EFAULT;

	mutex_lock(&ts_ctrl->chip_ops_lock);
	for (i = 0; i < ARRAY_SIZE(ts_ctrl->chip_ops); ++i) {
		if (!ts_ctrl->chip_ops[i]) {
			ts_ctrl->chip_ops[i] = ops;
			TS_LOG_INFO("%s register dev ops at index:%d\n", __func__, i);
			break;
		}
	}
	mutex_unlock(&ts_ctrl->chip_ops_lock);
	return 0;
}

void ts_kit_hybrid_init(void)
{
	mutex_lock(&init_mutex);
	if (ts_ctrl) {
		/* already init */
		mutex_unlock(&init_mutex);
		return;
	}
	TS_LOG_INFO("%s +\n", __func__);
	ts_ctrl = kzalloc(sizeof(*ts_ctrl), GFP_KERNEL);
	if (!ts_ctrl) {
		mutex_unlock(&init_mutex);
		return;
	}
	ts_ctrl->enable_irq_depth = 0;
	ts_ctrl->last_skip_cmd = TS_KIT_HYBRID_INVALID_CMD;
	mutex_init(&ts_ctrl->ts_lock);
	mutex_init(&ts_ctrl->irq_lock);
	mutex_init(&ts_ctrl->chip_ops_lock);
	/* get the i2c switch interface */
	ts_ctrl->i2c_sw_ops = hybrid_swctrl_init(i2c_switch_handle, "i2c");
	if (!ts_ctrl->i2c_sw_ops)
		TS_LOG_ERR("%s init i2c switch failed!\n", __func__);

	/* init request i2c to AP */
	hybrid_i2c_request(1);
	hybrid_key_init();
	TS_LOG_INFO("%s -\n", __func__);
	mutex_unlock(&init_mutex);
}

void ts_kit_hybrid_release(void)
{
	if (ts_ctrl)
		hybrid_swctrl_release(ts_ctrl->i2c_sw_ops);
	kfree(ts_ctrl);
	ts_ctrl = NULL;
	hybrid_key_release();
}

static int ts_hybrid_idle2mcu(void)
{
	TS_LOG_INFO("%s get in\n", __func__);

	ts_kit_suspend();
	input_chips_suspend();

	/* send aim state message to MCU then switch i2c to MCU */
	if (ts_ctrl && ts_ctrl->i2c_sw_ops)
		ts_ctrl->i2c_sw_ops->send_hybrid_state(ts_ctrl->i2c_sw_ops, INPUT_AOD);
	hybrid_i2c_request(0);
	return 0;
}

void set_hybrid_ts_skip(u8 state)
{
	if (!ts_ctrl)
		return;

	mutex_lock(&ts_ctrl->ts_lock);
	ts_ctrl->skip_state = state;
	if (state == 0 && ts_ctrl->last_skip_cmd != TS_KIT_HYBRID_INVALID_CMD) {
		send_hybrid_ts_cmd(ts_ctrl->last_skip_cmd);
		TS_LOG_INFO("%s reset ts cmd:%d\n", __func__, ts_ctrl->last_skip_cmd);
		ts_ctrl->last_skip_cmd = TS_KIT_HYBRID_INVALID_CMD;
	}

	if (state == 1) {
		ts_ctrl->last_skip_cmd = TS_KIT_HYBRID_INVALID_CMD;
		TS_LOG_INFO("%s skip next tp resume\n", __func__);
	}

	mutex_unlock(&ts_ctrl->ts_lock);
}
