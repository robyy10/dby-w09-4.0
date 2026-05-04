/*
 * microphone_debug.h
 *
 * microphone debug driver
 *
 * Copyright (c) 2012-2019 Huawei Technologies Co., Ltd.
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
#ifndef __MICROPHONE_DEBUG_H__
#define __MICROPHONE_DEBUG_H__
#include <sound/soc.h>

enum microphone_debug_status_type {
	MICROPHONE_DEBUG_BUILTIN_MIC_GOOD = 0,
	MICROPHONE_DEBUG_BUILTIN_MIC_BAD,
};

void microphone_debug_set_state(int state);

#endif // __MICROPHONE_DEBUG_H__
