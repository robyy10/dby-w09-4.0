// SPDX-License-Identifier: GPL-2.0
/*
 * btkb_led.h
 *
 * btkb_led driver
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

#ifndef _BTKB_LED_H_
#define _BTKB_LED_H_

struct btkb_led_ops {
	void(*led_toggle)(struct input_dev *dev, bool activate);
};

enum btkb_led_notifier_client {
	BTKB_LED_NOTIFIER_CLIENT_FB,
	BTKB_LED_NOTIFIER_CLIENT_DRM,
};

int btkb_led_register(struct btkb_led_dev *led_dev, struct input_dev *dev);
int btkb_led_unregister(struct btkb_led_dev *led_dev);
struct btkb_led_dev *input_get_led_dev(void);
#endif
