// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_hybrid_alpm.c
 *
 * source file for hybrid aod control
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
#include "lcd_kit_hybrid_alpm.h"

#include "lcd_kit_drm_panel.h"
#include "huawei_ts_kit_hybrid_core.h"
#include "lcd_kit_hybrid_swctrl.h"

static bool hybrid_aod_state;

static struct dsi_display *lcd_get_dsi_display(struct display_hybrid_ctrl *display_ctrl)
{
	u32 panel_id = lcd_get_active_panel_id();
	struct qcom_panel_info *pinfo = NULL;

	if (!display_ctrl || !display_ctrl->panel)
		return NULL;

	pinfo = lcm_get_panel_info(panel_id);
	if (!pinfo) {
		LCD_KIT_ERR("pinfo is null!\n");
		return NULL;
	}
	return pinfo->display;
}

static void lcd_kit_mipi_state_callback(int sw, u32 power_state)
{
	mipi_state_callback callback_function = lcd_kit_get_mipi_state_callback();
	if (callback_function != NULL)
		callback_function(sw, power_state);
}

static int alpm_on_light(struct display_hybrid_ctrl *display_ctrl,
			 struct lcd_kit_dsi_panel_cmds *cmds)
{
	int ret;
	struct dsi_display *display = NULL;

	lcd_kit_mipi_state_callback(hybrid_mipi_check(), SDE_MODE_DPMS_LP1);

	display = lcd_get_dsi_display(display_ctrl);
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}

	mutex_lock(&display_ctrl->hybrid_lock);
	if (hybrid_aod_state || display_ctrl->force_mcu) {
		LCD_KIT_INFO("already get in hybrid AOD\n");
		goto end;
	}

	/* send ts event */
	send_hybrid_ts_cmd(TS_KIT_HYBRID_IDLE);
	set_skip_frame_commit(1);
	display_ctrl->panel->power_mode = SDE_MODE_DPMS_LP1;
	/* delay 20 ms for te disable vblank */
	mdelay(20);
	/* te off */
	ret = lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.te_off_cmds);
	if (ret != LCD_KIT_OK)
		LCD_KIT_WARNING("send te off cmds error\n");
	ret = lcd_kit_dsi_cmds_tx(display, cmds);
	if (ret != LCD_KIT_OK)
		LCD_KIT_WARNING("send enter aod cmds error\n");
	/* send ap aod to mcu */
	if (display_ctrl->mipi_sw_ops)
		display_ctrl->mipi_sw_ops->send_hybrid_state(display_ctrl->mipi_sw_ops,
			AP_DISPLAY_AOD);
end:
	hybrid_backlight_lock();
	display_ctrl->alpm_skip_backlight = true;
	/* reset esd_recovery_level */
	lcd_kit_dsi_panel_update_backlight(display_ctrl->panel, 1);
	display_ctrl->alpm_skip_backlight = false;
	hybrid_backlight_unlock();
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

int alpm_on_high_light(struct display_hybrid_ctrl *display_ctrl)
{
	return alpm_on_light(display_ctrl, &display_ctrl->hybrid_info.alpm_on_hl_cmds);
}

int alpm_on_low_light(struct display_hybrid_ctrl *display_ctrl)
{
	return alpm_on_light(display_ctrl, &display_ctrl->hybrid_info.alpm_on_ll_cmds);
}

int alpm_on_middle_light(struct display_hybrid_ctrl *display_ctrl)
{
	return alpm_on_light(display_ctrl, &display_ctrl->hybrid_info.alpm_on_ml_cmds);
}

int alpm_on_hbm_light(struct display_hybrid_ctrl *display_ctrl)
{
	return alpm_on_light(display_ctrl, &display_ctrl->hybrid_info.alpm_on_hbml_cmds);
}

int alpm_on_no_light(struct display_hybrid_ctrl *display_ctrl)
{
	return alpm_on_light(display_ctrl, &display_ctrl->hybrid_info.alpm_on_nl_cmds);
}

int alpm_send_frame(struct display_hybrid_ctrl *display_ctrl)
{
	int ret;
	struct dsi_display *display = NULL;

	display = lcd_get_dsi_display(display_ctrl);
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}

	mutex_lock(&display_ctrl->hybrid_lock);
	if (hybrid_aod_state || display_ctrl->force_mcu) {
		LCD_KIT_INFO("already get in hybrid AOD\n");
		goto end;
	}

	/* te on for send */
	ret = lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.te_on_cmds);
	if (ret != LCD_KIT_OK)
		LCD_KIT_WARNING("send te on cmds error\n");
	set_skip_frame_commit(0);
end:
	mutex_unlock(&display_ctrl->hybrid_lock);

	return 0;
}

static int alpm_exit_check(struct display_hybrid_ctrl *display_ctrl, bool force)
{
	struct dsi_display *display = NULL;
	uint32_t panel_id;
	uint32_t bl_level;
	int ret;

	lcd_kit_mipi_state_callback(hybrid_mipi_check(), SDE_MODE_DPMS_ON);

	display = lcd_get_dsi_display(display_ctrl);
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}

	mutex_lock(&display_ctrl->hybrid_lock);
	if (!force && (hybrid_aod_state || display_ctrl->force_mcu)) {
		LCD_KIT_INFO("already get in hybrid AOD\n");
		goto end;
	}

	if (!display_ctrl->panel->panel_initialized) {
		LCD_KIT_WARNING("Panel not initialized\n");
		mutex_unlock(&display_ctrl->hybrid_lock);
		return 0;
	}

	if (display_ctrl->panel->power_mode != SDE_MODE_DPMS_LP1 &&
	    display_ctrl->panel->power_mode != SDE_MODE_DPMS_LP2) {
		mutex_unlock(&display_ctrl->hybrid_lock);
		LCD_KIT_INFO("alpm already exit\n");
		return 0;
	}
	display_ctrl->panel->power_mode = SDE_MODE_DPMS_ON;
	ret = lcd_kit_set_normal();
	if (!force || ret > 0) {
		hybrid_backlight_lock();
		panel_id = lcd_kit_get_current_panel_id(display_ctrl->panel);
		bl_level = lcd_kit_get_current_brightness(panel_id);
		if (bl_level > 1)
			lcd_kit_dsi_panel_update_backlight(display_ctrl->panel, bl_level);
		hybrid_backlight_unlock();
	}
	/* send ts event */
	send_hybrid_ts_cmd(TS_KIT_HYBRID_RESUME);
	/* send ap aod to mcu */
	if (display_ctrl->mipi_sw_ops)
		display_ctrl->mipi_sw_ops->send_hybrid_state(display_ctrl->mipi_sw_ops,
			AP_DISPLAY_ON);
end:
	display_ctrl->alpm_skip_backlight = false;
	display_ctrl->panel->power_mode = SDE_MODE_DPMS_ON;
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

int alpm_exit(struct display_hybrid_ctrl *display_ctrl)
{
	return alpm_exit_check(display_ctrl, false);
}

int alpm_exit_inner(struct display_hybrid_ctrl *display_ctrl)
{
	return alpm_exit_check(display_ctrl, true);
}

int alpm_display_off(struct display_hybrid_ctrl *display_ctrl)
{
	return 0;
}

int hybrid_alpm_on(struct display_hybrid_ctrl *display_ctrl)
{
	int ret;
	struct dsi_display *display = NULL;

	display = lcd_get_dsi_display(display_ctrl);
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}

	mutex_lock(&display_ctrl->hybrid_lock);
	if (hybrid_aod_state) {
		LCD_KIT_INFO("already get in hybrid AOD\n");
		goto end;
	}
	if (!hybrid_mipi_check()) {
		LCD_KIT_INFO("do not hold mipi!\n");
		goto end;
	}

	hybrid_aod_state = true;
	set_skip_frame_commit(1);

	/* close te */
	lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.te_off_cmds);
	/* if AP has not get in LP mode need to wait 4 frames to avoid send frame */
	if (display_ctrl->panel->power_mode != SDE_MODE_DPMS_LP1 &&
	    display_ctrl->panel->power_mode != SDE_MODE_DPMS_LP2)
		/* wait for 4 frames * 17 ms to avoid send data */
		mdelay(4 * 17);
	/* enter ulps */
	ret = dsi_display_set_ulps(display, true);
	LCD_KIT_INFO("enter upls ret:%d\n", ret);
	send_hybrid_ts_cmd(TS_KIT_HYBRID_IDLE_TO_MCU);
	/* send AOD state to mcu */
	if (display_ctrl->mipi_sw_ops)
		display_ctrl->mipi_sw_ops->send_hybrid_state(display_ctrl->mipi_sw_ops,
							     DISPLAY_AOD);
	/* request switch to mcu */
	hybrid_mipi_request(0);
end:
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

int hybrid_alpm_exit(struct display_hybrid_ctrl *display_ctrl)
{
	uint32_t panel_id;
	uint32_t bl_level;
	struct dsi_display *display = NULL;

	display = lcd_get_dsi_display(display_ctrl);
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}

	mutex_lock(&display_ctrl->hybrid_lock);
	if (!hybrid_aod_state) {
		LCD_KIT_INFO("already exit hybrid AOD\n");
		goto end;
	}

	if (display_ctrl->panel->panel_initialized) {
		send_hybrid_ts_cmd(TS_KIT_HYBRID_NORMAL_TO_AP);
		lcd_kit_set_normal();
		hybrid_backlight_lock();
		panel_id = lcd_kit_get_current_panel_id(display_ctrl->panel);
		bl_level = lcd_kit_get_current_brightness(panel_id);
		if (bl_level > 1)
			lcd_kit_dsi_panel_update_backlight(display_ctrl->panel, bl_level);
		hybrid_backlight_unlock();
	} else {
		LCD_KIT_WARNING("Panel not initialized\n");
	}
	hybrid_aod_state = false;
	display_ctrl->force_mcu = false;
	set_skip_frame_commit(0);
end:
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

int hybrid_force_alpm_unlocked(struct display_hybrid_ctrl *display_ctrl)
{
	struct dsi_display *display = NULL;

	hybrid_aod_state = false;
	display = lcd_get_dsi_display(display_ctrl);
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}

	display_ctrl->force_mcu = false;

	if (!display_ctrl->panel->panel_initialized) {
		LCD_KIT_WARNING("Panel not initialized\n");
		return 0;
	}

	send_hybrid_ts_cmd(TS_KIT_HYBRID_FORCE_IDLE);
	dsi_display_set_ulps(display, true);
	/* request switch */
	hybrid_mipi_request(1);
	/* set into alpm without change brightness */
	lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.te_off_cmds);
	lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.alpm_on_cmds);
	display_ctrl->panel->power_mode = SDE_MODE_DPMS_LP1;
	return 0;
}

int hybrid_force_alpm(struct display_hybrid_ctrl *display_ctrl)
{
	int ret;

	if (!display_ctrl)
		return -EFAULT;

	mutex_lock(&display_ctrl->hybrid_lock);
	ret = hybrid_force_alpm_unlocked(display_ctrl);
	mutex_unlock(&display_ctrl->hybrid_lock);
	return ret;
}

int alpm_skip_backlight(struct display_hybrid_ctrl *display_ctrl)
{
	if (!display_ctrl)
		return -EINVAL;

	mutex_lock(&display_ctrl->hybrid_lock);
	display_ctrl->alpm_skip_backlight = true;
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

int alpm_skip_backlight_rb(struct display_hybrid_ctrl *display_ctrl)
{
	if (!display_ctrl)
		return -EINVAL;

	mutex_lock(&display_ctrl->hybrid_lock);
	display_ctrl->alpm_skip_backlight = false;
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}
