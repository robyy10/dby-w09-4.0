// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_hybrid_recover.c
 *
 * head file for hybrid mipi switch recover
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

#include "lcd_kit_hybrid_recover.h"

#include "lcd_kit_drm_panel.h"
#include "lcd_kit_panel.h"
#include "lcd_kit_hybrid_core.h"
#include "ext_sensorhub_inner_cmd.h"

struct lcd_kit_recovery_info {
	bool enter_recovery;
	bool enter_erecovery;
};

static struct lcd_kit_recovery_info g_lcd_kit_recovery_info = {0};

void lcd_kit_esd_enable(struct dsi_panel *panel, bool enable)
{
	struct qcom_panel_info *pinfo = NULL;
	struct dsi_display *disp = NULL;
	u32 panel_id;

	LCD_KIT_INFO("esd_enable = %d\n", enable);

	panel_id = lcd_kit_get_current_panel_id(panel);
	pinfo = lcm_get_panel_info(panel_id);
	if (!pinfo) {
		LCD_KIT_ERR("pinfo is null!\n");
		return;
	}
	disp = pinfo->display;
	if (!disp) {
		LCD_KIT_ERR("disp is null!\n");
		return;
	}
	/* call sde interface to enabel/disable status recover work */
	sde_connector_schedule_status_work(disp->drm_conn, enable);
}

static struct lcd_kit_recovery_info *lcd_kit_get_recovery_info(void)
{
	return &g_lcd_kit_recovery_info;
}

static int __init lcd_kit_early_parse_recovery_cmdline(char *p)
{
	struct lcd_kit_recovery_info *recovery_info = lcd_kit_get_recovery_info();

	if (p) {
		recovery_info->enter_recovery = (strncmp(p, "1", strlen("1")) == 0) ? true : false;
		LCD_KIT_INFO("%s mode!\n", recovery_info->enter_recovery ?
			"recovery/erecovery" : "normal");
	}

	return 0;
}

early_param("enter_recovery", lcd_kit_early_parse_recovery_cmdline);

static bool lcd_kit_check_recovery_mode(void)
{
	return lcd_kit_get_recovery_info()->enter_recovery;
}

static int __init lcd_kit_early_parse_erecovery_cmdline(char *p)
{
	struct lcd_kit_recovery_info *recovery_info = lcd_kit_get_recovery_info();

	if (p) {
		recovery_info->enter_erecovery = (strncmp(p, "1", strlen("1")) == 0) ? true : false;
		LCD_KIT_INFO("%s mode!\n", recovery_info->enter_recovery ?
			"recovery/erecovery" : "normal");
	}

	return 0;
}

early_param("enter_erecovery", lcd_kit_early_parse_erecovery_cmdline);

static bool lcd_kit_check_erecovery_mode(void)
{
	return lcd_kit_get_recovery_info()->enter_erecovery;
}

void hybrid_recovery_backlight(struct dsi_panel *panel)
{
	int brightness = 0;

	if (!panel || !panel->panel_initialized)
		return;

	dsi_panel_acquire_panel_lock(panel);
	lcd_kit_esd_recovery_enable(panel);
	lcd_kit_esd_backlight_enable(panel);
	lcd_kit_esd_recover_bl(panel, &brightness);
	LCD_KIT_INFO("esd_recovery_level is %u\n", brightness);
	if (brightness > 0)
		dsi_panel_set_backlight(panel, brightness);
	else if (lcd_kit_check_recovery())
		/* set default backlight level 102 in recover mode */
		dsi_panel_set_backlight(panel, 102);

	dsi_panel_release_panel_lock(panel);
}

bool lcd_kit_check_recovery(void)
{
	if (lcd_kit_check_recovery_mode() || lcd_kit_check_erecovery_mode())
		return true;

	return false;
}

bool lcd_kit_recover_recovery(struct dsi_panel *panel)
{
	u8 data = 0x80;

	if (!panel)
		return false;

	if (!lcd_kit_check_recovery())
		return false;

	LCD_KIT_INFO("recover lcd in recovery or erecovery, send message to recovery UI");

	send_inner_command(0x90, 0x06, &data, sizeof(data));
	return true;
}
