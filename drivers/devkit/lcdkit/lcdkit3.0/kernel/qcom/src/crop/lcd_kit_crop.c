// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_crop.c
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

#include "lcd_kit_crop.h"
#include "msm_drv.h"
#include "securec.h"
#include "image_compositing_algo.h"
#include "matting_algo_task.h"

#define FB_INFO			lcd_kit_get_fb_info()
#define DEFAULT_PIXEL_FORMAT	PIX_FMT_RGBA8888
#define DEFAULT_BPP		32
#define DEFAULT_HEIGHT		466

struct plane_info {
	const int id;
	const int seq;
	void *gem_addr;
};

/* 0->plane-64, 1->plane - 67, 2->plane - 49, 3->plane - 71 */
static struct plane_info g_plane_table[IMAGE_LAYER_MAX_NUM] = {
	{ 64, 0, NULL },
	{ 67, 1, NULL },
	{ 49, 2, NULL },
	{ 71, 3, NULL },
};

static struct display_framebuffer_info_st g_fb_info;

static struct display_framebuffer_info_st *lcd_kit_get_fb_info(void)
{
	return &g_fb_info;
}

static int get_plane_seq(int plane_id)
{
	int i;

	/* match id and seq */
	for (i = 0; i < ARRAY_SIZE(g_plane_table); ++i) {
		if (plane_id != g_plane_table[i].id)
			continue;
		return g_plane_table[i].seq;
	}
	LCD_KIT_ERR("invalid plane id\n");
	return -EINVAL;
}

static int lcd_kit_crop_mem_init(int size)
{
	int i;

	/* match id and seq */
	for (i = 0; i < ARRAY_SIZE(g_plane_table); ++i) {
		if (g_plane_table[i].gem_addr)
			continue;
		g_plane_table[i].gem_addr = kvzalloc(size, GFP_KERNEL);
		if (!g_plane_table[i].gem_addr) {
			LCD_KIT_ERR("fb%d alloc gem fail!", i);
			return LCD_KIT_FAIL;
		}
	}
	return 0;
}

static void *get_plane_gem_addr(int plane_id, int size)
{
	int i;
	int ret;

	/* match id and seq */
	for (i = 0; i < ARRAY_SIZE(g_plane_table); ++i) {
		if (plane_id != g_plane_table[i].id)
			continue;

		if (g_plane_table[i].gem_addr)
			return g_plane_table[i].gem_addr;

		ret = lcd_kit_crop_mem_init(size);
		if (ret) {
			LCD_KIT_ERR("plane-%d gem is null!", plane_id);
			return NULL;
		}
		return g_plane_table[i].gem_addr;
	}
	return NULL;
}

void lcd_kit_crop(struct drm_crtc *crtc)
{
	int id = 0;
	int seq = 0;
	struct drm_plane *plane;
	struct drm_framebuffer *fb;
	struct display_framebuffer_info_st info = {0};
	struct display_framebuffer_info_st *g_info = FB_INFO;
	void *gem_addr = NULL;

	if (!get_frame_crop_state() || !crtc)
		return;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		id = plane->base.id;
		if (!plane->state || !plane->state->fb) {
			LCD_KIT_ERR("plane-%d framebuffer is null\n", id);
			continue;
		}
		fb = plane->state->fb;
		if (fb->height != DEFAULT_HEIGHT)
			continue;

		info.fb_byte_width = fb->pitches[0];
		info.fb_width = fb->width;
		info.fb_height = fb->height;
		info.fb_length = info.fb_byte_width * info.fb_height;

		if (fb->obj[0]) {
			gem_addr = msm_gem_prime_vmap(fb->obj[0]);
		} else {
			LCD_KIT_ERR("plane-%d, fb obj is null!", id);
			continue;
		}
		seq = get_plane_seq(id);
		if (seq < 0)
			continue;
		info.fb_start_addr[seq] = get_plane_gem_addr(id, info.fb_length);
		if (info.fb_start_addr[seq]) {
			memcpy_s(info.fb_start_addr[seq], info.fb_length, gem_addr, info.fb_length);
			info.fb_num++;
		}
	}

	if (!info.fb_num)
		return;

	*g_info = info;
	g_info->fb_bit_per_pixel = DEFAULT_BPP;
	g_info->pixel_format = DEFAULT_PIXEL_FORMAT;
	set_frame_crop_state(false);
	matting_algo_get_fb_info(g_info);
}

void lcd_kit_crop_release(void)
{
	int i;

	for (i = 0; i < IMAGE_LAYER_MAX_NUM; i++) {
		if (g_plane_table[i].gem_addr) {
			kvfree(g_plane_table[i].gem_addr);
			g_plane_table[i].gem_addr = NULL;
		}
	}
}
