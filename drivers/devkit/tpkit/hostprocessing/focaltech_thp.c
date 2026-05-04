/*
 * Thp driver code for focaltech
 *
 * Copyright (c) 2012-2020 Huawei Technologies Co., Ltd.
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
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include "huawei_thp.h"

#define FOCALTECH_IC_NAME "focaltech"
#define THP_FOCALTECH_DEV_NODE_NAME "focaltech"

#define DATA_HEAD 0x3F
#define DATA_MASK 0xFF
#define COMOND_HEAD 0xC0
#define COMOND_READ_ONE 0x81
#define COMOND_WRITE_ONE 0x01
#define COMOND_ADDR_RESET 0x55
#define COMMOND_READ_DATA 0x3A
#define COMOND_ADDR_DETECT 0x90
#define CHIP_DETECT_FAILE_ONE 0x00
#define CHIP_DETECT_FAILE_TWO 0xFF
#define CHIP_DETECT_FAILE_THR 0xEF
#define CHIP_DETECT_RETRY_NUMB 3
#define DELAY_AFTER_FIRST_BYTE 30

#define POWER_CTRL_DELAY 2
#define DOUBLE_TAP_FLAG 2
#define READ_LEN 4
#define FTS_IC_NAME_LEN 2
#define FTS_PRODUCT_NAME_LEN 4
#define FTS_READ_CMD 0xA0
#define FTS_WRITE_CMD 0x00
#define SPI_DUMMY_BYTE 0x07
#define DATA_CRC_EN 0x20
#define CRC_REG_VALUE 0xFFFF
#define CRC_CHECK_BIT_SIZE 8
#define CRC_CHECK_CODE 0x8408
#define CRC_BUF_MIN_SIZE 2
#define CRC_LOW_BIT_CHECK 0x01
#define CRC_BIT_CHECK 1
#define ADDR_OFFSET 0x02
#define ADDR_RAWDATA 0x01
#define ADDR_FRAME 0x3A
#define DELAY8_AFTER_RST 8
#define GET_HIGH_BIT 8
#define SPI_RETRY_NUMBER 3
#define CS_HIGH_DELAY 150
#define FTS_REG_GESTURE_EN 0xD0
#define FTS_REG_GESTURE 0xD1
#define FTS_REG_GESTURE_DATA 0xD3
#define FTS_REG_LEN 1
#define FTS_OFF_STS 3
#define FTS_GESTURE_DATA_LEN 30
#define FTS_GESTURE_STR_LEN_MAX 152
#define FTS_GESTURE_ID_BIT 2
#define FTS_GESTURE_ID_VALUE 0x24
#define GESTURE_DATA_ENABLE 1
#define GESTURE_INFO_BIT 3
#define GESTURE_INFO_VALUE 0x80
#define SLEEP_RETRY_NUM 3
#define SLEEP_BUF_BIT 3
#define WAIT_FOR_SPI_BUS_READ_DELAY 5
#define UDFP_INFO_SIZE 17
#if (defined(CONFIG_HUAWEI_THP_MTK) || defined(CONFIG_HUAWEI_THP_QCOM))
static struct udfp_mode_status ud_mode_status;
#define FTS_UDFP_DATA_LEN (17 * 4)
#endif
/*
 * Here we use "fts_ic_type" to identify focal ICs' types:
 * 1.FT8615_SERIES includes ft8615 and ft8719,
 * and "support_vendor_ic_type" is 0 in dts.
 * 2.FT8720_SERIES includes ft8720 and ft8009,
 * but ft8009 has some differences in ecc and get_frame command,
 * and this series should config "support_vendor_ic_type" to 1.
 * 3.The future focal ic almost belongs to FT8720_SERIES,
 * so we have to config the "support_vendor_ic_type" to 1 in dts.
 */
enum fts_ic_type {
	FT8615_SERIES,
	FT8720_SERIES = 1,
};

static bool is_ic_ft8009;

/* spi interface */
static int touch_driver_spi_transfer(const char *tx_buf,
	char *rx_buf, unsigned int len)
{
	int rc;
	const int index = 2;
	struct thp_core_data *cd = thp_get_core_data();
	struct spi_device *sdev = cd->sdev;
	struct spi_message msg;
	struct spi_transfer xfer1 = {
		.tx_buf = tx_buf,
		.rx_buf = rx_buf,
		.len = len,
	};
	struct spi_transfer xfer[index];
	unsigned int finger_status = !!(thp_get_status(THP_STATUS_UDFP));
	unsigned int gesture_status = cd->easy_wakeup_info.sleep_mode;
	bool no_need_work_in_suspend = cd->suspended && (!cd->need_work_in_suspend)
		&& !(cd->support_gesture_mode && ((gesture_status == TS_GESTURE_MODE)
			|| finger_status || cd->aod_touch_status));

	if (no_need_work_in_suspend)
		return 0;
	if (cd->support_vendor_ic_type == FT8720_SERIES) {
		spi_message_init(&msg);
		spi_message_add_tail(&xfer1, &msg);
	} else {
		memset(xfer, 0, sizeof(xfer));

		spi_message_init(&msg);
		xfer[0].tx_buf = &tx_buf[0];
		if (rx_buf)
			xfer[0].rx_buf = &rx_buf[0];
		xfer[0].len = 1;
		xfer[0].delay_usecs = DELAY_AFTER_FIRST_BYTE;
		spi_message_add_tail(&xfer[0], &msg);
		if (len > 1) {
			xfer[1].tx_buf = &tx_buf[1];
			if (rx_buf)
				xfer[1].rx_buf = &rx_buf[1];
			xfer[1].len = len - 1;
			spi_message_add_tail(&xfer[1], &msg);
		}
	}

	mutex_lock(&cd->spi_mutex);
	if (!cd->not_support_cs_control)
		thp_spi_cs_set(GPIO_HIGH);
	rc = spi_sync(sdev, &msg);
	mutex_unlock(&cd->spi_mutex);

	return rc;
}

static int touch_driver_hardware_reset_to_rom(struct thp_device *tdev)
{
	int rc;
	int index = 0;
	unsigned char cmd[3]; /* 3 byte once */

	if (!tdev) {
		thp_log_info("%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	/* reset */
	gpio_direction_output(tdev->gpios->rst_gpio, THP_RESET_LOW);
	thp_do_time_delay(tdev->timing_config.boot_reset_low_delay_ms);

	gpio_set_value(tdev->gpios->rst_gpio, THP_RESET_HIGH);
	thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);

	mdelay(8); /* 8ms sequence delay */

	cmd[index++] = COMOND_HEAD;
	cmd[index++] = COMOND_WRITE_ONE;
	cmd[index++] = COMOND_ADDR_RESET;
	rc = touch_driver_spi_transfer(cmd, cmd, ARRAY_SIZE(cmd));
	if (rc) {
		thp_log_err("%s:write 0x55 command fail\n", __func__);
		return rc;
	}

	mdelay(8); /* 8ms sequence delay */
	return 0;
}

static int touch_driver_hardware_reset_to_rom_new(struct thp_device *tdev)
{
	int rc;
	int index = 0;
	unsigned char cmd[4] = { 0x0 }; /* 4 byte once */

	/* reset */
#ifndef CONFIG_HUAWEI_THP_MTK
	gpio_direction_output(tdev->gpios->rst_gpio, THP_RESET_LOW);
	thp_do_time_delay(tdev->timing_config.boot_reset_low_delay_ms);
	gpio_set_value(tdev->gpios->rst_gpio, THP_RESET_HIGH);
	thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);
#else
	if (tdev->thp_core->support_pinctrl == 0) {
		thp_log_info("%s: not support pinctrl\n", __func__);
		return -EINVAL;
	}
	pinctrl_select_state(tdev->thp_core->pctrl,
		tdev->thp_core->mtk_pinctrl.reset_low);
	thp_do_time_delay(tdev->timing_config.boot_reset_low_delay_ms);
	pinctrl_select_state(tdev->thp_core->pctrl,
		tdev->thp_core->mtk_pinctrl.reset_high);
	thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);
#endif
	mdelay(DELAY8_AFTER_RST);

	cmd[index++] = COMOND_ADDR_RESET;
	cmd[index++] = FTS_WRITE_CMD;
	cmd[index++] = 0x00; /* high bit */
	cmd[index++] = 0x00; /* low bit */
	rc = touch_driver_spi_transfer(cmd, cmd, ARRAY_SIZE(cmd));
	if (rc) {
		thp_log_err("%s:write 0x55 command fail\n", __func__);
		return rc;
	}
	mdelay(DELAY8_AFTER_RST);
	return 0;
}

static u8 cal_ecc_xor8(u8 *buf, u16 buflen)
{
	u16 i;
	u8 result = 0;

	for (i = 0; i < buflen; i++)
		result ^= buf[i];

	return result;
}

static int rdata_crc_check(u8 *rdata, u16 rlen)
{
	u16 i;
	u16 j;
	u16 crc_read;
	u16 crc_calc = CRC_REG_VALUE;

	if (rdata == NULL) {
		thp_log_err("%s: rdata is NULL\n", __func__);
		return -EIO;
	}
	if (rlen < CRC_BUF_MIN_SIZE) {
		thp_log_err("%s: thp_check_frame_valid buf less than 2: %d\n",
			__func__, rlen);
		return -EIO;
	}
	for (i = 0; i < (rlen - CRC_BUF_MIN_SIZE); i++) {
		crc_calc ^= rdata[i];
		for (j = 0; j < CRC_CHECK_BIT_SIZE; j++) {
			if (crc_calc & CRC_LOW_BIT_CHECK)
				crc_calc = (crc_calc >> CRC_BIT_CHECK) ^
					CRC_CHECK_CODE;
			else
				crc_calc = (crc_calc >> CRC_BIT_CHECK);
		}
	}
	/* rlen - 1 is to get the last element of rdata array */
	crc_read = (u16)(rdata[rlen - 1] << CRC_CHECK_BIT_SIZE) +
		rdata[rlen - CRC_BUF_MIN_SIZE];
	if (crc_calc != crc_read) {
		thp_log_err("%s: ecc fail:cal %x != buf %x\n",
			__func__, crc_calc, crc_read);
		return -EIO;
	}
	return 0;
}

static void touch_driver_write_reg(u8 addr, u8 value)
{
	int i;
	int ret;
	unsigned char w_buf[SPI_DUMMY_BYTE + FTS_REG_LEN];
	unsigned char r_buf[SPI_DUMMY_BYTE + FTS_REG_LEN];
	u32 index = 0;
	u32 txlen;

	thp_log_info("%s: called\n", __func__);
	w_buf[index++] = addr;
	w_buf[index++] = FTS_WRITE_CMD;
	w_buf[index++] = (FTS_REG_LEN >> GET_HIGH_BIT) & 0xFF;
	w_buf[index++] = FTS_REG_LEN & 0xFF;
	txlen = SPI_DUMMY_BYTE + FTS_REG_LEN;
	w_buf[SPI_DUMMY_BYTE] = value;
	for (i = 0; i < SPI_RETRY_NUMBER; i++) {
		ret = touch_driver_spi_transfer(w_buf, r_buf, txlen);
		if ((ret == 0) && ((r_buf[FTS_OFF_STS] & FTS_READ_CMD) == 0))
			break;
		thp_log_info("data write addr:%x, status:%x, retry:%d, ret:%d",
			FTS_REG_GESTURE_EN, r_buf[FTS_OFF_STS], i, ret);
		ret = -EIO;
		udelay(CS_HIGH_DELAY);
	}
}

static int touch_driver_check_frame_valid(u8 *buf, u16 len)
{
	u16 ecc_buf;
	u16 ecc_cal;
	struct thp_core_data *cd = thp_get_core_data();

	if ((cd->support_vendor_ic_type == FT8720_SERIES) && (!is_ic_ft8009))
		return rdata_crc_check(buf, len + ADDR_OFFSET);
	if (len < 2) {
		thp_log_err("%s: buf len < 2 %d\n", __func__, len);
		return -EIO;
	}
	ecc_cal = (u16)cal_ecc_xor8(buf, len - 2);
	ecc_buf = (u16)((buf[len - 2] << 8) + buf[len - 1]);
	if (ecc_cal != ecc_buf) {
		thp_log_err("ecc fail:cal(%x) != buf(%x)\n", ecc_cal, ecc_buf);
		return -EIO;
	}

	return 0;
}

static void touch_driver_projectid_to_ic_type(struct thp_core_data *cd)
{
	char temp_buf[FTS_IC_NAME_LEN + 1] = {0};

	strncpy(temp_buf, cd->project_id + FTS_PRODUCT_NAME_LEN,
		FTS_IC_NAME_LEN);
	if (!strncmp("9D", temp_buf, FTS_IC_NAME_LEN)) {
		is_ic_ft8009 = true;
		cd->support_vendor_ic_type = FT8720_SERIES;
		cd->spi_config.mode = 0;
		cd->sdev->mode = 0;
		thp_log_info("%s: ic is 9D, spi reconfiged\n", __func__);
	}
}

static int thp_parse_extra_config(struct device_node *thp_node,
	struct thp_core_data *cd)
{
	int rc;
	unsigned int value = 0;

	if (!thp_node || !cd) {
		thp_log_info("%s: input null\n", __func__);
		return -ENODEV;
	}
	rc = of_property_read_u32(thp_node, "reset_status_in_suspend_mode",
		&value);
	if (!rc) {
		cd->reset_status_in_suspend_mode = value;
		thp_log_info("%s: reset_status_in_suspend_mode %u\n",
			__func__, value);
	}
	rc = of_property_read_u32(thp_node, "need_set_reset_gpio_high",
		&value);
	if (!rc) {
		cd->need_set_reset_gpio_high = value;
		thp_log_info("%s: need_set_reset_gpio_high %u\n",
			__func__, value);
	}
	return 0;
}

static int touch_driver_power_init(void)
{
	int ret = thp_power_supply_get(THP_IOVDD);
	if (ret)
		thp_log_err("%s: fail to get vcc power\n", __func__);
	ret = thp_power_supply_get(THP_VCC);
	if (ret)
		thp_log_err("%s: fail to get power\n", __func__);
	return 0;
}

static int touch_driver_power_release(void)
{
	int ret = thp_power_supply_put(THP_VCC);
	if (ret)
		thp_log_err("%s: fail to release vcc power\n", __func__);
	ret = thp_power_supply_put(THP_IOVDD);
	if (ret)
		thp_log_err("%s: fail to release power\n", __func__);
	return ret;
}

static int touch_driver_power_on(struct thp_device *tdev)
{
	int ret;
	struct thp_core_data *cd = thp_get_core_data();

	thp_log_info("%s:called\n", __func__);
	gpio_direction_output(tdev->gpios->rst_gpio, GPIO_LOW);
	if (!cd->not_support_cs_control)
		gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);
	ret = thp_power_supply_ctrl(THP_IOVDD, THP_POWER_ON,
		POWER_CTRL_DELAY);
	if (ret)
		thp_log_err("%s:power ctrl iovdd fail\n", __func__);
	ret = thp_power_supply_ctrl(THP_VCC, THP_POWER_ON,
		POWER_CTRL_DELAY);
	if (ret)
		thp_log_err("%s:power ctrl vcc fail\n", __func__);
	if (!cd->not_support_cs_control)
		gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);
#ifdef CONFIG_HUAWEI_THP_QCOM
	if (cd->support_control_cs_off &&
			(!IS_ERR_OR_NULL(cd->qcom_pinctrl.cs_high))) {
		pinctrl_select_state(tdev->thp_core->pctrl,
			tdev->thp_core->qcom_pinctrl.cs_high);
		thp_log_info("%s: cs to high\n", __func__);
	}
#endif
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);
	return ret;
}

static int touch_driver_power_off(struct thp_device *tdev)
{
	int ret;
	struct thp_core_data *cd = thp_get_core_data();

	gpio_set_value(tdev->gpios->rst_gpio, GPIO_LOW);
	thp_do_time_delay(tdev->timing_config.suspend_reset_after_delay_ms);
	ret = thp_power_supply_ctrl(THP_VCC, THP_POWER_OFF,
		POWER_CTRL_DELAY);
	if (ret)
		thp_log_err("%s:power ctrl vcc fail\n", __func__);
	ret = thp_power_supply_ctrl(THP_IOVDD, THP_POWER_OFF,
		POWER_CTRL_DELAY);
	if (ret)
		thp_log_err("%s:power ctrl fail\n", __func__);
	if (!cd->not_support_cs_control)
		gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);
#ifdef CONFIG_HUAWEI_THP_QCOM
	if (cd->support_control_cs_off &&
		(!IS_ERR_OR_NULL(cd->qcom_pinctrl.cs_low))) {
		pinctrl_select_state(tdev->thp_core->pctrl,
			tdev->thp_core->qcom_pinctrl.cs_low);
		thp_log_info("%s:cs to low\n", __func__);
	}
#endif
	return ret;
}

static int touch_driver_init(struct thp_device *tdev)
{
	struct thp_core_data *cd = NULL;
	struct device_node *fts_node = NULL;

	thp_log_info("%s: called\n", __func__);
	if (!tdev) {
		thp_log_info("%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	cd = tdev->thp_core;
	fts_node = of_get_child_by_name(cd->thp_node,
						THP_FOCALTECH_DEV_NODE_NAME);
	if (!fts_node) {
		thp_log_info("%s: dev not config in dts\n",
				__func__);
		return -ENODEV;
	}

	if (thp_parse_spi_config(fts_node, cd))
		thp_log_err("%s: spi config parse fail\n",
			__func__);

	if (thp_parse_timing_config(fts_node, &tdev->timing_config))
		thp_log_err("%s: timing config parse fail\n",
			__func__);

	if (thp_parse_feature_config(fts_node, cd))
		thp_log_err("%s: feature_config fail\n", __func__);
	if (thp_parse_extra_config(fts_node, cd))
		thp_log_err("%s: extra_config fail\n", __func__);

	touch_driver_projectid_to_ic_type(cd);

	return 0;
}

static int touch_driver_chip_detect(struct thp_device *tdev)
{
	int rc;
	int i;
	unsigned char cmd[3]; /* 3 bytes once */
	/* 20 bytes for spi transfer */
	unsigned char cmd1[20] = {0};
	u32 txlen = 0;

	if (!tdev) {
		thp_log_info("%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	if (tdev->thp_core->self_control_power) {
		touch_driver_power_init();
		touch_driver_power_on(tdev);
	}
	for (i = 0; i < CHIP_DETECT_RETRY_NUMB; i++) {
		if (tdev->thp_core->support_vendor_ic_type == FT8720_SERIES) {
			rc = touch_driver_hardware_reset_to_rom_new(tdev);
			if (rc) {
				thp_log_err("%s:write 0x55 command fail\n",
					__func__);
				continue;
			}

			cmd1[txlen++] = COMOND_ADDR_DETECT;
			cmd1[txlen++] = FTS_READ_CMD;
			cmd1[txlen++] = 0x00; /* high bit */
			cmd1[txlen++] = 0x02; /* low bit */
			txlen = SPI_DUMMY_BYTE + ADDR_OFFSET;
			if (FTS_READ_CMD & DATA_CRC_EN)
				txlen = txlen + ADDR_OFFSET;

			rc = touch_driver_spi_transfer(cmd1, cmd1, txlen);
			if (rc) {
				thp_log_err("%s:write 0x90 command fail\n",
					__func__);
				return rc;
			}

			if ((cmd1[SPI_DUMMY_BYTE] == CHIP_DETECT_FAILE_ONE) ||
			    (cmd1[SPI_DUMMY_BYTE] == CHIP_DETECT_FAILE_TWO) ||
			    (cmd1[SPI_DUMMY_BYTE] == CHIP_DETECT_FAILE_THR)) {
				thp_log_err(
					"%s:chip id read fail, ret=%d, i=%d, data=%x\n",
					__func__, rc, i, cmd1[SPI_DUMMY_BYTE]);
			} else {
				thp_log_info(
					"%s:chip id read success, chip id:0x%X,0x%X\n",
					__func__, cmd1[SPI_DUMMY_BYTE],
					cmd1[SPI_DUMMY_BYTE + 1]);
				return 0;
			}
			/* Delay 50ms to retry if reading ic id failed */
			msleep(50);
		} else {
			rc = touch_driver_hardware_reset_to_rom(tdev);
			if (rc) {
				thp_log_err("%s:write 0x55 command fail\n",
					__func__);
				continue;
			}

			cmd[0] = COMOND_HEAD;
			cmd[1] = COMOND_READ_ONE;
			cmd[2] = COMOND_ADDR_DETECT;
			rc = touch_driver_spi_transfer(cmd, cmd,
				ARRAY_SIZE(cmd));
			if (rc) {
				thp_log_err("%s:write 0x90 command fail\n",
					__func__);
				goto exit;
			}
			msleep(10); /* 10ms sequential */

			cmd[0] = DATA_HEAD;
			cmd[1] = DATA_MASK;
			cmd[2] = DATA_MASK;
			rc = touch_driver_spi_transfer(cmd, cmd,
				ARRAY_SIZE(cmd));
			if (rc) {
				thp_log_err("%s:read 0x90 data fail\n",
					__func__);
				goto exit;
			}

			if ((cmd[1] == CHIP_DETECT_FAILE_ONE) ||
				(cmd[1] == CHIP_DETECT_FAILE_TWO) ||
				(cmd[1] == CHIP_DETECT_FAILE_THR)) {
				thp_log_err("%s:chip id read fail\n",
					__func__);
				thp_log_err("ret=%d, i=%d, data=%x\n",
					rc, i, cmd[1]);
			} else {
				thp_log_info("%s:chip id read success\n",
					__func__);
				thp_log_info("chip id:0x%X%X\n",
					cmd[1], cmd[2]); /* cmd[2] data mask */
				return 0;
			}
			msleep(50); /* 50ms sequential */
		}
	}
exit:
	if (tdev->thp_core->self_control_power) {
		touch_driver_power_off(tdev);
		touch_driver_power_release();
	}
	if (tdev->thp_core->fast_booting_solution) {
		kfree(tdev->tx_buff);
		tdev->tx_buff = NULL;
		kfree(tdev->rx_buff);
		tdev->rx_buff = NULL;
		kfree(tdev);
		tdev = NULL;
	}
	return -EIO;
}

static int touch_driver_get_frame(struct thp_device *tdev,
	char *buf, unsigned int len)
{
	int ret;
	unsigned char *w_buf = NULL;
	unsigned char *r_buf = NULL;
	u32 txlen = 0;

	if (!tdev) {
		thp_log_info("%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	if (!tdev->tx_buff || !tdev->rx_buff) {
		thp_log_err("%s: out of memory\n", __func__);
		return -ENOMEM;
	}
	if (!buf) {
		thp_log_info("%s: input buf null\n", __func__);
		return -ENOMEM;
	}

	if ((!len) || (len >= THP_MAX_FRAME_SIZE - 1)) {
		thp_log_info("%s: read len: %u illegal\n", __func__, len);
		return -ENOMEM;
	}

	w_buf = tdev->tx_buff;
	r_buf = tdev->rx_buff;
	memset(tdev->tx_buff, 0xFF, THP_MAX_FRAME_SIZE);
	memset(tdev->rx_buff, 0, THP_MAX_FRAME_SIZE);

	if (tdev->thp_core->support_vendor_ic_type == FT8720_SERIES) {
		w_buf[txlen++] = is_ic_ft8009 ? ADDR_RAWDATA : ADDR_FRAME;
		w_buf[txlen++] = FTS_READ_CMD;
		w_buf[txlen++] = (len >> 8) & 0xFF; /* get high 8 bit */
		w_buf[txlen++] = len & 0xFF; /* get low 8 bit */
		txlen = SPI_DUMMY_BYTE + len;
		txlen = txlen + ADDR_OFFSET;

		ret = touch_driver_spi_transfer(w_buf, r_buf, txlen);
		if (ret < 0) {
			thp_log_err("spi transfer fail\n");
			return ret;
		}

		ret = touch_driver_check_frame_valid(r_buf +
						SPI_DUMMY_BYTE,
						txlen - SPI_DUMMY_BYTE -
						ADDR_OFFSET);
		if (ret < 0) {
			ret = touch_driver_spi_transfer(w_buf, r_buf, txlen);
			if (ret < 0) {
				thp_log_err("spi transfer fail\n");
				return ret;
			}
		}
		memcpy(buf, r_buf + SPI_DUMMY_BYTE, len);
	} else {
		w_buf[0] = COMMOND_READ_DATA;
		ret = touch_driver_spi_transfer(w_buf, r_buf, len + 1);
		if (ret < 0) {
			thp_log_err("spi transfer fail\n");
			return ret;
		}
		ret = touch_driver_check_frame_valid(r_buf + 1, len);
		if (ret < 0) {
			ret = touch_driver_spi_transfer(w_buf, r_buf, len + 1);
			if (ret < 0) {
				thp_log_err("spi transfer fail\n");
				return ret;
			}
		}
		memcpy(buf, r_buf + 1, len);
	}
	return 0;
}

#ifdef CONFIG_HUAWEI_THP_MTK
static void touch_driver_pinctrl_resume(struct thp_device *tdev)
{
	if (tdev->thp_core->support_pinctrl == 0) {
		thp_log_info("%s: not support pinctrl\n", __func__);
		return;
	}
	pinctrl_select_state(tdev->thp_core->pctrl,
		tdev->thp_core->mtk_pinctrl.cs_high);
	if (tdev->thp_core->reset_status_in_suspend_mode)
		thp_log_info("%s: reset retains high\n", __func__);
	else
		pinctrl_select_state(tdev->thp_core->pctrl,
			tdev->thp_core->mtk_pinctrl.reset_high);
	thp_do_time_delay(tdev->timing_config.resume_reset_after_delay_ms);
}

static void touch_driver_pinctrl_suspend(struct thp_device *tdev)
{
	if (tdev->thp_core->support_pinctrl == 0) {
		thp_log_info("%s: not support pinctrl\n", __func__);
		return;
	}
	if (is_pt_test_mode(tdev)) {
		thp_log_info("%s: PT sleep mode\n", __func__);
	} else {
		if (tdev->thp_core->reset_status_in_suspend_mode)
			thp_log_info("%s: reset retains high\n", __func__);
		else
			pinctrl_select_state(tdev->thp_core->pctrl,
				tdev->thp_core->mtk_pinctrl.reset_low);
	}
	pinctrl_select_state(tdev->thp_core->pctrl,
		tdev->thp_core->mtk_pinctrl.cs_low);
	thp_do_time_delay(tdev->timing_config.suspend_reset_after_delay_ms);
}
#endif

static void touch_driver_power_on_tddi(struct thp_device *tdev)
{
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	thp_do_time_delay(tdev->timing_config.resume_reset_after_delay_ms);
}

static void touch_driver_power_off_tddi(struct thp_device *tdev)
{
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_LOW);
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);
	thp_do_time_delay(tdev->timing_config.suspend_reset_after_delay_ms);
}

static int touch_driver_resume(struct thp_device *tdev)
{
	unsigned int gesture_status;
	unsigned int finger_status = !!(thp_get_status(THP_STATUS_UDFP));
	struct thp_core_data *cd = thp_get_core_data();
	bool no_power_off_flag;
	bool error_flag = (!tdev) || (!tdev->thp_core);

	thp_log_info("%s: called\n", __func__);
	if (error_flag) {
		thp_log_info("%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	gesture_status = tdev->thp_core->easy_wakeup_info.sleep_mode;
	no_power_off_flag = (gesture_status == TS_GESTURE_MODE) ||
		finger_status || cd->aod_touch_status || is_pt_test_mode(tdev);
#ifndef CONFIG_HUAWEI_THP_MTK
	if (no_power_off_flag) {
		thp_log_info("%s: gesture mode in\n", __func__);
		gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);
		gpio_set_value(tdev->gpios->rst_gpio, GPIO_LOW);
		msleep(1);
		gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	} else {
		if (tdev->thp_core->self_control_power)
			touch_driver_power_on(tdev);
		else
			touch_driver_power_on_tddi(tdev);
	}
#else
	touch_driver_pinctrl_resume(tdev);
#endif
#if (defined(CONFIG_HUAWEI_THP_MTK) || defined(CONFIG_HUAWEI_THP_QCOM))
	if (tdev->thp_core->use_ap_gesture)
		ud_mode_status.lowpower_mode = 0; /* clear lowpower status */
#endif
	return 0;
}

static int touch_driver_wrong_touch(struct thp_device *tdev)
{
	if ((!tdev) || (!tdev->thp_core)) {
		thp_log_err("%s: input dev is null\n", __func__);
		return -EINVAL;
	}

	if (tdev->thp_core->support_gesture_mode) {
		mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
		tdev->thp_core->easy_wakeup_info.off_motion_on = true;
		mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
		thp_log_info("%s, done\n", __func__);
	}
	return 0;
}

static void touch_driver_enter_gesture_mode(struct thp_device *tdev)
{
	int i;
	int ret;
	unsigned char w_buf[SPI_DUMMY_BYTE + FTS_REG_LEN];
	unsigned char r_buf[SPI_DUMMY_BYTE + FTS_REG_LEN];
	u32 index = 0;
	u32 txlen;

	thp_log_info("%s: called\n", __func__);
	w_buf[index++] = FTS_REG_GESTURE_EN;
	w_buf[index++] = FTS_WRITE_CMD;
	w_buf[index++] = (FTS_REG_LEN >> GET_HIGH_BIT) & 0xFF;
	w_buf[index++] = FTS_REG_LEN & 0xFF;
	txlen = SPI_DUMMY_BYTE + FTS_REG_LEN;
	w_buf[SPI_DUMMY_BYTE] = GESTURE_DATA_ENABLE;

	for (i = 0; i < SPI_RETRY_NUMBER; i++) {
		ret = touch_driver_spi_transfer(w_buf, r_buf, txlen);
		if ((ret == 0) && ((r_buf[FTS_OFF_STS] & FTS_READ_CMD) == 0))
			break;
		thp_log_info("data write addr:%x,status:%x,retry:%d,ret:%d",
			FTS_REG_GESTURE_EN, r_buf[FTS_OFF_STS], i, ret);
		ret = -EIO;
		udelay(CS_HIGH_DELAY);
	}
	if (ret < 0)
		thp_log_err("data write addr:%x fail,status:%x,ret:%d",
			FTS_REG_GESTURE_EN, r_buf[FTS_OFF_STS], ret);
	mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
	tdev->thp_core->easy_wakeup_info.off_motion_on = true;
	mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
}

static void gesture_process_oled(struct thp_device *tdev,
	unsigned int gesture_status, unsigned int finger_status)
{
	u8 reg_value = 0;
	struct thp_core_data *cd = thp_get_core_data();

	if (gesture_status == TS_GESTURE_MODE) {
		thp_log_info("enable double click");
		reg_value |= 1 << 1; /* bit1 set to 1 */
	}
	if (finger_status) {
		thp_log_info("enable finger print");
		reg_value |= 1 << 4; /* bit4 set to 1 */
	}
	if (cd->aod_touch_status) {
		thp_log_info("enable single click");
		reg_value |= 1 << 0; /* bit0 set to 1 */
	}
	thp_log_info("write to reg 0xd1 %x\n", reg_value);
	touch_driver_write_reg(FTS_REG_GESTURE, reg_value);
	touch_driver_enter_gesture_mode(tdev);
}

static void gesture_process_tddi(struct thp_device *tdev,
	unsigned int gesture_status)
{
	if (gesture_status == TS_GESTURE_MODE &&
		tdev->thp_core->lcd_gesture_mode_support) {
		thp_log_info("%s: TS_GESTURE_MODE\n", __func__);
		touch_driver_enter_gesture_mode(tdev);
	}
}

static void parse_gesture_info(u8 *gesture_buffer)
{
	int cnt = 0;
	char str[FTS_GESTURE_STR_LEN_MAX];
	int str_len_max = FTS_GESTURE_STR_LEN_MAX;
	int i;

	if ((gesture_buffer[GESTURE_INFO_BIT] & GESTURE_INFO_VALUE) ==
		GESTURE_INFO_VALUE) {
		for (i = 0; i < FTS_GESTURE_DATA_LEN; i++)
			cnt += snprintf(str + cnt, str_len_max - cnt, "%02X ",
				gesture_buffer[i]);
		thp_log_info("%s: %s\n", __func__, str);
	}
}

static int touch_driver_read_gesture_data(void)
{
	int ret;
	int i;
	unsigned char w_buf[SPI_DUMMY_BYTE + FTS_GESTURE_DATA_LEN];
	unsigned char r_buf[SPI_DUMMY_BYTE + FTS_GESTURE_DATA_LEN];
	u32 txlen = 0;
	unsigned char *gesture_data = NULL;

	thp_log_info("%s: called\n", __func__);
	w_buf[txlen++] = FTS_REG_GESTURE_DATA;
	w_buf[txlen++] = FTS_READ_CMD;
	w_buf[txlen++] = (FTS_GESTURE_DATA_LEN >> GET_HIGH_BIT) & 0xFF;
	w_buf[txlen++] = FTS_GESTURE_DATA_LEN & 0xFF;
	txlen = SPI_DUMMY_BYTE + FTS_GESTURE_DATA_LEN + ADDR_OFFSET;
	for (i = 0; i < SPI_RETRY_NUMBER; i++) {
		ret = touch_driver_spi_transfer(w_buf, r_buf, txlen);
		if ((ret == 0) && ((r_buf[FTS_OFF_STS] & FTS_READ_CMD) == 0)) {
			ret = touch_driver_check_frame_valid(r_buf +
				SPI_DUMMY_BYTE,
				txlen - SPI_DUMMY_BYTE - ADDR_OFFSET);
			if (ret < 0) {
				thp_log_info("addr:%x crc abnormal,retry:%d",
					FTS_REG_GESTURE_DATA, i);
				msleep(WAIT_FOR_SPI_BUS_READ_DELAY);
				continue;
			}
			break;
		}
		thp_log_info("addr:%x status:%x,ret:%d",
			FTS_REG_GESTURE_DATA, r_buf[FTS_OFF_STS], ret);
		ret = -EIO;
		msleep(WAIT_FOR_SPI_BUS_READ_DELAY);
	}
	if (ret < 0) {
		thp_log_err("data read(addr:%x) %s,status:%x,ret:%d",
			FTS_REG_GESTURE_DATA,
			(i >= SPI_RETRY_NUMBER) ? "crc abnormal" : "fail",
			r_buf[FTS_OFF_STS], ret);
		return ret;
	}
	gesture_data = r_buf + SPI_DUMMY_BYTE;
	parse_gesture_info(gesture_data);
	if (gesture_data[0] != GESTURE_DATA_ENABLE) {
		thp_log_err("gesture is disabled");
		return -EIO;
	}
	if (gesture_data[FTS_GESTURE_ID_BIT] == FTS_GESTURE_ID_VALUE)
		return 0;
	return -EIO;
}

int touch_driver_report_gesture(struct thp_device *tdev,
	unsigned int *gesture_wakeup_value)
{
	if ((!tdev) || (!tdev->thp_core) || (!tdev->thp_core->sdev)) {
		thp_log_info("%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	if ((!gesture_wakeup_value) ||
		(!tdev->thp_core->support_gesture_mode)) {
		thp_log_err("%s, gesture not support\n", __func__);
		return -EINVAL;
	}
	if (touch_driver_read_gesture_data()) {
		thp_log_err("%s: gesture data read or write fail\n", __func__);
		return -EINVAL;
	}
	mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
	if (tdev->thp_core->easy_wakeup_info.off_motion_on == true) {
		tdev->thp_core->easy_wakeup_info.off_motion_on = false;
		*gesture_wakeup_value = TS_DOUBLE_CLICK;
	}
	mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
	return 0;
}

static int touch_driver_suspend(struct thp_device *tdev)
{
	unsigned int gesture_status;
	struct thp_core_data *cd = thp_get_core_data();
	unsigned int finger_status = !!(thp_get_status(THP_STATUS_UDFP));
	bool error_flag = (!tdev) || (!tdev->thp_core);
	bool gesture_flag;

	thp_log_info("%s: called\n", __func__);
	if (error_flag) {
		thp_log_info("%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	gesture_status = tdev->thp_core->easy_wakeup_info.sleep_mode;
	gesture_flag = (gesture_status == TS_GESTURE_MODE) ||
		finger_status || cd->aod_touch_status;
	if (gesture_flag) {
		if (tdev->thp_core->self_control_power)
			gesture_process_oled(tdev, gesture_status, finger_status);
		else
			gesture_process_tddi(tdev, gesture_status);
		return 0;
	}
#ifndef CONFIG_HUAWEI_THP_MTK
	if (is_pt_test_mode(tdev)) {
		thp_log_info("%s: PT sleep mode\n", __func__);
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);
	} else {
		if (tdev->thp_core->self_control_power)
			touch_driver_power_off(tdev);
		else
			touch_driver_power_off_tddi(tdev);
	}
#else
	touch_driver_pinctrl_suspend(tdev);
#endif
	thp_log_info("%s: called end\n", __func__);
	return 0;
}

static void touch_driver_exit(struct thp_device *tdev)
{
	thp_log_info("%s: called\n", __func__);
	if (tdev) {
		kfree(tdev->tx_buff);
		tdev->tx_buff = NULL;
		kfree(tdev->rx_buff);
		tdev->rx_buff = NULL;
		kfree(tdev);
		tdev = NULL;
	}
}
static int touch_driver_second_poweroff(void)
{
	struct thp_core_data *cd = thp_get_core_data();
	struct thp_device *tdev = cd->thp_dev;
	int ret;
	int i;
	unsigned char *w_buf = NULL;
	unsigned char *r_buf = NULL;
	u32 txlen = 0;

	thp_log_info("%s: called\n", __func__);
	if (!tdev || !tdev->tx_buff || !tdev->rx_buff) {
		thp_log_err("%s: tdev/tx_buff/rx_buff null\n", __func__);
		return -ENOMEM;
	}

	w_buf = tdev->tx_buff;
	r_buf = tdev->rx_buff;

	for (i = 0; i < SLEEP_RETRY_NUM; i++) {
		udelay(150); /* delay 150us */
		w_buf[txlen++] = 0x62; /* power off twice command */
		w_buf[txlen++] = FTS_WRITE_CMD; /* 0x00  write */
		w_buf[txlen++] = 0x00;
		w_buf[txlen++] = 0x00;
		ret = touch_driver_spi_transfer(w_buf, r_buf, txlen);
		if ((ret < 0) || ((r_buf[SLEEP_BUF_BIT] & 0xA0) != 0)) {
			thp_log_err("data write addr:%x fail,status:%x,ret:%d",
				0x62, r_buf[SLEEP_BUF_BIT], ret);
			continue;
		} else {
			break;
		}
	}
	return ret;
}

#if (defined(CONFIG_HUAWEI_THP_MTK) || defined(CONFIG_HUAWEI_THP_QCOM))
static int state_check_for_lowpower_set(struct thp_device *tdev,
	u8 state)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (tdev == NULL) {
		thp_log_err("%s: tdev null\n", __func__);
		return -EINVAL;
	}
	if (cd->work_status != SUSPEND_DONE) {
		thp_log_info("%s: resumed, not handle lp\n", __func__);
		return -EINVAL;
	}
	if (ud_mode_status.lowpower_mode == state) {
		thp_log_info("%s:don't repeat old status %u\n",
			__func__, state);
		return -EINVAL;
	}
		return 0;
	}

static int touch_driver_set_cmd(unsigned char *tx_buf,
	unsigned char *rx_buf, unsigned int len)
{
	int ret;
	int index;
	for (index = 0; index < SPI_RETRY_NUMBER; index++) {
		ret = touch_driver_spi_transfer(tx_buf, rx_buf, len);
		if (ret < 0) {
			thp_log_info("fts_set_lowpower_state fail");
			msleep(1);
			continue;
		}
		break;
	}
	return ret;
}

static int touch_driver_set_lowpower_state(struct thp_device *tdev,
	u8 state)
{
	int ret;
	unsigned char sleep_cmd[] = {0x5B, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
	unsigned char r_buf[READ_LEN] = {0};

	thp_log_info("%s: called state = %u\n", __func__, state);
	if (state_check_for_lowpower_set(tdev, state))
		return -EINVAL;
	ret = touch_driver_set_cmd(sleep_cmd, r_buf, sizeof(sleep_cmd));
	if (ret < 0)
		return ret;
	ud_mode_status.lowpower_mode = state;
	return 0;
}

static int touch_driver_set_aod_state(struct thp_device *tdev,
	u8 state, struct thp_aod_window window)
{
	return 0;
}

static int touch_driver_parse_event_info(struct thp_device *tdev,
	const char *read_buff, struct thp_udfp_data *udfp_data)
{
	int index;
	struct thp_core_data *cd = NULL;
	u32 udfp_info_data[UDFP_INFO_SIZE] = {0};

	cd = tdev->thp_core;
	for (index = 0; index < UDFP_INFO_SIZE; index++)
		/* Concatenate bytes based on bits. */
		udfp_info_data[index] = (read_buff[(index * 4)] << 24) +
		(read_buff[(index * 4) + 1] << 16) + (read_buff[(index * 4) + 2] << 8) +
		read_buff[(index * 4) + 3];
	udfp_data->tpud_data.udfp_event = TP_FP_EVENT_MAX;
	udfp_data->key_event = udfp_info_data[15]; /* gesture event */
	/* udfp_info_data[8~9] = touch coordinate */
	udfp_data->tpud_data.tp_x = udfp_info_data[8];
	udfp_data->tpud_data.tp_y = udfp_info_data[9];

	thp_log_info("tp_x = %d, tp_y = %d",
		udfp_data->tpud_data.tp_x, udfp_data->tpud_data.tp_y);
		udfp_data->tpud_data.udfp_event = udfp_info_data[1]; /* finger event */
	if (udfp_data->key_event == DOUBLE_TAP_FLAG)
		udfp_data->key_event = TS_DOUBLE_CLICK;
		udfp_data->aod_event = udfp_info_data[14]; /* aod event */
	return 0;
}

static int touch_driver_get_gesture_data(unsigned char *w_buf,
	unsigned char *r_buf)
{
	u32 txlen = 0;
	int index;
	int ret;

	w_buf[txlen++] = FTS_REG_GESTURE_DATA;
	w_buf[txlen++] = FTS_READ_CMD;
	w_buf[txlen++] = (FTS_UDFP_DATA_LEN >> GET_HIGH_BIT) & 0xFF;
	w_buf[txlen++] = FTS_UDFP_DATA_LEN & 0xFF;
	txlen = SPI_DUMMY_BYTE + FTS_UDFP_DATA_LEN + ADDR_OFFSET;
	for (index = 0; index < SPI_RETRY_NUMBER; index++) {
		ret = touch_driver_spi_transfer(w_buf, r_buf, txlen);
		if ((ret == 0) && ((r_buf[FTS_OFF_STS] & FTS_READ_CMD) == 0)) {
			ret = touch_driver_check_frame_valid(r_buf +
				SPI_DUMMY_BYTE,
				txlen - SPI_DUMMY_BYTE - ADDR_OFFSET);
			if (ret < 0) {
				thp_log_err("addr:%x crc abnormal, retry:%d",
					FTS_REG_GESTURE_DATA, index);
				msleep(WAIT_FOR_SPI_BUS_READ_DELAY);
				continue;
			}
			break;
		}
		thp_log_err("addr:%x status:%x, ret:%d",
			FTS_REG_GESTURE_DATA, r_buf[FTS_OFF_STS], ret);
		ret = -EIO;
		msleep(WAIT_FOR_SPI_BUS_READ_DELAY);
	}
		return ret;
	}

static int touch_driver_get_event_info(struct thp_device *tdev,
	struct thp_udfp_data *udfp_data)
{
	int ret;
	unsigned char *gesture_data = NULL;
	unsigned char w_buf[SPI_DUMMY_BYTE + FTS_UDFP_DATA_LEN + ADDR_OFFSET] = {0};
	unsigned char r_buf[SPI_DUMMY_BYTE + FTS_UDFP_DATA_LEN + ADDR_OFFSET] = {0};

	thp_log_info("%s enter\n", __func__);
	ret = touch_driver_get_gesture_data(w_buf, r_buf);
	if (ret < 0)
		return ret;
	gesture_data = r_buf + SPI_DUMMY_BYTE;
	ret = touch_driver_parse_event_info(tdev, gesture_data, udfp_data);
	return ret;
}
#endif
struct thp_device_ops fts_dev_ops = {
	.init = touch_driver_init,
	.detect = touch_driver_chip_detect,
	.get_frame = touch_driver_get_frame,
	.resume = touch_driver_resume,
	.suspend = touch_driver_suspend,
	.exit = touch_driver_exit,
	.spi_transfer = touch_driver_spi_transfer,
	.chip_wrong_touch = touch_driver_wrong_touch,
	.chip_gesture_report = touch_driver_report_gesture,
	.second_poweroff = touch_driver_second_poweroff,
	#if (defined(CONFIG_HUAWEI_THP_MTK) || defined(CONFIG_HUAWEI_THP_QCOM))
	.get_event_info = touch_driver_get_event_info,
	.tp_lowpower_ctrl = touch_driver_set_lowpower_state,
	.tp_aod_event_ctrl = touch_driver_set_aod_state,
	#endif
};

static int __init touch_driver_module_init(void)
{
	int rc = 0;
	struct thp_device *dev = NULL;
	struct thp_core_data *cd = thp_get_core_data();

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		thp_log_err("%s: thp device malloc fail\n", __func__);
		return -ENOMEM;
	}

	dev->tx_buff = kzalloc(THP_MAX_FRAME_SIZE, GFP_KERNEL);
	dev->rx_buff = kzalloc(THP_MAX_FRAME_SIZE, GFP_KERNEL);
	if (!dev->tx_buff || !dev->rx_buff) {
		thp_log_err("%s: out of memory\n", __func__);
		rc = -ENOMEM;
		goto err;
	}

	dev->ic_name = FOCALTECH_IC_NAME;
	dev->ops = &fts_dev_ops;
	if (cd && cd->fast_booting_solution) {
		thp_send_detect_cmd(dev, NO_SYNC_TIMEOUT);
		return 0;
	}
	rc = thp_register_dev(dev);
	if (rc) {
		thp_log_err("%s: register fail\n", __func__);
		goto err;
	}

	return rc;
err:
	kfree(dev->tx_buff);
	dev->tx_buff = NULL;
	kfree(dev->rx_buff);
	dev->rx_buff = NULL;
	kfree(dev);
	dev = NULL;
	return rc;
}

static void __exit touch_driver_module_exit(void)
{
	thp_log_info("%s: called\n", __func__);
};

#ifdef CONFIG_HUAWEI_THP_QCOM
late_initcall(touch_driver_module_init);
#else
module_init(touch_driver_module_init);
#endif
module_exit(touch_driver_module_exit);
MODULE_AUTHOR("focaltech");
MODULE_DESCRIPTION("focaltech driver");
MODULE_LICENSE("GPL");
