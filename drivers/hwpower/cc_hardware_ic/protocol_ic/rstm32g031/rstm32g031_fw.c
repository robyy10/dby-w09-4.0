// SPDX-License-Identifier: GPL-2.0
/*
 * rstm32g031_fw.c
 *
 * rstm32g031 firmware driver
 *
 * Copyright (c) 2023-2023 Huawei Technologies Co., Ltd.
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

#include "rstm32g031.h"
#include "rstm32g031_fw.h"
#include "rstm32g031_i2c.h"
#include "rstm32g031_mtp.h"
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <chipset_common/hwpower/common_module/power_gpio.h>
#include <chipset_common/hwpower/common_module/power_printk.h>

#define HWLOG_TAG rstm32g031_fw
HWLOG_REGIST();

static struct mtp_info g_fw_mtp_info;

static int rstm32g031_fw_check_bootloader_mode(struct rstm32g031_device_info *di);

static int rstm32g031_fw_write_cmd(struct rstm32g031_device_info *di, u16 cmd)
{
	int i;
	u8 ack;
	u8 buf[RSTM32G031_FW_CMD_SIZE] = { 0 };

	/* fill cmd */
	buf[0] = cmd >> 8;
	buf[1] = cmd & 0xFF;

	if (rstm32g031_write_word_bootloader(di, buf, RSTM32G031_FW_CMD_SIZE))
		return -EIO;

	for (i = 0; i < RSTM32G031_FW_ACK_COUNT; i++) {
		ack = 0;
		(void)rstm32g031_read_word_bootloader(di, &ack, 1);
		if (ack == RSTM32G031_FW_ACK_VAL) {
			hwlog_info("write_cmd succ: i=%d cmd=%x ack=%x\n", i, cmd, ack);
			return 0;
		}

		power_usleep(DT_USLEEP_1MS);
	}

	hwlog_err("write_cmd fail: i=%d cmd=%x\n", i, cmd);
	return -EIO;
}

static int rstm32g031_fw_write_addr(struct rstm32g031_device_info *di, u32 addr)
{
	int i;
	u8 ack;
	u8 buf[RSTM32G031_FW_ADDR_SIZE] = { 0 };

	/* fill address */
	buf[0] = addr >> 24;
	buf[1] = (addr >> 16) & 0xFF;
	buf[2] = (addr >> 8) & 0xFF;
	buf[3] = (addr >> 0) & 0xFF;
	buf[4] = buf[0];
	for (i = 1; i < RSTM32G031_FW_ADDR_SIZE - 1; i++)
		buf[4] ^= buf[i];

	if (rstm32g031_write_word_bootloader(di, buf, RSTM32G031_FW_ADDR_SIZE))
		return -EIO;

	for (i = 0; i < RSTM32G031_FW_ACK_COUNT; i++) {
		ack = 0;
		(void)rstm32g031_read_word_bootloader(di, &ack, 1);
		if (ack == RSTM32G031_FW_ACK_VAL) {
			hwlog_info("write_addr succ: i=%d addr=%x ack=%x\n", i, addr, ack);
			return 0;
		}

		power_usleep(DT_USLEEP_1MS);
	}

	hwlog_err("write_addr fail: i=%d addr=%x\n", i, addr);
	return -EIO;
}

static int rstm32g031_fw_erase(struct rstm32g031_device_info *di)
{
	int ret;
	int i;
	u8 ack;
	u8 buf[RSTM32G031_FW_ERASE_SIZE] = { 0 };

	/* special erase */
	buf[0] = 0xFF;
	buf[1] = 0xFF;
	buf[2] = buf[0] ^ buf[1];

	ret = rstm32g031_fw_write_cmd(di, RSTM32G031_FW_ERASE_CMD);
	if (ret)
		return ret;

	if (rstm32g031_write_word_bootloader(di, buf, RSTM32G031_FW_ERASE_SIZE))
		return -EIO;

	for (i = 0; i < RSTM32G031_FW_ACK_COUNT; i++) {
		ack = 0;
		(void)rstm32g031_read_word_bootloader(di, &ack, 1);
		if (ack == RSTM32G031_FW_ACK_VAL) {
			hwlog_info("erase succ: i=%d ack=%x\n", i, ack);
			return 0;
		}
		power_usleep(DT_USLEEP_20MS);
	}

	hwlog_err("erase fail\n");
	return -EIO;
}

static int rstm32g031_fw_read_data(struct rstm32g031_device_info *di, u32 addr,
	u8 *data, int len)
{
	int i;
	int ret;
	u8 ack = 0;
	u8 temp[RSTM32G031_FW_READ_OPTOPN_SIZE] = { 0 };

	ret = rstm32g031_fw_write_cmd(di, RSTM32G031_FW_READ_CMD);
	ret += rstm32g031_fw_write_addr(di, addr);
	if (ret)
		return -EIO;

	/* buf content: (len of data need to read - 1) + checksum */
	temp[0] = len - 1;
	temp[1] = 0xFF ^ temp[0];
	if (rstm32g031_write_word_bootloader(di, temp, RSTM32G031_FW_READ_OPTOPN_SIZE))
		return -EIO;

	for (i = 0; i < RSTM32G031_FW_ACK_COUNT; i++) {
		ack = 0;
		power_usleep(DT_USLEEP_2MS);
		(void)rstm32g031_read_word_bootloader(di, &ack, 1);
		if (ack == RSTM32G031_FW_ACK_VAL) {
			hwlog_info("send read num succ: i=%d len=%d ack=%x\n", i, len, ack);
			break;
		}
	}

	if (i > RSTM32G031_FW_ACK_COUNT) {
		hwlog_err("read data fail\n");
		return -EIO;
	}

	return rstm32g031_read_word_bootloader(di, data, len);
}

static int rstm32g031_fw_write_data(struct rstm32g031_device_info *di,
	const u8 *data, int len)
{
	int i;
	u8 ack;
	u8 checksum = len - 1;
	u8 buf[RSTM32G031_FW_PAGE_SIZE + 2] = {0};

	if ((len > RSTM32G031_FW_PAGE_SIZE) || (len <= 0)) {
		hwlog_err("data len illegal, len=%d\n", len);
		return -EINVAL;
	}

	/* buf content: (len of data - 1) + data + checksum */
	buf[0] = len - 1;
	for (i = 1; i <= len; i++) {
		buf[i] = data[i - 1];
		checksum ^= buf[i];
	}
	buf[len + 1] = checksum;

	if (rstm32g031_write_word_bootloader(di, buf, len + 2))
		return -EIO;

	for (i = 0; i < RSTM32G031_FW_ACK_COUNT; i++) {
		ack = 0;
		power_usleep(DT_USLEEP_10MS);
		(void)rstm32g031_read_word_bootloader(di, &ack, 1);
		if (ack == RSTM32G031_FW_ACK_VAL) {
			hwlog_info("write_data succ: i=%d len=%d ack=%x\n", i, len, ack);
			return 0;
		}
	}

	hwlog_err("write_data fail\n");
	return -EIO;
}

static int rstm32g031_fw_modify_option_byte(struct rstm32g031_device_info *di)
{
	int i;
	int ret;
	u8 temp = 0;

	ret = rstm32g031_fw_read_data(di, RSTM32G031_FW_BOOTN_ADDR, &temp, 1);
	if (ret)
		return -EIO;

	hwlog_info("option byte is 0x%x\n", temp);
	if (temp == RSTM32G031_FW_NBOOT_VAL)
		return 0;

	for (i = 0; i < RSTM32G031_FW_RETRY_COUNT; i++) {
		ret = rstm32g031_fw_write_cmd(di, RSTM32G031_FW_WRITE_CMD);
		ret += rstm32g031_fw_write_addr(di, RSTM32G031_FW_OPTION_ADDR);
		ret += rstm32g031_fw_write_data(di, g_rstm32g031_option_data, RSTM32G031_OPTION_SIZE);
		if (ret)
			return -EIO;

		/* wait for option byte set complete */
		power_msleep(DT_MSLEEP_30MS, 0, NULL);
		ret = rstm32g031_fw_read_data(di, RSTM32G031_FW_BOOTN_ADDR, &temp, 1);
		if (ret)
			return -EIO;

		hwlog_info("check option byte is 0x%x\n", temp);
		if (temp == RSTM32G031_FW_NBOOT_VAL)
			return 0;
	}

	return ret;
}

static int rstm32g031_fw_set_checksum_byte(struct rstm32g031_device_info *di, u8 ver_id)
{
	int ret;

	ret = rstm32g031_fw_write_cmd(di, RSTM32G031_FW_WRITE_CMD);
	ret += rstm32g031_fw_write_addr(di, RSTM32G031_FW_MTP_CHECK_ADDR);
	ret += rstm32g031_fw_write_data(di, &ver_id, 1);
	return ret;
}

static int rstm32g031_fw_write_mtp_data(struct rstm32g031_device_info *di,
	const u8 *mtp_data, int mtp_size)
{
	int ret;
	int size;
	int offset = 0;
	int remaining = mtp_size;
	u32 addr = RSTM32G031_FW_MTP_ADDR;

	while (remaining > 0) {
		ret = rstm32g031_fw_write_cmd(di, RSTM32G031_FW_WRITE_CMD);
		ret += rstm32g031_fw_write_addr(di, addr);
		size = (remaining > RSTM32G031_FW_PAGE_SIZE) ? RSTM32G031_FW_PAGE_SIZE : remaining;
		ret += rstm32g031_fw_write_data(di, mtp_data + offset, size);
		if (ret) {
			hwlog_err("write mtp data fail\n");
			return ret;
		}

		offset += size;
		remaining -= size;
		addr += size;
	}

	return 0;
}

static void rstm32g031_fw_change_mode(struct rstm32g031_device_info *di, int enable)
{
	int boot = enable ? RSTM32G031_FW_GPIO_HIGH : RSTM32G031_FW_GPIO_LOW;

	(void)gpio_direction_output(di->gpio_enable, boot);
	power_usleep(DT_USLEEP_2MS);

	/* reset pin pull low */
	(void)gpio_direction_output(di->gpio_reset, 0);
	power_usleep(DT_USLEEP_100US);

	/* reset pin pull high */
	(void)gpio_direction_output(di->gpio_reset, 1);
	power_usleep(DT_USLEEP_10MS);
}

static int rstm32g031_fw_program_begin(struct rstm32g031_device_info *di)
{
	int i;

	for (i = 0; i < RSTM32G031_FW_RETRY_COUNT; i++) {
		rstm32g031_fw_change_mode(di, RSTM32G031_FW_GPIO_HIGH);
		if (!rstm32g031_fw_check_bootloader_mode(di))
			return 0;
	}

	return -EINVAL;
}

static int rstm32g031_fw_program_end(struct rstm32g031_device_info *di)
{
	int ret;

	/* enable pin pull low */
	(void)gpio_direction_output(di->gpio_enable, 0);

	/* write go cmd */
	ret = rstm32g031_fw_write_cmd(di, RSTM32G031_FW_GO_CMD);
	ret += rstm32g031_fw_write_addr(di, RSTM32G031_FW_MTP_ADDR);

	/* wait for program jump */
	power_msleep(DT_MSLEEP_30MS, 0, NULL);

	return ret;
}

static int rstm32g031_fw_update_mtp(struct rstm32g031_device_info *di,
	const u8 *mtp_data, int mtp_size, u8 ver_id)
{
	int i;

	for (i = 0; i < RSTM32G031_FW_RETRY_COUNT; i++) {
		hwlog_info("mtp update start, i=%d\n", i);
		if (rstm32g031_fw_program_begin(di))
			continue;

		if (rstm32g031_fw_modify_option_byte(di))
			continue;

		if (rstm32g031_fw_erase(di))
			continue;

		if (rstm32g031_fw_write_mtp_data(di, mtp_data, mtp_size))
			continue;

		if (rstm32g031_fw_set_checksum_byte(di, ver_id))
			continue;

		if (rstm32g031_fw_program_end(di))
			continue;

		hwlog_info("mtp update end\n");
		return 0;
	}

	rstm32g031_fw_change_mode(di, RSTM32G031_FW_GPIO_LOW);
	return -EINVAL;
}

static int rstm32g031_fw_check_bootloader_mode(struct rstm32g031_device_info *di)
{
	int ret;
	u8 ack = 0;
	u8 data = 0;

	/* write ver cmd and wait ack */
	ret = rstm32g031_fw_write_cmd(di, RSTM32G031_FW_GET_VER_CMD);
	if (ret) {
		hwlog_err("not work bootloader mode\n");
		return -EIO;
	}

	/* get data and wait ack */
	ret = rstm32g031_read_word_bootloader(di, &data, 1);
	ret += rstm32g031_read_word_bootloader(di, &ack, 1);
	hwlog_info("get_data=%x ack=0x%x\n", data, ack);
	if (ret) {
		hwlog_err("not work bootloader mode\n");
		return -EIO;
	}

	hwlog_info("work bootloader mode\n");
	return 0;
}

static bool rstm32g031_fw_need_update(struct rstm32g031_device_info *di, u8 ver_id)
{
	g_fw_mtp_info.ver_id_reg = RSTM32G031_FW_VER_ID_REG;
	g_fw_mtp_info.hw_id_reg = RSTM32G031_FW_HW_ID_REG;
	g_fw_mtp_info.ver_id = RSTM32G031_MTP_VER;
	g_fw_mtp_info.mtp_size = RSTM32G031_MTP_SIZE;
	g_fw_mtp_info.mtp_data = g_rstm32g031_mtp_data;
	if (ver_id == RSTM32G031_MTP_VER)
		return false;
	else
		return true;

	return false;
}

int rstm32g031_fw_set_hw_config(struct rstm32g031_device_info *di)
{
	return rstm32g031_write_byte(di, g_fw_mtp_info.hw_id_reg, di->param_dts.hw_config);
}

int rstm32g031_fw_get_hw_config(struct rstm32g031_device_info *di)
{
	u8 id = 0;
	int ret;

	ret = rstm32g031_read_byte(di, g_fw_mtp_info.hw_id_reg, &id);
	if (ret)
		return -EINVAL;

	di->fw_hw_id = id;
	hwlog_info("fw_hw_id: [%x]=0x%x\n", g_fw_mtp_info.hw_id_reg, id);
	return 0;
}

int rstm32g031_fw_get_ver_id(struct rstm32g031_device_info *di)
{
	u8 id = 0;
	int ret;

	ret = rstm32g031_read_byte(di, g_fw_mtp_info.ver_id_reg, &id);
	if (ret)
		return -EINVAL;

	di->fw_ver_id = id;
	hwlog_info("fw_ver_id: [%x]=0x%x\n", g_fw_mtp_info.ver_id_reg, id);
	return 0;
}

int rstm32g031_fw_update_online_mtp(struct rstm32g031_device_info *di,
	u8 *mtp_data, int mtp_size, int ver_id)
{
	(void)rstm32g031_fw_get_ver_id(di);
	hwlog_info("update online mtp: ver_id=%x mtp_ver=%x\n", di->fw_ver_id, ver_id);
	return rstm32g031_fw_update_mtp(di, mtp_data, mtp_size, ver_id);
}

int rstm32g031_fw_update_mtp_check(struct rstm32g031_device_info *di)
{
	u8 buf = 0;
	int ret;

	if (rstm32g031_fw_program_begin(di)) {
		ret = 0;
		goto out;
	}

	if (rstm32g031_fw_read_data(di, RSTM32G031_FW_MTP_CHECK_ADDR, &buf, 1)) {
		ret = -EINVAL;
		goto out;
	}

	if (!rstm32g031_fw_need_update(di, buf)) {
		hwlog_info("not need update mtp: ver_id=%x, type=%d\n",
			buf, di->param_dts.int_gpio_type);
		ret = 0;
		goto out;
	}

	hwlog_info("need update mtp: ver_id=%x mtp_ver=%x\n", buf, g_fw_mtp_info.ver_id);
	return rstm32g031_fw_update_mtp(di, g_fw_mtp_info.mtp_data,
		g_fw_mtp_info.mtp_size, g_fw_mtp_info.ver_id);

out:
	rstm32g031_fw_change_mode(di, RSTM32G031_FW_GPIO_LOW);
	return ret;
}
