// SPDX-License-Identifier: GPL-2.0
/*
 * buck_charge_ic_manager.h
 *
 * buck charge ic management interface
 *
 * Copyright (c) 2021-2021 Huawei Technologies Co., Ltd.
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

#ifndef _BUCK_CHARGE_IC_MANAGER_H_
#define _BUCK_CHARGE_IC_MANAGER_H_

#include <chipset_common/hwpower/buck_charge/buck_charge_ic.h>

#if (defined(CONFIG_HUAWEI_CHARGER_AP) || defined(CONFIG_HUAWEI_CHARGER) || defined(CONFIG_FCP_CHARGER))
int charge_init_chip(struct charge_init_data *data);
int charger_set_hiz(int enable);
int charge_set_vbus_vset(u32 volt);
/* mivr: minimum input voltage regulation */
int charge_set_mivr(u32 volt);
int charge_set_batfet_disable(int val);
int charge_set_watchdog(int time);
int charge_reset_watchdog(void);
void charge_kick_watchdog(void);
void charge_disable_watchdog(void);
int charge_get_vusb(void);
int charge_get_vbus(void);
int charge_get_ibus(void);
int charge_get_iin_set(void);
unsigned int charge_get_charging_state(void);
int charge_set_dev_iin(int iin);
int charge_check_input_dpm_state(void);
int charge_check_charger_plugged(void);
int charge_get_vsys(int *vsys_vol);
#else
static inline int charger_set_hiz(int enable)
{
	return -EPERM;
}

static inline int charge_get_iin_set(void)
{
	return -EINVAL;
}

static inline int charge_set_dev_iin(int iin)
{
	return -EINVAL;
}

static inline unsigned int charge_get_charging_state(void)
{
	return CHAGRE_STATE_NORMAL;
}

static inline int charge_set_vbus_vset(u32 volt)
{
	return -EINVAL;
}

/* mivr: minimum input voltage regulation */
static inline int charge_set_mivr(u32 volt)
{
	return -EINVAL;
}

static inline int charge_set_batfet_disable(int val)
{
	return -EINVAL;
}

static inline int charge_set_watchdog(int time)
{
	return -EINVAL;
}

static inline int charge_reset_watchdog(void)
{
	return -EINVAL;
}

static inline void charge_kick_watchdog(void)
{
}

static inline void charge_disable_watchdog(void)
{
}

static inline int charge_get_ibus(void)
{
	return 0;
}

static inline int charge_get_vbus(void)
{
	return 0;
}

static inline int charge_get_vusb(void)
{
	return -EINVAL;
}

static inline int charge_init_chip(struct charge_init_data *data)
{
	return -EINVAL;
}

static inline int charge_check_charger_plugged(void)
{
	return -EINVAL;
}
static inline int charge_get_vsys(int *vsys_vol)
{
	return -EINVAL;
}
#endif /* CONFIG_HUAWEI_CHARGER_AP || CONFIG_HUAWEI_CHARGER || CONFIG_FCP_CHARGER */

#endif /* _BUCK_CHARGE_IC_MANAGER_H_ */
