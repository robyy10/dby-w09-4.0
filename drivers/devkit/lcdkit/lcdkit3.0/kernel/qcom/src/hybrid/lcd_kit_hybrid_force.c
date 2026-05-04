// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_hybrid_force.c
 *
 * source file for force request control
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
#include "lcd_kit_hybrid_force.h"
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#include "lcd_kit_hybrid_core.h"
#include "lcd_kit_hybrid_recover.h"

static int force_request_gpio;
static int force_request_irq;
static bool force_init;
static struct work_struct force_work;

static int request_force_gpio(int *gpio)
{
	struct device_node *np = NULL;
	int ret = -ENODEV;

	/* get compat node from dts tree */
	np = of_find_compatible_node(NULL, NULL, "force,request");
	if (!np) {
		LCD_KIT_ERR("%s: node not found\n", __func__);
		return ret;
	}

	/* get gpio from compat node and request the gpio */
	*gpio = of_get_named_gpio(np, "force_request", 0);
	if (*gpio < 0) {
		LCD_KIT_ERR("%s: get gpio error\n");
		return ret;
	}

	if (gpio_request(*gpio, "force_request") < 0) {
		LCD_KIT_ERR("%s: failed to request gpio\n", __func__);
		return ret;
	}

	return 0;
}

static void force_request_work(struct work_struct *work)
{
	if (lcd_kit_check_recovery())
		return;

	lcd_kit_restore_display();
}

static irqreturn_t force_handler(int irq, void *arg)
{
	if (!schedule_work(&force_work))
		LCD_KIT_WARNING("%s schedule force work error", __func__);

	return IRQ_HANDLED;
}

bool check_force_request(void)
{
	bool ret = false;

	if (!force_init)
		return false;

	if (lcd_kit_check_recovery())
		return false;

	ret = gpio_get_value(force_request_gpio);
	if (ret)
		LCD_KIT_INFO("force request gpio high\n");

	return ret;
}

int force_request_init(void)
{
	int ret;
	bool force_state = false;

	ret = request_force_gpio(&force_request_gpio);
	if (ret < 0)
		return -EFAULT;

	INIT_WORK(&force_work, force_request_work);
	force_request_irq = gpio_to_irq(force_request_gpio);
	ret = request_irq(force_request_irq, force_handler,
			  IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND,
			  "force_request", NULL);
	if (ret < 0) {
		LCD_KIT_ERR("force request irq failed: %d\n", ret);
		return -EFAULT;
	}
	force_init = true;
	force_state = check_force_request();
	LCD_KIT_INFO("force request gpio state: %d\n", force_state);
	if (force_state) {
		LCD_KIT_INFO("force request skip frame state\n");
		set_skip_frame_commit(1);
	}
	return 0;
}
