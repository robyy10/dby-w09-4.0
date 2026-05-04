/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rstm32g031.h
 *
 * rstm32g031 header file
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

#ifndef _RSTM32G031_H_
#define _RSTM32G031_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

#define BUF_LEN                               26
#define RSTM32G031_REG_BASE                   0x10
#define RSTM32G031_REG_NUM                    8
#define CHIP_DEV_NAME_LEN                     32

#define RSTM32G031_MAX_IBUS                   2500  /* mA */

struct rstm32g031_param {
	int wait_time;
	int hw_config; /* choose different hw configs by different boardid */
	int int_gpio_type; /* 0:int gpio for wakeup; 1:int gpio for interrupt */
	int max_design_ibus; /* 0:int gpio for wakeup; 1:int gpio for interrupt */
};

struct rstm32g031_device_info {
	struct i2c_client *client;
	struct device *dev;
	struct work_struct irq_work;
	struct rstm32g031_param param_dts;
	char name[CHIP_DEV_NAME_LEN];
	int gpio_int;
	int irq_int;
	int gpio_enable;
	int gpio_reset;
	int chip_already_init;
	int fw_hw_id;
	int fw_ver_id;
	int fw_size;
	int in_sleep;
	u8 *fw_data;
};

#endif /* _RSTM32G031_H_ */
