// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_hybrid_core.c
 *
 * source file for hybrid switch control
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
#include "lcd_kit_hybrid_core.h"

#include <linux/mutex.h>
#include "securec.h"
#include "sde/sde_connector.h"
#include "sde/sde_crtc.h"
#include <drm/drm_crtc.h>
#include "lcd_kit_drm_panel.h"
#include "lcd_kit_panel.h"
#include "lcd_kit_hybrid_swctrl.h"
#include "lcd_kit_hybrid_recover.h"
#include "lcd_kit_hybrid_alpm.h"
#include "lcd_kit_hybrid_force.h"
#include "huawei_ts_kit_hybrid_core.h"

struct hybrid_handler {
	enum hybrid_mode mode;
	int (*hybrid_handle)(struct display_hybrid_ctrl *display_ctrl);
};

#define STATUS_IDLE 0xdc
#define STATUS_NORMAL 0x9c

static int hybrid_lcd_on(struct display_hybrid_ctrl *display_ctrl);
static int hybrid_lcd_off(struct display_hybrid_ctrl *display_ctrl);
static int hybrid_on_switch_mcu(struct display_hybrid_ctrl *display_ctrl);
static int hybrid_on_switch_ap(struct display_hybrid_ctrl *display_ctrl);
static int hybrid_tp_skip(struct display_hybrid_ctrl *display_ctrl);
static int hybrid_tp_resume(struct display_hybrid_ctrl *display_ctrl);

static int hybrid_mark_next_on(struct display_hybrid_ctrl *display_ctrl);
static int hybrid_cancel_next_on(struct display_hybrid_ctrl *display_ctrl);
static int hybrid_next_on(struct display_hybrid_ctrl *display_ctrl);

static struct hybrid_handler handle_table[] = {
	{ HYBRID_LCD_ON, hybrid_lcd_on },
	{ HYBRID_LCD_OFF, hybrid_lcd_off },
	{ HYBRID_ON_SWITCH_MCU, hybrid_on_switch_mcu },
	{ HYBRID_ON_SWITCH_AP, hybrid_on_switch_ap },
	/* aod on */
	{ HYBRID_ALPM_ON_HIGH_LIGHT, alpm_on_high_light },
	{ HYBRID_ALPM_ON_LOW_LIGHT, alpm_on_low_light },
	{ HYBRID_ALPM_ON_MIDDLE_LIGHT, alpm_on_middle_light },
	{ HYBRID_ALPM_ON_HBM_LIGHT, alpm_on_hbm_light },
	{ HYBRID_ALPM_ON_NO_LIGHT, alpm_on_no_light },
	{ HYBRID_FORCE_ALPM, hybrid_force_alpm },
	/* aod send frame */
	{ HYBRID_ALPM_TE_ON, alpm_send_frame },
	{ HYBRID_ALPM_EXIT, alpm_exit },
	{ HYBRID_ALPM_EXIT_INNER, alpm_exit_inner },
	{ HYBRID_ALPM_DISPLAY_OFF, alpm_display_off },
	/* hybrid aod */
	{ ALPM_ON_HYBRID, hybrid_alpm_on },
	{ ALPM_ON_HYBRID_EXIT, hybrid_alpm_exit },
	/* mark on */
	{ HYBRID_MARK_NEXT_ON, hybrid_mark_next_on },
	{ HYBRID_CANCEL_NEXT_ON, hybrid_cancel_next_on },
	{ HYBRID_NEXT_ON, hybrid_next_on },
	/* TP latecy request */
	{ HYBRID_TP_SKIP, hybrid_tp_skip },
	{ HYBRID_TP_RESUME, hybrid_tp_resume },
	/* skip backlight when goto alpm */
	{ ALPM_SKIP_BACKLIGHT, alpm_skip_backlight },
	{ ALPM_SKIP_BACKLIGHT_RB, alpm_skip_backlight_rb },
};

static struct display_hybrid_ctrl *display_ctrl;
static int mcu_reboot_state;
static bool g_first_frame = false;
static bool g_pre_switch_mcu = false;

int release_hybrid_drm_events(void);

bool hybrid_skip_frame_commit(void)
{
	if (!display_ctrl)
		return false;

	return display_ctrl->skip_frame_commit;
}
EXPORT_SYMBOL(hybrid_skip_frame_commit);

static struct dsi_display *lcd_get_dsi_display(void)
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

static void hybrid_fence_reset(void)
{
	struct dsi_display *display = NULL;
	struct sde_connector *conn = NULL;
	struct sde_crtc *sde_crtc = NULL;

	display = lcd_get_dsi_display();
	if (!display)
		return;

	conn = to_sde_connector(display->drm_conn);
	if (!conn || !conn->encoder || !conn->encoder->crtc || !conn->retire_fence)
		return;

	sde_crtc = container_of(conn->encoder->crtc, struct sde_crtc, base);
	if (!sde_crtc || !sde_crtc->output_fence)
		return;

	LCD_KIT_INFO("retire_fence commit:%d, done:%d, output_fence commit:%d, done:%d\n",
		conn->retire_fence->commit_count, conn->retire_fence->done_count,
		sde_crtc->output_fence->commit_count, sde_crtc->output_fence->done_count);
	sde_fence_signal(conn->retire_fence, ktime_get(), SDE_FENCE_RESET_TIMELINE);
	sde_fence_signal(sde_crtc->output_fence, ktime_get(), SDE_FENCE_RESET_TIMELINE);
	LCD_KIT_INFO("reset retire_fence commit:%d, done:%d, output_fence commit:%d, done:%d\n",
	conn->retire_fence->commit_count, conn->retire_fence->done_count,
	sde_crtc->output_fence->commit_count, sde_crtc->output_fence->done_count);
}

void set_skip_frame_commit(int state)
{
	if (!display_ctrl)
		return;

	if (display_ctrl->skip_frame_commit == 1 && state == 0) {
		release_hybrid_drm_events();
		LCD_KIT_INFO("released drm events\n");
		hybrid_fence_reset();
	}

	display_ctrl->skip_frame_commit = state;
}

bool hybrid_pre_switch(void)
{
	return g_pre_switch_mcu;
}

static int hybrid_on_switch_mcu(struct display_hybrid_ctrl *display_ctrl)
{
	int ret;
	struct dsi_display *display = NULL;

	LCD_KIT_INFO("%s get in\n", __func__);
	display = lcd_get_dsi_display();
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}

	mutex_lock(&display_ctrl->hybrid_lock);
	if (display_ctrl->current_state == 0) {
		LCD_KIT_ERR("do not hold mipi request\n");
		mutex_unlock(&display_ctrl->hybrid_lock);
		return 0;
	}
	/* disable esd check */
	lcd_kit_esd_enable(display_ctrl->panel, false);
	g_pre_switch_mcu = true;
	/* te off & irq */
	ret = lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.te_off_cmds);
	if (ret != LCD_KIT_OK)
		LCD_KIT_ERR("send te off cmds error\n");
	/* send ts event */
	send_hybrid_ts_cmd(TS_KIT_HYBRID_NORMAL_TO_MCU);
	/* wait for 4 frames * 17 ms to avoid send data */
	mdelay(4 * 17);
	set_skip_frame_commit(1);
	/* enter ulps */
	ret = dsi_display_set_ulps(display, true);
	LCD_KIT_INFO("enter upls ret:%d\n", ret);
	/* send on state to mcu */
	if (display_ctrl->mipi_sw_ops)
		display_ctrl->mipi_sw_ops->send_hybrid_state(display_ctrl->mipi_sw_ops, DISPLAY_ON);
	/* request switch to mcu */
	hybrid_mipi_request(0);
	g_pre_switch_mcu = false;
	display_ctrl->force_mcu = true;
	mutex_unlock(&display_ctrl->hybrid_lock);
	/* need display off by hybrid FWK */
	return 0;
}

/* hybrid trigger to AP */
static int hybrid_on_switch_ap(struct display_hybrid_ctrl *display_ctrl)
{
	int ret;
	struct dsi_display *display = NULL;

	LCD_KIT_INFO("%s get in\n", __func__);

	display = lcd_get_dsi_display();
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return LCD_KIT_FAIL;
	}
	mutex_lock(&display_ctrl->hybrid_lock);
	dsi_panel_acquire_panel_lock(display_ctrl->panel);
	if (!display_ctrl->panel->panel_initialized) {
		LCD_KIT_WARNING("Panel not initialized\n");
		dsi_panel_release_panel_lock(display_ctrl->panel);
		mutex_unlock(&display_ctrl->hybrid_lock);
		set_skip_frame_commit(0);
		return 0;
	}
	dsi_panel_release_panel_lock(display_ctrl->panel);
	display_ctrl->force_mcu = false;
	send_hybrid_ts_cmd(TS_KIT_HYBRID_NORMAL_TO_AP);
	ret = lcd_kit_set_normal();
	if (ret > 0)
		/* reinit panel and set last backlight */
		hybrid_recovery_backlight(display_ctrl->panel);
	set_skip_frame_commit(0);
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

void set_first_frame(bool value)
{
	g_first_frame = value;
}

bool get_first_frame(void)
{
	return g_first_frame;
}

/* set power mode trigger to AP */
static int hybrid_lcd_on_skip(struct display_hybrid_ctrl *display_ctrl, bool skip)
{
	int ret;
	uint32_t panel_id;

	if (!display_ctrl)
		return -EINVAL;

	mutex_lock(&display_ctrl->hybrid_lock);
	display_ctrl->lcd_on_skipped = false;
	if (display_ctrl->next_on_marked) {
		display_ctrl->lcd_on_skipped = true;
		display_ctrl->next_on_marked = false;
		display_ctrl->panel->panel_initialized = true;
		set_first_frame(true);
		mutex_unlock(&display_ctrl->hybrid_lock);
		return 0;
	}

	display_ctrl->force_mcu = false;
	if (!skip)
		set_skip_frame_commit(0);
	hybrid_mipi_request(1);
	/* delay 2 ms before send cmd */
	mdelay(2);
	if (check_force_request()) {
		/* if mcu force request AP's LCD and TP can not used */
		set_skip_frame_commit(1);
		LCD_KIT_INFO("get mcu force request display skip frame\n");
		send_hybrid_ts_cmd(TS_KIT_HYBRID_SUSPEND);
	} else {
		send_hybrid_ts_cmd(TS_KIT_HYBRID_RESUME);
	}
	mutex_unlock(&display_ctrl->hybrid_lock);

	/* send init code */
	panel_id = lcd_kit_get_current_panel_id(display_ctrl->panel);
	lcd_kit_proxmity_proc(panel_id, LCD_RESET_HIGH);
	display_ctrl->panel->panel_initialized = true;
	if (!check_force_request() && hybrid_mipi_check()) {
		ret = lcd_kit_set_normal();
		if (ret < 0) {
			display_ctrl->panel->panel_initialized = false;
			LCD_KIT_ERR("[%s] send init cmds error:%d\n", display_ctrl->panel->name, ret);
		}
	}
	if (!skip)
		set_first_frame(true);
	return 0;
}

static int hybrid_lcd_on(struct display_hybrid_ctrl *display_ctrl)
{
	return hybrid_lcd_on_skip(display_ctrl, false);
}

static int hybrid_lcd_off(struct display_hybrid_ctrl *display_ctrl)
{
	if (!display_ctrl)
		return -EINVAL;

	mutex_lock(&display_ctrl->hybrid_lock);
	display_ctrl->alpm_skip_backlight = false;
	display_ctrl->force_mcu = false;
	send_hybrid_ts_cmd(TS_KIT_HYBRID_SUSPEND);
	hybrid_mipi_request(0);
	set_skip_frame_commit(0);
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

int lcd_kit_hybrid_mode(u32 panel_id, enum hybrid_mode mode)
{
	int i;
	int ret = 0;

	LCD_KIT_INFO("%s get in mode:%u\n", __func__, mode);
	if (!display_ctrl || !display_ctrl->panel)
		return -EFAULT;

	if (panel_id != lcd_kit_get_current_panel_id(display_ctrl->panel)) {
		LCD_KIT_ERR("%s panel id not match\n", __func__);
		return -EINVAL;
	}

	/* match mode and handle */
	for (i = 0; i < ARRAY_SIZE(handle_table); ++i) {
		if (mode != handle_table[i].mode)
			continue;
		if (handle_table[i].hybrid_handle)
			ret = handle_table[i].hybrid_handle(display_ctrl);
		return ret;
	}
	return -EINVAL;
}

static void report_mipi_timeout(int value)
{
#if defined CONFIG_HUAWEI_DSM
	int ret;
	int recordtime = 0;
	s8 record_buf[DMD_RECORD_BUF_LEN] = {'\0'};
	struct dsm_client *lcd_dclient =  lcd_kit_get_lcd_dsm_client();

	if (!lcd_dclient) {
		LCD_KIT_ERR("dsm client is invalid!\n");
		return;
	}

	ret = snprintf_s(record_buf, DMD_RECORD_BUF_LEN,
			 DMD_RECORD_BUF_LEN - 1, "mipi request:%d timeout\n", value);
	if (ret < 0) {
		LCD_KIT_ERR("snprintf happened error!\n");
		return;
	}
	(void)lcd_dsm_client_record(lcd_dclient, record_buf,
		DSM_LCD_MIPI_ERROR_NO,
		REC_DMD_NO_LIMIT,
		&recordtime);
#endif
}

void hybrid_mipi_request(int value)
{
	int ret;

	if (!display_ctrl)
		return;

	display_ctrl->current_state = value;
	/* if mcu force request display, just record current state */
	if (check_force_request())
		return;

	/* get mipi switch interface to request mipi */
	if (!display_ctrl->mipi_sw_ops)
		return;

	ret = display_ctrl->mipi_sw_ops->request_sync(display_ctrl->mipi_sw_ops, value);
	if (ret == -ETIME)
		report_mipi_timeout(value);
}

bool hybrid_mipi_check(void)
{
	/* get mipi switch interface to check mipi status */
	if (display_ctrl && display_ctrl->mipi_sw_ops)
		return display_ctrl->mipi_sw_ops->sw_status_check(display_ctrl->mipi_sw_ops);

	return false;
}

static mipi_state_callback mipi_state_callback_function = NULL;
int lcd_kit_hybrid_mipi_state_register(mipi_state_callback callback)
{
	mipi_state_callback_function = callback;
	return 0;
}
EXPORT_SYMBOL(lcd_kit_hybrid_mipi_state_register);

mipi_state_callback lcd_kit_get_mipi_state_callback(void)
{
	return mipi_state_callback_function;
}

static void mipi_switch_handle(int sw, int req)
{
	u32 power_mode = SDE_MODE_DPMS_OFF;
	struct dsi_display *display = NULL;
	struct sde_connector *conn = NULL;

	display = lcd_get_dsi_display();
	if (!display)
		return;

	if (mipi_state_callback_function != NULL) {
		if (display_ctrl && display_ctrl->panel)
			power_mode = display_ctrl->panel->power_mode;

		mipi_state_callback_function(sw, power_mode);
	}

	/* switch to mcu */
	if (!sw) {
		if (req)
			mcu_reboot_state = 1;

		if (!display_ctrl || !display_ctrl->panel)
			return;
		LCD_KIT_INFO("disable esd\n");
		/* disable esd check */
		lcd_kit_esd_enable(display_ctrl->panel, false);
		display_ctrl->panel->esd_config.esd_enabled = false;
		return;
	}
	/* switch to ap */
	if (!display_ctrl || !display_ctrl->panel)
		return;

	display_ctrl->panel->esd_config.esd_enabled = display_ctrl->hybrid_info.esd_enabled;
	if (hybrid_mipi_check() && display_ctrl->panel->power_mode != SDE_MODE_DPMS_LP1 &&
	    display_ctrl->panel->power_mode != SDE_MODE_DPMS_LP2)
		lcd_kit_esd_enable(display_ctrl->panel, true);

	if (mcu_reboot_state == 0)
		return;

	/* if mcu reboot enable esd to recover lcd */
	mcu_reboot_state = 0;
	LCD_KIT_INFO("mcu reboot\n");
	if (lcd_kit_recover_recovery(display_ctrl->panel))
		return;
	/* AP AOD */
	if (hybrid_mipi_check() && (display_ctrl->panel->power_mode == SDE_MODE_DPMS_LP1 ||
				    display_ctrl->panel->power_mode == SDE_MODE_DPMS_LP2)) {
		conn = to_sde_connector(display->drm_conn);
		_sde_connector_report_panel_dead(conn, false);
	}
}

void lcd_kit_hybrid_init(struct dsi_panel *panel)
{
	LCD_KIT_INFO("%s +\n", __func__);
	if (!panel)
		return;

	display_ctrl = kzalloc(sizeof(*display_ctrl), GFP_KERNEL);
	if (!display_ctrl)
		return;

	display_ctrl->panel = panel;
	display_ctrl->skip_frame_commit = false;
	mutex_init(&display_ctrl->hybrid_lock);
	mutex_init(&display_ctrl->backlight_lock);
	hybrid_parse_dt(panel->panel_of_node, &display_ctrl->hybrid_info);
	/* get the mipi switch interface */
	display_ctrl->mipi_sw_ops = hybrid_swctrl_init(mipi_switch_handle, "mipi");
	if (!display_ctrl->mipi_sw_ops)
		LCD_KIT_ERR("%s init mipi switch failed!\n", __func__);

	display_ctrl->current_state = 1;
	force_request_init();
	LCD_KIT_INFO("%s -\n", __func__);
}

void lcd_kit_hybrid_release(struct dsi_panel *panel)
{
	if (display_ctrl)
		hybrid_swctrl_release(display_ctrl->mipi_sw_ops);

	kfree(display_ctrl);
	display_ctrl = NULL;
}

int lcd_kit_status_check(void)
{
	u8 read_value52 = 0;
	u8 read_value54 = 0;
	u8 read_value0d = 0;
	u8 read_value0e = 0;
	u8 read_value0f = 0;
	u8 all_pixel_on = 0x10; /* register value 0x10 for all pixel on */

	struct dsi_display *display = NULL;

	display = lcd_get_dsi_display();
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return 0;
	}
	if (!hybrid_mipi_check()) {
		LCD_KIT_INFO("%s mipi not at\n", __func__);
		return 0;
	}

	lcd_kit_dsi_cmds_rx(display, &read_value52, sizeof(read_value52),
		&display_ctrl->hybrid_info.status_cmds_52);
	lcd_kit_dsi_cmds_rx(display, &read_value54, sizeof(read_value54),
		&display_ctrl->hybrid_info.status_cmds_54);
	lcd_kit_dsi_cmds_rx(display, &read_value0d, sizeof(read_value0d),
		&display_ctrl->hybrid_info.status_cmds_0d);
	lcd_kit_dsi_cmds_rx(display, &read_value0e, sizeof(read_value0e),
		&display_ctrl->hybrid_info.status_cmds_0e);
	lcd_kit_dsi_cmds_rx(display, &read_value0f, sizeof(read_value0f),
		&display_ctrl->hybrid_info.status_cmds_0f);

	LCD_KIT_INFO("read value, 0x52:0x%02x, 0x54:0x%02x, 0x0d:0x%02x, 0x0e:0x%02x, 0x0f:0x%02x\n",
		read_value52, read_value54, read_value0d, read_value0e, read_value0f);

	if ((read_value0d & all_pixel_on) != 0x00) {
		LCD_KIT_ERR("0x0D reg is all pixel on\n");
		return -EFAULT;
	}

	return 0;
}

int lcd_kit_set_normal(void)
{
	int ret;
	u8 read_value = 0;
	bool vxn_panel = false;
	struct dsi_display *display = NULL;

	display = lcd_get_dsi_display();
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return 0;
	}

	hybrid_mipi_request(1);
	ret = lcd_kit_dsi_cmds_rx(display, &read_value, sizeof(read_value),
				  &display_ctrl->hybrid_info.status_cmds);
	if (ret)
		LCD_KIT_WARNING("mipi rx failed!\n");

	LCD_KIT_INFO("get LCD status:%02x!\n", read_value);
	ret = 0;
	switch (read_value) {
	case STATUS_NORMAL:
		LCD_KIT_INFO("%s get normal status\n", __func__);
		ret = lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.te_on_cmds);
		break;
	case STATUS_IDLE:
		LCD_KIT_INFO("%s get idle status\n", __func__);
		ret = lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.alpm_exit_cmds);
		if (ret)
			break;
		/* te on */
		ret = lcd_kit_dsi_cmds_tx(display, &display_ctrl->hybrid_info.te_on_cmds);
		break;
	default:
		vxn_panel = lcd_kit_check_vxn_panel();
		LCD_KIT_INFO("%s send initial code, vxn_panel:%d\n", __func__, vxn_panel);
		dsi_panel_acquire_panel_lock(display_ctrl->panel);
		if (!display_ctrl->panel->panel_initialized) {
			LCD_KIT_WARNING("Panel not initialized\n");
			dsi_panel_release_panel_lock(display_ctrl->panel);
			set_skip_frame_commit(0);
			return 0;
		}
		if (vxn_panel)
			ret = dsi_panel_tx_cmd_set(display_ctrl->panel, DSI_CMD_SET_ON_VXN);
		else
			ret = dsi_panel_tx_cmd_set(display_ctrl->panel, DSI_CMD_SET_ON);
		dsi_panel_release_panel_lock(display_ctrl->panel);
		if (ret == LCD_KIT_OK)
			ret = 1;
	}

	set_skip_frame_commit(0);

	return ret;
}

void lcd_kit_restore_display(void)
{
	struct dsi_display *display = NULL;
	struct sde_connector *conn = NULL;

	display = lcd_get_dsi_display();
	if (!display) {
		set_skip_frame_commit(0);
		LCD_KIT_ERR("disp is null!\n");
		return;
	}

	if (display_ctrl->current_state == 0) {
		LCD_KIT_INFO("%s display do nothing\n", __func__);
		set_skip_frame_commit(0);
		return;
	}

	LCD_KIT_INFO("%s to aim state\n", __func__);
	lcd_kit_esd_recovery_enable(display_ctrl->panel);
	conn = to_sde_connector(display->drm_conn);
	_sde_connector_report_panel_dead(conn, false);

	lcd_kit_esd_enable(display_ctrl->panel, true);
	set_skip_frame_commit(0);
}

/* pre on */

static int hybrid_mark_next_on(struct display_hybrid_ctrl *display_ctrl)
{
	if (!display_ctrl)
		return -EINVAL;

	mutex_lock(&display_ctrl->hybrid_lock);
	display_ctrl->next_on_marked = true;
	display_ctrl->lcd_on_skipped = false;
	mutex_unlock(&display_ctrl->hybrid_lock);
	return 0;
}

static int hybrid_cancel_next_on(struct display_hybrid_ctrl *display_ctrl)
{
	if (!display_ctrl)
		return -EINVAL;

	mutex_lock(&display_ctrl->hybrid_lock);
	display_ctrl->next_on_marked = false;
	mutex_unlock(&display_ctrl->hybrid_lock);

	if (display_ctrl->lcd_on_skipped) {
		LCD_KIT_INFO("%s get unexpected seq try to on\n", __func__);
		set_skip_frame_commit(1);
		/* wait for 1 frame * 17 ms to avoid send data */
		mdelay(17);
		hybrid_lcd_on_skip(display_ctrl, true);
		set_skip_frame_commit(0);
	}
	display_ctrl->lcd_on_skipped = false;
	return 0;
}

static int hybrid_next_on(struct display_hybrid_ctrl *display_ctrl)
{
	if (!display_ctrl)
		return -EINVAL;

	mutex_lock(&display_ctrl->hybrid_lock);
	if (!display_ctrl->lcd_on_skipped) {
		display_ctrl->next_on_marked = false;
		mutex_unlock(&display_ctrl->hybrid_lock);
		return 0;
	}
	display_ctrl->lcd_on_skipped = false;
	display_ctrl->next_on_marked = false;
	mutex_unlock(&display_ctrl->hybrid_lock);

	set_skip_frame_commit(1);
	/* wait for 1 frame * 17 ms to avoid send data */
	mdelay(17);
	hybrid_lcd_on_skip(display_ctrl, true);
	set_skip_frame_commit(0);

	return 0;
}

bool lcd_kit_check_vxn_panel(void)
{
	int ret;
	u8 read_value = 0;
	struct dsi_display *display = NULL;
	static enum display_ddic_id ddic_id = UNKNOWN_DDIC_ID;

	LCD_KIT_INFO("%s get in\n", __func__);

	if (ddic_id == VXN_DDIC_ID)
		return true;

	if (ddic_id)
		return false;

	display = lcd_get_dsi_display();
	if (!display) {
		LCD_KIT_ERR("disp is null!\n");
		return false;
	}

	ret = lcd_kit_dsi_cmds_rx(display, &read_value, sizeof(read_value),
				  &display_ctrl->hybrid_info.ddic_id_cmds);
	if (ret) {
		LCD_KIT_WARNING("mipi rx failed!\n");
		return false;
	}

	LCD_KIT_INFO("ddic_id 0x%x\n", read_value);

	if (read_value == display_ctrl->hybrid_info.vxn_id) {
		ddic_id = VXN_DDIC_ID;
		return true;
	}

	ddic_id = OTHER_DDIC_ID;
	return false;
}

static int hybrid_tp_skip(struct display_hybrid_ctrl *display_ctrl)
{
	set_hybrid_ts_skip(1);
	return 0;
}

static int hybrid_tp_resume(struct display_hybrid_ctrl *display_ctrl)
{
	set_hybrid_ts_skip(0);
	return 0;
}

bool hybrid_skip_backlight(void)
{
	if (!display_ctrl)
		return false;

	return display_ctrl->alpm_skip_backlight;
}

void hybrid_backlight_lock(void)
{
	if (!display_ctrl)
		return;

	mutex_lock(&display_ctrl->backlight_lock);
}

void hybrid_backlight_unlock(void)
{
	if (!display_ctrl)
		return;

	mutex_unlock(&display_ctrl->backlight_lock);
}
