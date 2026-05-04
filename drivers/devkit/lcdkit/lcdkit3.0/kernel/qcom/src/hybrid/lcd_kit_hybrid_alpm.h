/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lcd_kit_hybrid_alpm.h
 *
 * head file for hybrid aod control
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
#ifndef LCD_KIT_HYBRID_ALPM_H
#define LCD_KIT_HYBRID_ALPM_H

#include "lcd_kit_hybrid_core.h"

/*
 * go to alpm high backlight mode
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_on_high_light(struct display_hybrid_ctrl *display_ctrl);

/*
 * go to alpm low backlight mode
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_on_low_light(struct display_hybrid_ctrl *display_ctrl);

/*
 * go to alpm middle backlight mode
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_on_middle_light(struct display_hybrid_ctrl *display_ctrl);

/*
 * go to alpm hbm backlight mode
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_on_hbm_light(struct display_hybrid_ctrl *display_ctrl);

/*
 * go to alpm no backlight mode
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_on_no_light(struct display_hybrid_ctrl *display_ctrl);

/*
 * change to send frames mode
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_send_frame(struct display_hybrid_ctrl *display_ctrl);

/*
 * exit aod mode
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_exit(struct display_hybrid_ctrl *display_ctrl);

/*
 * exit aod mode, called inner
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_exit_inner(struct display_hybrid_ctrl *display_ctrl);

/*
 * go to display off at aod
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_display_off(struct display_hybrid_ctrl *display_ctrl);

/*
 * set AOD to hybrid(MCU AOD)
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int hybrid_alpm_on(struct display_hybrid_ctrl *display_ctrl);

/*
 * exit AOD from hybrid(MCU AOD)
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int hybrid_alpm_exit(struct display_hybrid_ctrl *display_ctrl);

/*
 * force AP goto AOD
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int hybrid_force_alpm(struct display_hybrid_ctrl *display_ctrl);

/*
 * force AP goto AOD without hybrid lock
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int hybrid_force_alpm_unlocked(struct display_hybrid_ctrl *display_ctrl);

/*
 * set skip backlight when goto alpm
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_skip_backlight(struct display_hybrid_ctrl *display_ctrl);

/*
 * rollback skip backlight
 * @param display_ctrl the display control
 * @return >= 0 for success, otherwise fail
 */
int alpm_skip_backlight_rb(struct display_hybrid_ctrl *display_ctrl);

#endif
