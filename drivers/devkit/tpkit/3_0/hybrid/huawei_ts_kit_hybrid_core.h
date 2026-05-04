/* SPDX-License-Identifier: GPL-2.0 */
/*
 * huawei_ts_kit_hybrid_core.h
 *
 * head file for hybrid ts control
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

#ifndef __TS_KIT_HYBRID_CORE_H_
#define __TS_KIT_HYBRID_CORE_H_
#include "huawei_ts_kit.h"

/* ts hybrid switch command list */
enum ts_kit_hybrid_cmd {
	TS_KIT_HYBRID_SUSPEND,
	TS_KIT_HYBRID_RESUME,
	TS_KIT_HYBRID_IDLE,
	TS_KIT_HYBRID_NORMAL_TO_MCU,
	TS_KIT_HYBRID_NORMAL_TO_AP,
	TS_KIT_HYBRID_IDLE_TO_MCU,
	TS_KIT_HYBRID_FORCE_IDLE,
	/* check ts_kit_hybrid_switch_control when add enum */
	TS_KIT_HYBRID_INVALID_CMD,
};

struct ts_hybrid_chip_ops {
	int (*hybrid_suspend)(void);
	int (*hybrid_resume)(void);
	int (*hybrid_idle)(void);
};

/*
 * process ts hybrid control command
 * @param proc_cmd the switch command to process
 */
void ts_kit_hybrid_switch_control(struct ts_cmd_node *proc_cmd);

/*
 * initialize the ts hybrid
 */
void ts_kit_hybrid_init(void);

/*
 * release the ts hybrid
 */
void ts_kit_hybrid_release(void);

/*
 * request i2c switch
 * @param value 0 for release i2c, 1 for request i2c.
 */
void hybrid_i2c_request(int value);

/*
 * check if AP hold i2c
 * @return true for i2c at AP, false otherwise.
 */
bool hybrid_i2c_check(void);

/*
 * send a hybrid ts command
 * @param sub_cmd hybrid command to process
 */
void send_hybrid_ts_cmd(enum ts_kit_hybrid_cmd sub_cmd);

/*
 * set tp skip or resume
 * @param state: 0 for skip next resume, 1 for resume
 */
void set_hybrid_ts_skip(u8 state);

/*
 * register hybrid ts chip operations
 * @param ops the operations to register
 */
int ts_hybrid_ops_register(struct ts_hybrid_chip_ops *ops);

#endif
