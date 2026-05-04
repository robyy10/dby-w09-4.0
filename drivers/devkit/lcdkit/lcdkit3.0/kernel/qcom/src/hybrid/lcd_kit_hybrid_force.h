/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lcd_kit_hybrid_force.h
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
#ifndef __LCD_KIT_HYBRID_FORCE_H_
#define __LCD_KIT_HYBRID_FORCE_H_
#include <linux/types.h>

bool check_force_request(void);

int force_request_init(void);
#endif
