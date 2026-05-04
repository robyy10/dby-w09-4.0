/* SPDX-License-Identifier: GPL-2.0 */
/*
 * direct_charge_cable.h
 *
 * cable detect for direct charge module
 *
 * Copyright (c) 2020-2020 Huawei Technologies Co., Ltd.
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

#ifndef _DIRECT_CHARGE_CABLE_H_
#define _DIRECT_CHARGE_CABLE_H_

#include <linux/errno.h>

#define CABLE_DETECT_CURRENT_THLD    3000

enum dc_cable_type {
	DC_UNKNOWN_CABLE,
	DC_NONSTD_CABLE,
	DC_STD_CABLE,
};

struct resist_data {
	int vadapt;
	int iadapt;
	int vbus;
	int ibus;
};

/* define cable operator for direct charge */

#define CABLE_DETECT_OK              1
#define CABLE_DETECT_NOK             0

struct dc_cable_ops {
	int (*detect)(void);
};

enum dc_cable_type_info {
	DC_CABLE_DETECT_OK,
	DC_ORIG_CABLE_TYPE,
	DC_CABLE_TYPE,
	DC_IS_CTC_CABLE,
	DC_CTC_CABLE_TYPE,
};

struct dc_cable_type_para {
	unsigned int cable_detect_ok;
	unsigned int orig_cable_type;
	unsigned int cable_type;
	bool is_ctc_cable;
	bool is_dpdm_cable;
	unsigned int ctc_cable_type;
	unsigned int cable_max_curr;
};

struct dc_cable_info {
	int std_cable_full_path_res_max;
	int ctc_cable_full_path_res_max;
	int nonstd_cable_full_path_res_max;
	u32 is_show_ico_first;
	int full_path_res_thld;
	u32 is_send_cable_type;
	bool ignore_full_path_res;
	bool cable_type_send_flag;
};

#ifdef CONFIG_DIRECT_CHARGER
int dc_cable_ops_register(struct dc_cable_ops *ops);
bool dc_is_support_cable_detect(void);
int dc_cable_detect(void);
unsigned int dc_get_cable_type_info(unsigned int type);
void dc_clear_cable_type_info(void);
int dc_get_cable_max_current(int mode);
void dc_update_cable_resistance_thld(struct dc_cable_info *info);
void dc_detect_std_cable(void);
int dc_calculate_path_resistance(int *rpath);
int dc_calculate_second_path_resistance(void);
int dc_resist_handler(int mode, int value);
int dc_second_resist_handler(void);
#else
static inline int dc_cable_ops_register(struct dc_cable_ops *ops)
{
	return -EINVAL;
}

static inline bool dc_is_support_cable_detect(void)
{
	return false;
}

static inline int dc_cable_detect(void)
{
	return -EINVAL;
}

static inline unsigned int dc_get_cable_type_info(unsigned int type)
{
	return 0;
}

static inline void dc_clear_cable_type_info(void)
{
}

static inline int dc_get_cable_max_current(int mode)
{
	return -EPERM;
}

static inline void dc_update_cable_resistance_thld(struct dc_cable_info *info)
{
}

static inline void dc_detect_std_cable(void)
{
}

static inline int dc_calculate_path_resistance(int *rpath)
{
	return -EPERM;
}

static inline int dc_calculate_second_path_resistance(int *rpath)
{
	return -EPERM;
}

static inline int dc_resist_handler(int mode, int value)
{
	return 0;
}

static inline int dc_second_resist_handler(void)
{
	return 0;
}
#endif /* CONFIG_DIRECT_CHARGER */

#endif /* _DIRECT_CHARGE_CABLE_H_ */
