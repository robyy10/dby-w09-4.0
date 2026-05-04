/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lcd_kit_hybrid_recover.c
 *
 * source file for hybrid mipi switch recover
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
#ifndef __LCD_KIT_HYBRID_RECOVER_H_
#define __LCD_KIT_HYBRID_RECOVER_H_

#include "lcd_kit_drm_panel.h"

/*
 * enable/disable lcd esd
 * @param panel the display panel to operation
 * @param enable true for enable, false for disable
 */
void lcd_kit_esd_enable(struct dsi_panel *panel, bool enable);

void sde_connector_schedule_status_work(struct drm_connector *connector, bool en);

/*
 * recover lcd in recovery & erecovery
 * @param panel the display panel to operation
 * @return true for get in recovery mode
 */
bool lcd_kit_recover_recovery(struct dsi_panel *panel);

/*
 * recover lcd backlight
 * @param panel the display panel to operation
 */
void hybrid_recovery_backlight(struct dsi_panel *panel);

/*
 * check recovery or erecovery mode
 * @return true for get in recovery or erecovery mode
 */
bool lcd_kit_check_recovery(void);

#endif
