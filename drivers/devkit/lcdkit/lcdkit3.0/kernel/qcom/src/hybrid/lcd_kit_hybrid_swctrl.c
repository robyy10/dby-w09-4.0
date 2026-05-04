// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_hybrid_swctrl.c
 *
 * source file for switch control
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
#include "lcd_kit_hybrid_swctrl.h"

#include "securec.h"
#include "lcd_kit_panel.h"
#include "lcd_kit_drm_panel.h"
#include "ext_sensorhub_api.h"

#define SWITCH_TIMEOUT 500
#define STATE_SUB_COMMAND 0x02
#define STATE_ACK 0x03
#define CHECK_SW_RETRY_CNT 5
#define SEND_STATE_RETRY_CNT 3

/* details for hybrid switch controller, contains a switch interface */
struct hybrid_sw_ctrl {
	const char *name;
	int request_gpio;
	int switch_gpio;
	int switch_irq;

	struct work_struct work;
	switch_work_handle work_handle;
	struct hybrid_sw_ops sw_ops;
	int ap_ctrl_state;
	/* lock for request sync */
	struct mutex request_lock;

	enum ext_channel_id channel_id;
	u8 state_sid;
	u8 state_cid;
};

static char *str_concat(const char *prefix, const char *suffix)
{
	char *out = NULL;
	u32 len;
	int ret;

	len = strlen(prefix) + strlen(suffix) + 1;
	out = kzalloc(len, GFP_KERNEL);
	if (!out)
		return NULL;

	ret = snprintf_s(out, len, len, "%s%s", prefix, suffix);
	if (ret < 0) {
		kfree(out);
		return NULL;
	}

	return out;
}

static int request_swctrl_gpio(int *gpio, const char *prefix, const char *compat_suffix,
			       const char *name_suffix)
{
	struct device_node *np = NULL;
	char *compat = NULL;
	char *name = NULL;
	int ret = -ENODEV;

	/* get compat node from dts tree */
	compat = str_concat(prefix, compat_suffix);
	if (!compat)
		return -EFAULT;
	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np) {
		LCD_KIT_ERR("%s: node not found\n", __func__);
		goto out;
	}

	/* get gpio from compat node and request the gpio */
	name = str_concat(prefix, name_suffix);
	if (!name)
		goto out;
	*gpio = of_get_named_gpio(np, name, 0);
	if (*gpio < 0) {
		LCD_KIT_ERR("%s: get gpio error:%d.\n", name, *gpio);
		goto out;
	}

	if (gpio_request(*gpio, name) < 0) {
		LCD_KIT_ERR("%s: failed to request gpio %d for %s\n", __func__, *gpio, name);
		goto out;
	}
	ret = 0;
out:
	kfree(compat);
	kfree(name);
	return ret;
}

static void switch_work(struct work_struct *work)
{
	int req;
	int sw;
	struct hybrid_sw_ctrl *sw_ctrl = NULL;

	if (!work)
		return;

	sw_ctrl = container_of(work, struct hybrid_sw_ctrl, work);
	if (sw_ctrl->request_gpio == 0 || sw_ctrl->switch_gpio == 0)
		return;

	req = gpio_get_value(sw_ctrl->request_gpio);
	sw = gpio_get_value(sw_ctrl->switch_gpio);
	LCD_KIT_INFO("%s receive irq! req:%d, sw:%d\n", sw_ctrl->name, req, sw);

	/* set switch status */
	sw_ctrl->ap_ctrl_state = sw;
	/* call the callback handle */
	if (sw_ctrl->work_handle)
		sw_ctrl->work_handle(sw, req);
}

static int request_sync(struct hybrid_sw_ops *ops, int value)
{
	int try_times = 0;
	struct hybrid_sw_ctrl *sw_ctrl = NULL;

	/* get switch controller details by switch interface */
	if (!ops)
		return -EINVAL;
	sw_ctrl = container_of(ops, struct hybrid_sw_ctrl, sw_ops);
	if (sw_ctrl->request_gpio == 0)
		return -EINVAL;

	LCD_KIT_INFO("set %s request gpio %d!\n", sw_ctrl->name, value);
	/* output the gpio state and wait for the aim state */
	mutex_lock(&sw_ctrl->request_lock);
	gpio_direction_output(sw_ctrl->request_gpio, value);
	while (sw_ctrl->ap_ctrl_state != value) {
		/* sleep for 1000 ~ 1001 us */
		usleep_range(1000, 1001);
		if (try_times++ > SWITCH_TIMEOUT) {
			LCD_KIT_ERR("wait for %s sw timeout!\n", sw_ctrl->name, __func__);
			mutex_unlock(&sw_ctrl->request_lock);
			return -ETIME;
		}
	}
	mutex_unlock(&sw_ctrl->request_lock);
	return 0;
}

static int send_hybrid_state(struct hybrid_sw_ops *ops, u8 state)
{
	int i;
	int ret = 0;
	struct command cmd;
	unsigned char buffer[] = { STATE_SUB_COMMAND, state };
	/* 2 bytes response */
	unsigned char resp_buf[2] = {0};
	struct hybrid_sw_ctrl *sw_ctrl = NULL;
	struct cmd_resp resp;

	/* get switch controller details by switch interface */
	if (!ops)
		return -EINVAL;
	sw_ctrl = container_of(ops, struct hybrid_sw_ctrl, sw_ops);

	LCD_KIT_INFO("send %s state: %d!\n", sw_ctrl->name, state);
	cmd.send_buffer = buffer;
	cmd.send_buffer_len = sizeof(buffer);
	cmd.service_id = sw_ctrl->state_sid;
	cmd.command_id = sw_ctrl->state_cid;
	resp.receive_buffer = resp_buf;
	resp.receive_buffer_len = sizeof(resp_buf);
	mutex_lock(&sw_ctrl->request_lock);
	for (i = 0; i < CHECK_SW_RETRY_CNT; ++i) {
		/* check switch is at AP */
		if (ops->sw_status_check(ops))
			break;
		/* delay 2ms for next check */
		mdelay(2);
	}
	if (i >= CHECK_SW_RETRY_CNT) {
		LCD_KIT_INFO("check %s is not at AP\n", sw_ctrl->name);
		goto release_lock;
	}

	for (i = 0; i < SEND_STATE_RETRY_CNT; ++i) {
		ret = send_command(sw_ctrl->channel_id, &cmd, true, &resp);
		LCD_KIT_INFO("%s send %s command state:%d ret:%d i:%d\n", __func__, sw_ctrl->name,
			state, ret, i);
		if (ret >= 0)
			break;
	}
	if (i >= SEND_STATE_RETRY_CNT) {
		LCD_KIT_INFO("send %s state err\n", sw_ctrl->name);
		goto release_lock;
	}

	if (resp.receive_buffer[0] != STATE_ACK || resp.receive_buffer[1] != state) {
		LCD_KIT_ERR("receive :%s state not match:0x%02x:0x%02x\n", sw_ctrl->name,
			    resp.receive_buffer[0], resp.receive_buffer[1]);
		ret = -EFAULT;
	}

release_lock:
	mutex_unlock(&sw_ctrl->request_lock);
	return ret;
}

static bool sw_status_check(struct hybrid_sw_ops *ops)
{
	struct hybrid_sw_ctrl *sw_ctrl = NULL;

	/* get switch controller details by switch interface */
	if (!ops)
		return false;

	sw_ctrl = container_of(ops, struct hybrid_sw_ctrl, sw_ops);
	if (sw_ctrl->switch_gpio == 0)
		return false;

	/* get the switch gpio status */
	return gpio_get_value(sw_ctrl->switch_gpio);
}

static irqreturn_t switch_handler(int irq, void *arg)
{
	struct hybrid_sw_ctrl *sw_ctrl = (struct hybrid_sw_ctrl *)arg;

	if (!sw_ctrl)
		return IRQ_HANDLED;

	schedule_work(&sw_ctrl->work);
	return IRQ_HANDLED;
}

static int hybrid_gpio_init(struct hybrid_sw_ctrl *sw_ctrl)
{
	int ret;

	LCD_KIT_INFO("+\n");
	/* initialize switch gpio */
	ret = request_swctrl_gpio(&sw_ctrl->switch_gpio, sw_ctrl->name, ",switch", "_switch");
	if (ret != 0) {
		LCD_KIT_ERR("%s ret = %d", __func__, ret);
		return -EFAULT;
	}
	if (gpio_direction_input(sw_ctrl->switch_gpio) < 0) {
		LCD_KIT_ERR("failed to set %s switch input\n", sw_ctrl->name);
		return -EFAULT;
	}
	sw_ctrl->switch_irq = gpio_to_irq(sw_ctrl->switch_gpio);

	/* initialize request gpio */
	ret = request_swctrl_gpio(&sw_ctrl->request_gpio, sw_ctrl->name, ",request", "_request");
	if (ret != 0) {
		LCD_KIT_ERR("%s ret = %d", __func__, ret);
		return -EFAULT;
	}

	INIT_WORK(&sw_ctrl->work, switch_work);

	sw_ctrl->ap_ctrl_state = gpio_get_value(sw_ctrl->switch_gpio);
	LCD_KIT_INFO("%s %s sw state=%d\n", __func__, sw_ctrl->name, sw_ctrl->ap_ctrl_state);

	/* initialize switch gpio interrupt */
	ret = request_irq(sw_ctrl->switch_irq, switch_handler,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND,
			  sw_ctrl->name, sw_ctrl);
	if (ret < 0) {
		LCD_KIT_ERR("request %s switch irq failed: %d\n", sw_ctrl->name, ret);
		return -EFAULT;
	}

	LCD_KIT_INFO("-\n");
	return 0;
}

static int hybrid_config_init(struct hybrid_sw_ctrl *sw_ctrl)
{
	struct device_node *np = NULL;
	char *compat = NULL;
	int ret = -ENODEV;

	/* get compat node from dts tree */
	compat = str_concat(sw_ctrl->name, ",request");
	if (!compat)
		return -EFAULT;
	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np) {
		LCD_KIT_ERR("%s: node not found\n", __func__);
		goto out;
	}

	/* get state commu service id */
	if (of_property_read_u8(np, "state_sid", &sw_ctrl->state_sid)) {
		LCD_KIT_ERR("of_property_read: state_sid not find\n");
		goto out;
	}

	/* get state commu command id */
	if (of_property_read_u8(np, "state_cid", &sw_ctrl->state_cid)) {
		LCD_KIT_ERR("of_property_read: state_cid not find\n");
		goto out;
	}

	/* get commu channel id */
	if (of_property_read_u32(np, "channel_id", &sw_ctrl->channel_id)) {
		LCD_KIT_ERR("of_property_read: channel_id not find\n");
		goto out;
	}

	ret = 0;
	LCD_KIT_INFO("state_sid:%02x, state_cid:%02x\n", sw_ctrl->state_sid, sw_ctrl->state_cid);
out:
	kfree(compat);
	return ret;
}

struct hybrid_sw_ops *hybrid_swctrl_init(switch_work_handle handle, const char *name)
{
	int ret;
	struct hybrid_sw_ctrl *sw_ctrl = NULL;

	if (!handle || !name)
		return NULL;

	sw_ctrl = kzalloc(sizeof(*sw_ctrl), GFP_KERNEL);
	if (!sw_ctrl)
		return NULL;
	/* initialize the switch controller details */
	sw_ctrl->name = name;
	sw_ctrl->work_handle = handle;
	sw_ctrl->sw_ops.request_sync = request_sync;
	sw_ctrl->sw_ops.sw_status_check = sw_status_check;
	sw_ctrl->sw_ops.send_hybrid_state = send_hybrid_state;
	mutex_init(&sw_ctrl->request_lock);
	ret = hybrid_gpio_init(sw_ctrl);
	if (ret < 0) {
		kfree(sw_ctrl);
		return NULL;
	}

	ret = hybrid_config_init(sw_ctrl);
	if (ret < 0) {
		kfree(sw_ctrl);
		return NULL;
	}

	/* return the interface object for caller */
	return &sw_ctrl->sw_ops;
}

static void hybrid_gpio_release(struct hybrid_sw_ctrl *sw_ctrl)
{
	if (sw_ctrl->request_gpio != 0)
		gpio_free(sw_ctrl->request_gpio);
	if (sw_ctrl->switch_gpio != 0)
		gpio_free(sw_ctrl->switch_gpio);
	if (sw_ctrl->switch_irq != 0)
		free_irq(sw_ctrl->switch_irq, sw_ctrl);
}

void hybrid_swctrl_release(struct hybrid_sw_ops *ops)
{
	struct hybrid_sw_ctrl *sw_ctrl = NULL;

	/* get switch controller details by switch interface */
	if (!ops)
		return;

	sw_ctrl = container_of(ops, struct hybrid_sw_ctrl, sw_ops);
	/* release the switch controller details */
	hybrid_gpio_release(sw_ctrl);
	kfree(sw_ctrl);
	sw_ctrl = NULL;
}
