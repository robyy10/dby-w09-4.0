/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lcd_kit_hybrid_core.h
 *
 * head file for hybrid switch control
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
#ifndef __LCD_KIT_HYBRID_CORE_H_
#define __LCD_KIT_HYBRID_CORE_H_

#include "dsi/dsi_panel.h"
#include "dsi/dsi_display.h"
#include "lcd_kit_hybrid_parse.h"

enum hybrid_mode {
	/* AOD enum */
	HYBRID_ALPM_ON_HIGH_LIGHT = 1,
	HYBRID_ALPM_ON_LOW_LIGHT = 3,
	HYBRID_ALPM_ON_MIDDLE_LIGHT = 6,
	HYBRID_ALPM_ON_HBM_LIGHT = 16,
	HYBRID_ALPM_ON_NO_LIGHT = 17,
	HYBRID_ALPM_DISPLAY_OFF = 0,
	HYBRID_ALPM_EXIT = 2,
	HYBRID_ALPM_EXIT_INNER = 24,
	HYBRID_ALPM_TE_ON = 11,
	HYBRID_FORCE_ALPM = 15,
	/* hybrid ctrl enum */
	ALPM_ON_HYBRID = 4,
	ALPM_ON_HYBRID_EXIT = 5,
	HYBRID_ON_SWITCH_MCU = 8,
	HYBRID_ON_DELAY_SWITCH = 9,
	HYBRID_ON_SWITCH_AP = 10,
	HYBRID_ON_DELAY_SWITCH_RB = 12,
	/* lcd on/off enum inner usage */
	HYBRID_LCD_ON = 13,
	HYBRID_LCD_OFF = 14,
	/* pre on */
	HYBRID_MARK_NEXT_ON = 21,
	HYBRID_CANCEL_NEXT_ON = 22,
	HYBRID_NEXT_ON = 23,
	/* skip TP resume */
	HYBRID_TP_SKIP = 18,
	HYBRID_TP_RESUME = 19,
	/* skip alpm backlight */
	ALPM_SKIP_BACKLIGHT = 31,
	ALPM_SKIP_BACKLIGHT_RB = 32,
};

struct display_hybrid_ctrl {
	struct dsi_panel *panel;
	struct lcd_kit_hybrid_info hybrid_info;
	/* lock for hybrid mode */
	struct mutex hybrid_lock;
	struct hybrid_sw_ops *mipi_sw_ops;
	bool skip_frame_commit;
	/* flag for AP aod and set normal to MCU */
	bool force_mcu;
	int current_state;
	bool lcd_on_skipped;
	bool next_on_marked;
	bool alpm_skip_backlight;
	/* lock for set backlight */
	struct mutex backlight_lock;
};

enum display_state {
	DISPLAY_ON = 1,
	DISPLAY_AOD = 2,

	AP_DISPLAY_ON = 11,
	AP_DISPLAY_AOD = 12,
};

enum display_ddic_id {
	UNKNOWN_DDIC_ID = 0,
	VXN_DDIC_ID = 1,
	OTHER_DDIC_ID = 2,
};

/*
 * request mipi switch
 * @param value 0 for release mipi, 1 for request mipi.
 */
void hybrid_mipi_request(int value);

/*
 * check if AP hold display mipi
 * @return true for display at AP, false otherwise.
 */
bool hybrid_mipi_check(void);

/*
 * initialize the lcd hybrid
 * @param panel the display panel to initialize
 */
void lcd_kit_hybrid_init(struct dsi_panel *panel);

/*
 * release the lcd hybrid
 * @param panel the display panel to deinitialize
 */
void lcd_kit_hybrid_release(struct dsi_panel *panel);

/*
 * switch hybrid lcd mode
 * @param panel_id the id of panel to switch hybrid mode
 * @param mode the mode to switch
 * @return >= 0 for success, otherwise fail
 */
int lcd_kit_hybrid_mode(u32 panel_id, enum hybrid_mode mode);

/*
 * set dsi ulps status
 * @param display the aim display
 * @param enable true for enter, false for exit
 * @return >= 0 for success, otherwise fail
 */
int dsi_display_set_ulps(struct dsi_display *display, bool enable);

/*
 * set skip frame state
 * @param state 1 for skip
 */
void set_skip_frame_commit(int state);

/*
 * set panel to narmal
 * @return > 0 for initial code
 */
int lcd_kit_set_normal(void);

/*
 * restore display state
 */
void lcd_kit_restore_display(void);

/*
 * callback function
 */
typedef int (*mipi_state_callback)(int sw, u32 power_mode);

/*
 * callback function registration interface
 */
int lcd_kit_hybrid_mipi_state_register(mipi_state_callback callback);

/*
 * get callback function
 */
mipi_state_callback lcd_kit_get_mipi_state_callback(void);

/*
 * check panel type
 * @return true for vxn panel
 * @return false for other panel
 */
bool lcd_kit_check_vxn_panel(void);

/*
 * check need skip backlight
 * @return true for skip
 * @return false for otherwise
 */
bool hybrid_skip_backlight(void);

/*
 * lock for set backlight
 */
void hybrid_backlight_lock(void);

/*
 * unlock for set backlight
 */
void hybrid_backlight_unlock(void);

#endif
