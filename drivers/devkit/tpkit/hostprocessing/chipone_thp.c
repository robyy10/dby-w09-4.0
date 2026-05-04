/*
 * Thp driver code for chipone
 *
 * Copyright (c) 2023 Huawei Technologies Co., Ltd.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include "huawei_thp.h"

#define MOVE_0BIT 0
#define MOVE_8BIT 8
#define MOVE_16BIT 16
#define MOVE_24BIT 24
#define OFFSET_1 1
#define OFFSET_2 2
#define OFFSET_3 3
#define OFFSET_4 4
#define OFFSET_5 5
#define SINGLE_CLICK 0x0001
#define DOUBLE_CLICK 0x0002
#define BOTH_CLICK 0x0003
#define SPEED_HZ_DRW (5 * 1000 * 1000)
#define SPEED_HZ_TRS (6 * 1000 * 1000)
#define TOUCH_GSTR_SIZE 112
#define DETECT_RETRY_TIME 3
#define DETECT_RETRY_DELAY_MS 6
#define DRW_MODE_RETRY_DELAY_MS 6
#define SPI_DELAY_MS 5
#define SPI_RETRY_TIMES 5
#define TOUCH_DRV_VERSION "v1.4"
#define TOUCH_IC_NAME "chipone"
#define TOUCH_DEV_NODE_NAME "chipone"

/* commands */
#define TOUCH_DRW_RD_CMD 0x61
#define TOUCH_DRW_WR_CMD 0x60
#define TOUCH_NORM_RD_CMD 0xF1
#define TOUCH_NORM_WR_CMD 0xF0
#define PROG_SPI_SEND_HEAD_SIZE 4
#define PROG_SPI_RECV_HEAD_SIZE 5
#define HW_REG_HARDWARE_ID 0x70000
#define HW_REG_BOOT_MODE 0x70010

/* buffer size */
#define TOUCH_SPI_BUF_SIZE (THP_MAX_FRAME_SIZE - 128)
#define TOUCH_FRAME_SIZE (PAGE_SIZE)

/* retries count */
#define TOUCH_ENTER_DRW_RETRIES 3

/* gesture flag */
#define CMD_BUF_SIZE 64
#define TCS_ERR_CODE_OK 0
#define BOOT_MODE_SRAM 0x03
#define TARGET_IC_MASTER 0x81
#define SWITCH_IC_RETRY_TIMES 2
#define GESTURE_INFO_LEN 112
#define GESTURE_D_TAP_VAL 0x50
#define ENABLE_SINGLE_CLICK 0x01
#define ENABLE_DOUBLE_CLICK 0x02
#define ENTER_PROG_MODE_RETRY_TIMES 5
#define ENTER_PROG_MODE_RETRY_DELAY 10

static uint8_t cmd_rbuf[CMD_BUF_SIZE];

enum touch_ic_type {
	IC_TYPE_ICNL9922C = 2,   /* 0x99C220 */
	IC_TYPE_ICNL9951R = 3,   /* 0x991510 */
};

enum touch_ic_hwid {
	ICNL9922C_HWID = 0x99C220,
	ICNL9951R_HWID = 0x991510,
	HWID_MASK = 0xFFFFF0,
};

/* structs for communication */
#pragma pack(push, 1)
struct touch_drw_head {
	uint8_t rwcmd;
	uint8_t addr[3];
	uint8_t len[3];
	uint8_t crc[2];
	uint8_t wait[4];
};

struct touch_drw_tail {
	uint8_t crc[2];
	uint8_t wait[1];
	uint8_t ack[2];
};

struct touch_cspi_tail {
	uint8_t ecode;
	uint8_t cmd[2];
	uint8_t crc[2];
};
#pragma pack(pop)

/* Get frame data command */
static uint8_t get_frame_cmd[] = {
	TOUCH_NORM_RD_CMD, 0x22, 0x41, 0xD4, 0x04, 0xF4, 0x7F
};

/* Set gesture report format */
static uint8_t gesture_format_cmd[] = {
	TOUCH_NORM_WR_CMD, 0x40, 0x22, 0x01, 0x00, 0x36, 0x2E,
	0x00, 0x00, 0x00
};

/* Enable double click gesture */
static uint8_t double_click_cmd[] = {
	TOUCH_NORM_WR_CMD, 0x28, 0x23, 0x04, 0x00, 0x33, 0x90,
	0x01, 0x00, 0x00, 0x00, 0x03, 0x94
};

/* Enable single click gesture */
static uint8_t single_click_cmd[] = {
	TOUCH_NORM_WR_CMD, 0x28, 0x23, 0x04, 0x00, 0x33, 0x90,
	0x00, 0x00, 0x80, 0x00, 0x09, 0x80
};

/* Enable both single gesture */
static uint8_t both_click_cmd[] = {
	TOUCH_NORM_WR_CMD, 0x28, 0x23, 0x04, 0x00, 0x90, 0x33,
	0x01, 0x00, 0x80, 0x00, 0x0A, 0x14
};

/* Enter gesture mode */
static uint8_t gesture_cmd[] = {
	TOUCH_NORM_WR_CMD, 0x04, 0x22, 0x01, 0x00, 0x28, 0x7E,
	0x03, 0x0A, 0x00
};

/* Enter sleep mode, disable gesture */
static uint8_t sleep_cmd[] = {
	TOUCH_NORM_WR_CMD, 0x04, 0x22, 0x01, 0x00, 0x28, 0x7E,
	0x02, 0x0F, 0x80
};

/* Get gesture data command */
static uint8_t get_gesture_cmd[] = {
	TOUCH_NORM_RD_CMD, 0x03, 0x41, 0x70, 0x00, 0xE9, 0xB3
};

/* Confirm gesture report received */
static uint8_t gesture_confirm_cmd[] = {
	TOUCH_NORM_WR_CMD, 0x1E, 0x23, 0x01, 0x00, 0x39, 0x36,
	0x00, 0x00, 0x00
};

static inline void put_unaligned_be16(void *p, uint16_t v)
{
	uint8_t *pu8 = (uint8_t *)p;

	pu8[0] = (v >> MOVE_8BIT) & 0xFF;
	pu8[1] = (v >> MOVE_0BIT) & 0xFF;
}

static inline void put_unaligned_be24(void *p, uint32_t v)
{
	uint8_t *pu8 = (uint8_t *)p;

	pu8[0] = (v >> MOVE_16BIT) & 0xFF;
	pu8[1] = (v >> MOVE_8BIT) & 0xFF;
	pu8[2] = (v >> MOVE_0BIT) & 0xFF;
}

static inline uint32_t get_unaligned_le24(const void *p)
{
	const uint8_t *puc = (const uint8_t *)p;

	return (puc[0] | (puc[1] << MOVE_8BIT) | (puc[2] << MOVE_16BIT));
}

static inline uint16_t get_unaligned_be16(const void *p)
{
	const uint8_t *pu8 = (const uint8_t *)p;

	return ((pu8[0] << MOVE_8BIT) | pu8[1]);
}

/* calculate 16bit crc for data */
const static uint16_t crc16_table[] = {
	0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
	0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
	0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
	0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
	0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
	0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
	0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
	0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
	0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
	0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
	0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
	0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
	0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
	0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
	0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
	0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
	0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
	0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
	0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
	0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
	0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
	0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
	0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
	0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
	0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
	0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
	0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
	0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
	0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
	0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
	0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
	0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202,
};

static uint16_t touch_driver_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc16 = 0;
	uint8_t index;

	thp_log_info("%s: called\n", __func__);
	while (len) {
		index = (((crc16 >> MOVE_8BIT) ^ *data) & 0xFF);
		crc16 = (crc16 << MOVE_8BIT) ^ crc16_table[index];
		data++;
		len--;
	}

	return crc16;
}

/* transfer spi data */
static int touch_driver_spi_transfer(struct thp_device *tdev,
	void *tbuf, void *rbuf, size_t len)
{
	struct spi_transfer xfer = {
		.tx_buf = tbuf,
		.rx_buf = rbuf,
		.len = len,
	};
	struct spi_message msg;
	int retry = 0;
	int rc = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	while (retry < SPI_RETRY_TIMES) {
		rc = thp_spi_sync(tdev->sdev, &msg);
		if (rc) {
			thp_log_err("%s: spi_sync failed: rc=%d, retry=%d\n",
				__func__, rc, retry);
			retry++;
			mdelay(SPI_DELAY_MS);
			continue;
		} else {
			break;
		}
	}

	return rc;
}

/* Dual protocols communication interface */
static int touch_dual_protocols_spi_transfer(struct thp_device *tdev,
	void *tbuf, size_t tlen)
{
	int rc = 0;
	uint8_t cmd[2];
	struct touch_cspi_tail *ptail = NULL;
	size_t crc_offset = offsetof(struct touch_cspi_tail, crc);
	uint16_t crc16_calc;
	uint16_t crc16_recv;

	thp_log_info("%s: called\n", __func__);
	cmd[0] = *((uint8_t *)tbuf + OFFSET_1);
	cmd[1] = *((uint8_t *)tbuf + OFFSET_2);

	rc = touch_driver_spi_transfer(tdev, tbuf, cmd_rbuf, tlen);
	if (rc) {
		thp_log_err("%s:cspi_transfer send failed: rc=%d\n", __func__, rc);
		return rc;
	}
	memset(cmd_rbuf, 0, CMD_BUF_SIZE);
	rc = touch_driver_spi_transfer(tdev, tbuf, cmd_rbuf,
		sizeof(struct touch_cspi_tail));
	if (rc) {
		thp_log_err("%s:cspi_transfer recv failed: rc=%d\n", __func__, rc);
		return rc;
	}
	ptail = (struct touch_cspi_tail *)cmd_rbuf;
	crc16_calc = touch_driver_crc16((uint8_t *)ptail, crc_offset);
	crc16_recv = ptail->crc[0] | (ptail->crc[1] << MOVE_8BIT);
	if (crc16_calc != crc16_recv) {
		thp_log_err("%s:crc error: calc %#06x != %#06x recv\n",
			__func__, crc16_calc, crc16_recv);
		rc = -EINVAL;
	} else {
		if (ptail->ecode) {
			thp_log_err("%s:cspi recv errno: %#04x\n", __func__, ptail->ecode);
			rc = -EINVAL;
		} else {
			rc = 0;
		}
	}

	return rc;
}

/* hardware reset */
static void touch_hw_reset(struct thp_device *tdev)
{
	thp_log_info("%s: called\n", __func__);
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	mdelay(tdev->timing_config.boot_reset_hi_delay_ms);
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_LOW);
	mdelay(tdev->timing_config.boot_reset_low_delay_ms);
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	mdelay(tdev->timing_config.boot_reset_after_delay_ms);
}

/* enter drw mode */
static int touch_driver_enter_drw_mode(struct thp_device *tdev)
{
	uint8_t enter_drw_cmd[] = { 0xCC, 0x33, 0x55, 0x5A };
	int cmd_size = sizeof(enter_drw_cmd);
	int retries = TOUCH_ENTER_DRW_RETRIES;
	int rc;

	thp_log_info("%s: called\n", __func__);
	rc = thp_bus_lock();
	if (rc < 0) {
		thp_log_err("%s:get lock fail\n", __func__);
		return -EINVAL;
	}
	memcpy(tdev->tx_buff, enter_drw_cmd, cmd_size);
	while (retries--) {
		/* tx_buff and rx_buff is for irq use only */
		rc = touch_driver_spi_transfer(tdev,
			tdev->tx_buff, tdev->rx_buff, cmd_size);
		if (!rc)
			break;
		thp_log_err("%s: Enter drw mode failed: rc=%d, retry %d\n",
			__func__, rc, retries);
		mdelay(DRW_MODE_RETRY_DELAY_MS);
	}
	thp_bus_unlock();
	if (rc)
		thp_log_err("%s: Enter drw mode failed: rc=%d\n", __func__, rc);

	return rc;
}

/* read reg value in drw mode */
static int touch_driver_drw_read_reg(struct thp_device *tdev,
	uint32_t reg_addr, uint8_t *rbuf, size_t rlen)
{
	struct touch_drw_head *psend = (struct touch_drw_head *)tdev->tx_buff;
	uint8_t *pdata = tdev->rx_buff + sizeof(struct touch_drw_head);
	uint16_t total_len = rlen + sizeof(struct touch_drw_head) +
		sizeof(struct touch_drw_tail);
	uint16_t crc16_calc;
	uint16_t crc16_recv;
	int rc;

	thp_log_info("%s: called\n", __func__);
	if (!rlen || (total_len > TOUCH_SPI_BUF_SIZE)) {
		thp_log_err("%s: Invalid read size %ld\n", __func__, rlen);
		return -EINVAL;
	}

	rc = thp_bus_lock();
	if (rc < 0) {
		thp_log_err("%s:get lock fail\n", __func__);
		return -EINVAL;
	}
	memset(tdev->tx_buff, 0, total_len);
	psend->rwcmd = TOUCH_DRW_RD_CMD;
	put_unaligned_be24(&psend->addr[0], reg_addr);
	put_unaligned_be24(&psend->len[0], rlen);
	crc16_calc = touch_driver_crc16(&psend->rwcmd,
		offsetof(struct touch_drw_head, crc));
	put_unaligned_be16(&psend->crc[0], (uint16_t)~crc16_calc);
	/* tx_buff and rx_buff is for irq use only */
	rc = touch_driver_spi_transfer(tdev, tdev->tx_buff,
		tdev->rx_buff, total_len);
	if (rc) {
		thp_log_err("%s:Drw read reg %#07x failed: rc=%d\n",
			__func__, reg_addr, rc);
	} else {
		crc16_calc = touch_driver_crc16(pdata, rlen);
		crc16_recv = ~get_unaligned_be16(pdata + rlen);
		if (crc16_calc != crc16_recv) {
			thp_log_err("%s: crc error: calc %#06x != %#06x recv\n",
				__func__, crc16_calc, crc16_recv);
			rc = -EINVAL;
		} else {
			memcpy(rbuf, pdata, rlen);
		}
	}
	thp_bus_unlock();

	return rc;
}

/* 9922c get hardware id */
static int touch_driver_get_9922c_hwid(struct thp_device *tdev, uint32_t *hwid)
{
	uint32_t ret_hwid;
	int rc;

	thp_log_info("%s: called\n", __func__);
	rc = touch_driver_enter_drw_mode(tdev);
	if (rc) {
		thp_log_err("%s: Enter drw mode failed: rc=%d\n", __func__, rc);
		return rc;
	}

	rc = touch_driver_drw_read_reg(tdev, HW_REG_HARDWARE_ID,
		(uint8_t *)&ret_hwid, sizeof(uint32_t));
	if (rc) {
		thp_log_err("%s: Read @%06x failed: rc=%d\n",
			__func__, HW_REG_HARDWARE_ID, rc);
		return rc;
	}
	thp_log_info("%s: hwid = %#06x@%#6x\n",
		__func__, ret_hwid, HW_REG_HARDWARE_ID);
	*hwid = ret_hwid;

	return rc;
}

/* 9951r get hwid */
static int touch_driver_prog_write_reg(struct thp_device *tdev,
		uint32_t reg_addr, uint8_t *wbuf, size_t wlen)
{
	int rc;

	if (!wlen || (wlen + PROG_SPI_SEND_HEAD_SIZE > THP_MAX_FRAME_SIZE))
		return -EINVAL;

	memset(tdev->tx_buff, 0, PROG_SPI_SEND_HEAD_SIZE + wlen);
	tdev->tx_buff[0] = TOUCH_DRW_WR_CMD;
	put_unaligned_be24(tdev->tx_buff + 1, reg_addr);
	memcpy(tdev->tx_buff + PROG_SPI_SEND_HEAD_SIZE, wbuf, wlen);

	rc = touch_driver_spi_transfer(tdev, tdev->tx_buff, tdev->rx_buff,
		PROG_SPI_SEND_HEAD_SIZE + wlen);
	if (rc) {
		thp_log_err("%s: prog read reg %#07x failed: rc=%d",
			__func__, reg_addr, rc);
		return rc;
	}

	return 0;
}

/* 9951r get hwid */
static int touch_driver_switch_target_ic(struct thp_device *tdev, uint8_t target_id)
{
	int rc;
	int i;

	for (i = 0; i < SWITCH_IC_RETRY_TIMES; i++) {
		thp_log_info("%s: size:%d\n", __func__, sizeof(uint8_t));
		rc = touch_driver_spi_transfer(tdev, &target_id, tdev->rx_buff,
			sizeof(uint8_t));
		if (rc) {
			thp_log_err("%s: spi_sync failed: rc=%d at %d times",
				__func__, rc, i);
			continue;
		}
	}

	if (rc) {
		thp_log_err("%s: switch target ic failed", __func__);
		return rc;
	}

	return rc;
}

/* 9951r get hwid */
static int touch_driver_enter_prog_mode(struct thp_device *tdev)
{
	int rc;
	int retries = ENTER_PROG_MODE_RETRY_TIMES;
	uint8_t magic[] = {0xCC, 0x33, 0x55, 0x5A};

	while (retries--) {
		rc = touch_driver_spi_transfer(tdev, magic, tdev->rx_buff,
			sizeof(magic));
		if (!rc) {
			break;
		}
		mdelay(ENTER_PROG_MODE_RETRY_DELAY);
	}

	if (rc) {
		thp_log_err("%s: enter prog mode failed", __func__);
		return rc;
	}

	return rc;
}

/* 9951r get hwid */
static int touch_driver_prog_read_reg(struct thp_device *tdev,
	uint32_t reg_addr, size_t rlen)
{
	int rc;

	if (!rlen || (rlen + PROG_SPI_RECV_HEAD_SIZE > THP_MAX_FRAME_SIZE))
		return -EINVAL;

	memset(tdev->tx_buff, 0, PROG_SPI_RECV_HEAD_SIZE);
	tdev->tx_buff[0] = TOUCH_DRW_RD_CMD;
	put_unaligned_be24(tdev->tx_buff + 1, reg_addr);

	rc = touch_driver_spi_transfer(tdev,
			tdev->tx_buff, tdev->rx_buff, PROG_SPI_RECV_HEAD_SIZE + rlen);
	if (rc) {
		thp_log_err("%s: prog read reg %#07x failed: rc=%d",
			__func__, reg_addr, rc);
		return rc;
	}

	return rc;
}

/* 9951r get hwid */
static int touch_driver_enter_norm_mode(struct thp_device *tdev)
{
	int rc;
	uint8_t sram_mode = BOOT_MODE_SRAM;

	thp_log_info("%s: enter", __func__);

	rc = touch_driver_prog_write_reg(tdev, HW_REG_BOOT_MODE, &sram_mode, sizeof(uint8_t));
	if (rc) {
		thp_log_err("%s: enter sram boot mode failed: rc=%d", __func__, rc);
		return rc;
	}

	thp_log_info("%s: exit", __func__);

	return rc;
}

/* 9951r get hwid */
static int touch_driver_get_9951r_hwid(struct thp_device *tdev, uint32_t *hwid)
{
	int rc = 0;

	rc = touch_driver_switch_target_ic(tdev, TARGET_IC_MASTER);
	if (rc) {
		thp_log_err("%s: switch target ic failed: rc=%d", __func__, rc);
		return rc;
	}

	rc = touch_driver_enter_prog_mode(tdev);
	if (rc) {
		thp_log_err("%s: enter program mode failed: rc=%d", __func__, rc);
		return rc;
	}

	rc = touch_driver_prog_read_reg(tdev, HW_REG_HARDWARE_ID, sizeof(uint32_t));
	if (rc) {
		thp_log_err("%s: prog read hwid failed: rc=%d", __func__, rc);
		return rc;
	}

	rc = touch_driver_enter_norm_mode(tdev);
	if (rc) {
		thp_log_err("%s: enter norm mode failed: rc=%d", __func__, rc);
		return rc;
	}

	*hwid = get_unaligned_le24(tdev->rx_buff + PROG_SPI_RECV_HEAD_SIZE);

	thp_log_info("%s: hwid = %#06x", __func__, *hwid);

	return rc;
}

/* free tdev and buffers */
static int touch_driver_dev_free(struct thp_device *tdev)
{
	thp_log_info("%s: called\n", __func__);
	if (tdev) {
		kfree(tdev->rx_buff);
		tdev->rx_buff = NULL;
		kfree(tdev->tx_buff);
		tdev->tx_buff = NULL;
		kfree(tdev);
		tdev = NULL;
	}

	return 0;
}

/* malloc tdev and buffers */
struct thp_device *touch_driver_dev_malloc(void)
{
	struct thp_device *tdev = NULL;

	thp_log_info("%s: called\n", __func__);
	tdev = kzalloc(sizeof(struct thp_device), GFP_KERNEL);
	if (!tdev)
		goto err_touch_driver_dev_free;
	tdev->tx_buff = kzalloc(TOUCH_SPI_BUF_SIZE, GFP_KERNEL);
	if (!tdev->tx_buff)
		goto err_touch_driver_dev_free;
	tdev->rx_buff = kzalloc(TOUCH_SPI_BUF_SIZE, GFP_KERNEL);
	if (!tdev->rx_buff)
		goto err_touch_driver_dev_free;

	return tdev;

err_touch_driver_dev_free:
	touch_driver_dev_free(tdev);
	thp_log_err("%s: Malloc thp_device failed\n", __func__);

	return NULL;
}

static int thp_parse_feature_ic_config(struct device_node *thp_node,
	struct thp_core_data *cd)
{
	int rc;

	thp_log_info("%s: called\n", __func__);
	cd->support_vendor_ic_type = 0;
	rc = of_property_read_u32(thp_node, "support_vendor_ic_type",
		&cd->support_vendor_ic_type);
	if (!rc)
		thp_log_info("%s:support_vendor_ic_type parsed:%u\n",
			__func__, cd->support_vendor_ic_type);

	rc = of_property_read_u32(thp_node, "aod_support_on_tddi",
		&cd->aod_support_on_tddi);
	if (!rc)
		thp_log_info("%s: aod_support_on_tddi %u\n",
			__func__, cd->aod_support_on_tddi);

	rc = of_property_read_u32(thp_node, "project_in_tp",
		&cd->project_in_tp);
	if (!rc)
		thp_log_info("%s: project_in_tp %u\n",
			__func__, cd->project_in_tp);

	return rc;
}

/* thp callback, init */
static int touch_driver_init(struct thp_device *tdev)
{
	struct thp_core_data *cd = tdev->thp_core;
	struct device_node *touch_node = NULL;
	int rc;

	thp_log_info("%s: called\n", __func__);
	if (tdev->sdev->master->flags & SPI_MASTER_HALF_DUPLEX) {
		thp_log_err("%s: Full duplex not supported by master\n",
			__func__);
		return -EIO;
	}
	touch_node = of_get_child_by_name(cd->thp_node, TOUCH_DEV_NODE_NAME);
	if (!touch_node) {
		thp_log_err("%s: %s node NOT found in dts\n",
			__func__, TOUCH_DEV_NODE_NAME);
		return -ENODEV;
	}
	rc = thp_parse_spi_config(touch_node, cd);
	if (rc) {
		thp_log_err("%s: Spi config parse failed: rc=%d\n",
			__func__, rc);
		return -EINVAL;
	}
	rc = thp_parse_timing_config(touch_node, &tdev->timing_config);
	if (rc) {
		thp_log_err("%s: Timing config parse failed: rc=%d\n",
			__func__, rc);
		return -EINVAL;
	}

	rc = thp_parse_feature_ic_config(touch_node, cd);
	if (rc) {
		thp_log_err("%s: Feature ic config parse failed: rc=%d\n",
			__func__, rc);
		return -EINVAL;
	}

	return 0;
}

/* thp callback, detect */
static int touch_driver_detect(struct thp_device *tdev)
{
	struct thp_core_data *cd = tdev->thp_core;
	uint32_t hwid = 0;
	int rc = 0;
	int retry;

	thp_log_info("%s: called\n", __func__);
	for (retry = 0; retry < DETECT_RETRY_TIME; retry++) {
		thp_spi_cs_set(GPIO_HIGH);
		touch_hw_reset(tdev);
		if (cd->support_vendor_ic_type == IC_TYPE_ICNL9922C) {
			rc = touch_driver_get_9922c_hwid(tdev, &hwid);
			if (rc) {
				thp_log_err("%s: Get hwid 9922c failed: rc=%d\n",
					__func__, rc);
				thp_time_delay(DETECT_RETRY_DELAY_MS);
				continue;
			}
		} else if (cd->support_vendor_ic_type == IC_TYPE_ICNL9951R) {
			rc = touch_driver_get_9951r_hwid(tdev, &hwid);
			if (rc) {
				thp_log_err("%s: get hwid 9951r failed: rc=%d\n",
					__func__, rc);
				thp_time_delay(DETECT_RETRY_DELAY_MS);
				continue;
			}
		}

		hwid &= HWID_MASK;
		if (hwid != ICNL9922C_HWID && hwid != ICNL9951R_HWID) {
			thp_log_err("%s: Mismatch hwid count:%d,got %#06x\n",
				__func__, retry, hwid);
		} else {
			thp_log_info("%s: detect success, hwid=%06x\n", __func__, hwid);
			return 0;
		}
		mdelay(DETECT_RETRY_DELAY_MS);
	}

	if (tdev->thp_core->fast_booting_solution)
		touch_driver_dev_free(tdev);

	return -EINVAL;
}

/* thp callback, get_frame */
static int touch_driver_get_frame(struct thp_device *tdev,
	char *buf, unsigned int len)
{
	unsigned int copy_len = len < TOUCH_FRAME_SIZE ? len : TOUCH_FRAME_SIZE;
	int rc = 0;

	rc = thp_bus_lock();
	if (rc < 0) {
		thp_log_err("%s:get lock fail\n", __func__);
		return -EINVAL;
	}
	memset(tdev->tx_buff, 0, TOUCH_FRAME_SIZE);
	memset(tdev->rx_buff, 0, TOUCH_FRAME_SIZE);
	memcpy(tdev->tx_buff, get_frame_cmd, sizeof(get_frame_cmd));
	/* tx_buff and rx_buff is for irq use only */
	rc = touch_driver_spi_transfer(tdev, tdev->tx_buff,
		tdev->rx_buff, TOUCH_FRAME_SIZE);
	if (rc) {
		thp_log_err("%s: Get data frame failed: rc=%d\n", __func__, rc);
		thp_bus_unlock();
		return -EIO;
	}
	memcpy(buf, tdev->rx_buff, copy_len);
	thp_bus_unlock();

	if (len > TOUCH_FRAME_SIZE)
		memset(buf + TOUCH_FRAME_SIZE, 0, len - TOUCH_FRAME_SIZE);

	return rc;
}

/* thp callback, resume */
static int touch_driver_resume(struct thp_device *tdev)
{
	thp_log_info("%s: called\n", __func__);
	if ((!tdev) || (!tdev->thp_core) || (!tdev->thp_core->sdev)) {
		thp_log_err("%s: Invalid tdev\n", __func__);
		return -EINVAL;
	}

	gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);
	touch_hw_reset(tdev);
	return 0;
}

/* Set gesture mode command,three commands need to be set consecutively */
static int touch_driver_set_gesture_mode(struct thp_device *tdev,
	uint8_t *cmd, size_t cmd_len)
{
	int rc;

	thp_log_info("%s: called\n", __func__);
	/* Data format for notifying the IC of preparing to send a gesture */
	rc = thp_bus_lock();
	if (rc < 0) {
		thp_log_err("%s:get lock fail\n", __func__);
		return -EINVAL;
	}
	rc = touch_dual_protocols_spi_transfer(tdev, gesture_format_cmd,
		sizeof(gesture_format_cmd));
	if (rc) {
		thp_log_err("%s:Send gesture format cmd failed: rc=%d\n", __func__, rc);
		thp_bus_unlock();
		return -EIO;
	}

	/* Send current gesture mode command */
	rc = touch_dual_protocols_spi_transfer(tdev, cmd, cmd_len);
	if (rc) {
		thp_log_err("%s:Send supported gesture mode cmd failed: rc=%d\n",
			__func__, rc);
		thp_bus_unlock();
		return -EIO;
	}

	/* Toggle gesture command mode (IC scans in gesture command mode) */
	rc = touch_dual_protocols_spi_transfer(tdev,
		gesture_cmd, sizeof(gesture_cmd));
	if (rc) {
		thp_log_err("%s:Enter gesutre mode failed: rc=%d\n", __func__, rc);
		thp_bus_unlock();
		return -EIO;
	}
	thp_bus_unlock();

	return 0;
}

/* 9922c Set gesture cmd */
static int touch_driver_set_gesture_cmd(uint8_t **cmd,
	size_t *cmd_len, uint16_t param)
{
	switch (param) {
	case SINGLE_CLICK:
		*cmd = single_click_cmd;
		*cmd_len = sizeof(single_click_cmd);
		break;
	case DOUBLE_CLICK:
		*cmd = double_click_cmd;
		*cmd_len = sizeof(double_click_cmd);
		break;
	case BOTH_CLICK:
		*cmd = both_click_cmd;
		*cmd_len = sizeof(both_click_cmd);
		break;
	default:
		thp_log_err("%s:BUG Invalid gesture setting: %#06x\n", __func__, param);
		return -EINVAL;
	}

	return 0;
}

/* 9922c Enter gesture mode */
static int touch_enter_9922c_gesture_mode(struct thp_device *tdev, uint16_t param)
{
	uint8_t *cmd = NULL;
	size_t cmd_len = 0;
	int rc;

	thp_log_info("%s: called\n", __func__);
	rc = touch_driver_set_gesture_cmd(&cmd, &cmd_len, param);
	if (rc) {
		thp_log_err("%s:set gesture cmd failed: rc=%d\n",
			__func__, rc);
		return -EIO;
	}
	rc = touch_driver_set_gesture_mode(tdev, cmd, cmd_len);
	if (rc) {
		thp_log_err("%s:Send supported gesture mode cmd failed: rc=%d\n",
			__func__, rc);
		return -EIO;
	}

	return 0;
}

/* 9951r enter gesture mode */
static int touch_check_enter_gesture(struct thp_device *tdev)
{
	static uint8_t enter_gesture_mode[] = {
		TOUCH_NORM_WR_CMD, 0x04, 0x22, 0x10, 0x00, 0x28, 0x18,
		0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x0a, 0x0a
	};
	int rc = 0;
	uint16_t crc16_calc;
	uint16_t crc16_recv;
	uint8_t errcode;

	memcpy(tdev->tx_buff, enter_gesture_mode, sizeof(enter_gesture_mode));
	rc = touch_driver_spi_transfer(tdev, tdev->tx_buff, NULL,
		sizeof(enter_gesture_mode));
	if (rc) {
		thp_log_err("%s: write failed: rc=%d", __func__, rc);
		return -EIO;
	}
	rc = touch_driver_spi_transfer(tdev, NULL, tdev->rx_buff,
		OFFSET_3 + sizeof(crc16_recv));
	if (rc) {
		thp_log_err("%s: read failed: rc=%d", __func__, rc);
		return -EIO;
	}

	errcode = tdev->rx_buff[0];
	if (errcode != TCS_ERR_CODE_OK) {
		thp_log_err("%s: errcode=%d", __func__, errcode);
		return -EINVAL;
	}
	if (tdev->rx_buff[1] != 0x04 && tdev->rx_buff[2] != 0x22) {
		thp_log_err("%s: cmd mismatch %d,%d", __func__,
			tdev->rx_buff[1], tdev->rx_buff[2]);
		return -EINVAL;
	}
	crc16_recv = tdev->rx_buff[3] | (tdev->rx_buff[4] << MOVE_8BIT);
	crc16_calc = touch_driver_crc16(tdev->rx_buff, OFFSET_3);
	if (crc16_calc != crc16_recv) {
		thp_log_err("%s: crc mismatch: calc %04x != %04x recv", __func__,
			crc16_calc, crc16_recv);
		return -EINVAL;
	}

	return rc;
}

/* 9951r enter gesture mode */
static int touch_enter_9951r_gesture_mode(struct thp_device *tdev)
{
	int rc = 0;

	rc = touch_check_enter_gesture(tdev);
	if (rc) {
		thp_log_err("%s: enter gesture mode failed: rc=%d",
			__func__, rc);
		return rc;
	}

	mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
	tdev->thp_core->easy_wakeup_info.off_motion_on = true;
	mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);

	return rc;
}

/* enter gesture mode */
static int touch_driver_enter_gesture_mode(struct thp_device *tdev, uint16_t param)
{
	int rc;
	struct thp_core_data *cd = NULL;

	cd = tdev->thp_core;
	thp_log_info("%s: TS_GESTURE_MODE, cs_gpio :%d\n", __func__,
		tdev->gpios->cs_gpio);
	if (cd->support_vendor_ic_type == IC_TYPE_ICNL9922C) {
		rc = touch_enter_9922c_gesture_mode(tdev, param);
		if (rc) {
			thp_log_err("%s:Enter gesture mode failed: rc=%d\n", __func__, rc);
			return rc;
		}
	} else if (cd->support_vendor_ic_type == IC_TYPE_ICNL9951R) {
		rc = touch_enter_9951r_gesture_mode(tdev);
		if (rc) {
			thp_log_err("%s:Enter gesture mode failed: rc=%d\n", __func__, rc);
			return rc;
		}
	} else {
		thp_log_err("%s:Unexpected mode\n", __func__);
		rc = -EINVAL;
	}

	return rc;
}

static int touch_driver_enter_sleep_mode(struct thp_device *tdev)
{
	int rc;

	rc = thp_bus_lock();
	if (rc < 0) {
		thp_log_err("%s:get lock fail\n", __func__);
		return -EINVAL;
	}
	rc = touch_dual_protocols_spi_transfer(tdev, sleep_cmd, sizeof(sleep_cmd));
	if (rc)
		thp_log_err("%s:Set sleep mode failed: rc=%d\n", __func__, rc);
	thp_bus_unlock();

	return rc;
}

/* set gesture param */
static void touch_driver_set_param(uint16_t *param,
	struct thp_device *tdev)
{
	struct thp_core_data *cd = tdev->thp_core;

	if (cd->aod_touch_status)
		*param |= ENABLE_SINGLE_CLICK;
	if (cd->sleep_mode == TS_GESTURE_MODE)
		*param |= ENABLE_DOUBLE_CLICK;
}

/* thp callback, suspend */
static int touch_driver_suspend(struct thp_device *tdev)
{
	uint16_t param = 0x00000;

	thp_log_info("%s: called\n", __func__);
	if ((!tdev) || (!tdev->thp_core) || (!tdev->thp_core->sdev)) {
		thp_log_err("%s: Invalid tdev\n", __func__);
		return -EINVAL;
	}

	touch_driver_set_param(&param, tdev);
	if (is_pt_test_mode(tdev) || !param) {
		return touch_driver_enter_sleep_mode(tdev);
	} else {
		return touch_driver_enter_gesture_mode(tdev, param);
	}
}

/* thp callback, exit */
static void touch_driver_exit(struct thp_device *tdev)
{
	thp_log_info("%s: called\n", __func__);
	touch_driver_dev_free(tdev);
}

#if defined(CONFIG_HUAWEI_THP_QCOM)
/* check gesture,  NOTICE: thp bus is locked */
static int touch_driver_check_gesture(uint8_t *gesture,
	uint32_t gesture_size)
{
	struct touch_cspi_tail *tail =
		(struct touch_cspi_tail *) (gesture + gesture_size);
	uint16_t crc16_calc;
	uint16_t crc16_recv;
	size_t crc_offset = offsetof(struct touch_cspi_tail, crc);

	thp_log_info("%s: called\n", __func__);
	crc16_calc = touch_driver_crc16(gesture, (gesture_size + crc_offset));
	crc16_recv = (gesture[gesture_size + OFFSET_4] << MOVE_8BIT) |
		gesture[gesture_size + OFFSET_3];
	if (crc16_calc != crc16_recv) {
		thp_log_err("%s:Crc error: calc %#06x != %#06x recv\n",
			__func__, crc16_calc, crc16_recv);
		return -EINVAL;
	}
	if ((tail->cmd[0] != get_gesture_cmd[1]) ||
		(tail->cmd[1] != get_gesture_cmd[2])) {
		thp_log_err("%s:Mismatched reply! cmd[2] = %02x,%02x\n",
			__func__, tail->cmd[0], tail->cmd[1]);
		return -EINVAL;
	}

	return 0;
}

/* thp callback, parse_event_info */
static int touch_driver_parse_event_info(struct thp_device *tdev,
	struct thp_udfp_data *udfp_data)
{
	unsigned int read_len = TOUCH_GSTR_SIZE;
	int rc;

	thp_log_info("%s: called\n", __func__);
	rc = thp_bus_lock();
	if (rc < 0) {
		thp_log_err("%s:get lock fail\n", __func__);
		return -EINVAL;
	}
	memcpy(tdev->tx_buff, get_gesture_cmd, sizeof(get_gesture_cmd));
	/* tx_buff and rx_buff is for irq use only */
	rc = touch_driver_spi_transfer(tdev, tdev->tx_buff, tdev->rx_buff,
		read_len + sizeof(struct touch_cspi_tail));
	if (rc) {
		thp_log_err("%s:Get gesture failed: rc=%d\n", __func__, rc);
		goto get_gesture_info_failed;
	}
	rc = touch_driver_check_gesture(tdev->rx_buff, read_len);
	if (rc) {
		thp_log_err("%s:Check gesture failed\n", __func__);
		goto get_gesture_info_failed;
	}
	memcpy(udfp_data, tdev->rx_buff, sizeof(struct thp_udfp_data));

get_gesture_info_failed:
	/*
	 * Indicates the ready flag of the clear gesture. This cspi must be issued
	 * to clear the ready flag of the data regardless of whether the SPI
	 * transmission is successful
	 */
	rc = touch_dual_protocols_spi_transfer(tdev, gesture_confirm_cmd,
		sizeof(gesture_confirm_cmd));
	if (rc)
		thp_log_err("%s:Clear gesture status failed: rc=%d\n", __func__, rc);
	thp_bus_unlock();

	return rc;
}
#endif

/* 9951r gesture check */
static int touch_driver_gesture_check(struct thp_device *tdev)
{
	uint16_t crc16_calc;
	uint16_t crc16_recv;
	uint8_t errcode;
	int rc = 0;

	errcode = tdev->rx_buff[GESTURE_INFO_LEN];
	if (errcode != TCS_ERR_CODE_OK) {
		thp_log_err("%s: errcode=%d", __func__, errcode);
		rc = -EINVAL;
		return rc;
	}
	if (tdev->rx_buff[GESTURE_INFO_LEN + OFFSET_1] != 0x03 &&
		tdev->rx_buff[GESTURE_INFO_LEN + OFFSET_2] != 0x41) {
			thp_log_err("%s: cmd mismatch %d,%d", __func__,
				tdev->rx_buff[GESTURE_INFO_LEN + OFFSET_1],
				tdev->rx_buff[GESTURE_INFO_LEN + OFFSET_2]);
		rc = -EINVAL;
		return rc;
	}
	crc16_recv = tdev->rx_buff[GESTURE_INFO_LEN + OFFSET_3] |
			(tdev->rx_buff[GESTURE_INFO_LEN + OFFSET_4] << MOVE_8BIT);
	crc16_calc = touch_driver_crc16(tdev->rx_buff, GESTURE_INFO_LEN + OFFSET_3);
	if (crc16_calc != crc16_recv) {
		thp_log_err("%s: crc mismatch: calc %04x != %04x recv", __func__,
			crc16_calc, crc16_recv);
		rc = -EINVAL;
		return rc;
	}

	return rc;
}

/* 9951r gesture report */
static int touch_driver_gesture_report(struct thp_device *tdev,
	unsigned int *gesture_wakeup_value)
{
	int rc = 0;

	static uint8_t get_gesture_cmd[] = {
		TOUCH_NORM_RD_CMD, 0x03, 0x41, 0x70, 0x00, 0xe9, 0xb3
	};

	thp_log_info("%s, gesture trigger\n", __func__);
	mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
	if (tdev->thp_core->easy_wakeup_info.off_motion_on == true) {
		memcpy(tdev->tx_buff, get_gesture_cmd, sizeof(get_gesture_cmd));
		rc = touch_driver_spi_transfer(tdev, tdev->tx_buff, tdev->rx_buff,
			GESTURE_INFO_LEN + OFFSET_5);
		if (rc) {
			thp_log_err("%s: write failed: rc=%d", __func__, rc);
			rc = -EIO;
			goto err_unlock;
		}

		rc = touch_driver_gesture_check(tdev);
		if (rc) {
			thp_log_err("%s: gesture check failed: rc=%d", __func__, rc);
			rc = -EIO;
			goto err_unlock;
		}

		if (tdev->rx_buff[0] == GESTURE_D_TAP_VAL) {
			*gesture_wakeup_value = TS_DOUBLE_CLICK;
			thp_log_info("%s, double click report\n", __func__);
			rc = 0;
			goto err_enter_gesture;
		} else {
			thp_log_info("%s, unknown gesture id %#04x",
				__func__, tdev->rx_buff[0]);
			rc = -EINVAL;
			goto err_enter_gesture;
		}
	}

err_enter_gesture:
	touch_check_enter_gesture(tdev);
	tdev->thp_core->easy_wakeup_info.off_motion_on = true;
err_unlock:
	mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);

	return rc;
}

/* thp ops */
static struct thp_device_ops touch_dev_ops = {
	.init = touch_driver_init,
	.detect = touch_driver_detect,
	.get_frame = touch_driver_get_frame,
	.resume = touch_driver_resume,
	.suspend = touch_driver_suspend,
	.exit = touch_driver_exit,
	.chip_gesture_report = touch_driver_gesture_report,
#if defined(CONFIG_HUAWEI_THP_QCOM)
	.get_event_info = touch_driver_parse_event_info,
#endif
};

/* module init */
static int __init touch_driver_module_init(void)
{
	struct thp_device *tdev = NULL;
	int rc;
	struct thp_core_data *cd = thp_get_core_data();

	thp_log_info("%s: THP dirver " TOUCH_DRV_VERSION " for IC", __func__);
	tdev = touch_driver_dev_malloc();
	if (!tdev) {
		rc = -ENOMEM;
		thp_log_err("%s: Malloc for thp device failed: rc=%d\n",
			__func__, rc);
		return rc;
	}
	tdev->ic_name = TOUCH_IC_NAME;
	tdev->dev_node_name = TOUCH_DEV_NODE_NAME;
	tdev->ops = &touch_dev_ops;
	if (cd && cd->fast_booting_solution) {
		thp_send_detect_cmd(tdev, NO_SYNC_TIMEOUT);
		/*
		 * The thp_register_dev will be called later to complete
		 * the real detect action.If it fails, the detect function will
		 * release the resources requested here.
		 */
		return 0;
	}
	rc = thp_register_dev(tdev);
	if (rc) {
		rc = -EFAULT;
		thp_log_err("%s: Register thp device failed: rc=%d\n",
			__func__, rc);
		touch_driver_dev_free(tdev);
		return rc;
	}
	thp_log_info("%s: THP dirver registered\n", __func__);

	return 0;
}

/* module exit */
static void __exit touch_driver_module_exit(void)
{
	thp_log_info("%s: called, do nothing", __func__);
}

#ifdef CONFIG_HUAWEI_THP_QCOM
late_initcall(touch_driver_module_init);
#else
module_init(touch_driver_module_init);
#endif
module_exit(touch_driver_module_exit);
MODULE_AUTHOR("huawei");
MODULE_DESCRIPTION("huawei driver");
MODULE_LICENSE("GPL");
