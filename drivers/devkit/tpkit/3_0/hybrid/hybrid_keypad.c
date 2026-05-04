// SPDX-License-Identifier: GPL-2.0
/*
 * hybrid_keypad.c
 *
 * source file for keypad irq solve in hybrid case
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
#include "hybrid_keypad.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio_keys.h>
#include "huawei_ts_kit.h"
#include "securec.h"
#include "ext_sensorhub_api.h"

#define KEYPAD_SERVICE_ID          0x01
#define MCU_FUNCK_COMMAND_ID       0x9B
#define KEY_SID_CID_COUNT          1
#define KEY_DATA_LEN               5
#define FUNC_IRQ_DISABLE           0x0
#define FUNC_IRQ_ENABLE            0x1

static DEFINE_MUTEX(key_init_mutex);

static struct sid_cid g_keypad_sid_cid[KEY_SID_CID_COUNT] = {
	{ KEYPAD_SERVICE_ID, MCU_FUNCK_COMMAND_ID },
};

static struct subscribe_cmds g_keypad_cmds = {
	g_keypad_sid_cid, KEY_SID_CID_COUNT,
};

struct keypad_payload {
	u8 func_command;
	int value;
};

struct keypad_info {
	bool keypad_init;
	/* lock for keypad cmd */
	struct mutex lock;
};

static struct keypad_info *g_keypad_info;
static int g_funckey_status = 1;

static int keypad_recv_mcu_cb(unsigned char service_id,
			      unsigned char command_id, unsigned char *data, int data_len);

static int register_mcu_report_callback(void)
{
	return register_data_callback(UPGRADE_CHANNEL, &g_keypad_cmds,
		keypad_recv_mcu_cb);
}

static int keypad_recv_mcu_cb(unsigned char service_id,
			      unsigned char command_id, unsigned char *data, int data_len)
{
	struct keypad_payload key_payload;

	if (!g_keypad_info || !g_keypad_info->keypad_init) {
		TS_LOG_ERR("%s hybridkey not init\n", __func__);
		return -EINVAL;
	}

	if (service_id != KEYPAD_SERVICE_ID || command_id != MCU_FUNCK_COMMAND_ID) {
		TS_LOG_ERR("%s service_id:%d command_id: %d\n", __func__, service_id, command_id);
		return -EINVAL;
	}

	if (!data || data_len != KEY_DATA_LEN) {
		TS_LOG_ERR("%s recv mcu response keypad data error datalen:%d\n",
			   __func__, data_len);
		return -EINVAL;
	}

	mutex_lock(&g_keypad_info->lock);
	if (memcpy_s(&key_payload, sizeof(key_payload), data, data_len) != EOK) {
		TS_LOG_ERR("%s cpy keypad data failed", __func__);
		goto err;
	}
	switch (key_payload.func_command) {
	case FUNC_IRQ_ENABLE:
		if (!g_funckey_status) {
			TS_LOG_INFO("%s enable the function key irq", __func__);
			func_key_irq_enable();
			g_funckey_status = 1;
		}
		break;
	case FUNC_IRQ_DISABLE:
		if (g_funckey_status) {
			TS_LOG_INFO("%s disable the function key irq", __func__);
			func_key_irq_disable();
			g_funckey_status = 0;
		}
		break;
	default:
		break;
	}
err:
	mutex_unlock(&g_keypad_info->lock);
	return 0;
}

void hybrid_key_init(void)
{
	int rc;

	mutex_lock(&key_init_mutex);
	if (g_keypad_info) {
		mutex_unlock(&key_init_mutex);
		return;
	}
	g_keypad_info = kzalloc(sizeof(*g_keypad_info), GFP_KERNEL);
	if (!g_keypad_info) {
		mutex_unlock(&key_init_mutex);
		return;
	}
	mutex_init(&g_keypad_info->lock);
	rc = register_mcu_report_callback();
	if (rc < 0) {
		TS_LOG_ERR("%s callback register fail ret=%d\n", __func__, rc);
		g_keypad_info->keypad_init = false;
	} else {
		TS_LOG_INFO("%s callback register success\n", __func__);
		g_keypad_info->keypad_init = true;
	}
	mutex_unlock(&key_init_mutex);
}

void hybrid_key_release(void)
{
	g_keypad_info->keypad_init = false;
	kfree(g_keypad_info);
	g_keypad_info = NULL;
}
