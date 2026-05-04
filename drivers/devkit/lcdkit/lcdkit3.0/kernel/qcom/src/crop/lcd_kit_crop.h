// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_crop.h
 *
 * source file for crop feature
 *
 * Copyright (c) 2022-2023 Huawei Technologies Co., Ltd.
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

#ifndef __LCD_KIT_CROP_H_
#define __LCD_KIT_CROP_H_

#include <drm/drm_crtc.h>
#include "lcd_kit_common.h"

void lcd_kit_crop(struct drm_crtc *crtc);
void lcd_kit_crop_release(void);

#endif