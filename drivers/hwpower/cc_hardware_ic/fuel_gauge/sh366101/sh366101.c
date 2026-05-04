// SPDX-License-Identifier: GPL-2.0
/*
 * sh366101.c
 *
 * driver for sh366101 battery fuel gauge
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

#include "sh366101.h"

#define HWLOG_TAG sh366101
#define SH366301_CAPACITY_TH    7
HWLOG_REGIST();

static unsigned short sh366101_buf2u16_bg(unsigned char *p)
{
	return (((u16)p[0] << 8) | (u8)p[1]);
}

static unsigned short sh366101_buf2u16_lt(unsigned char *p)
{
	return (((u16)p[1] << 8) | (u8)p[0]);
}

static unsigned int sh366101_buf2u32_bg(unsigned char *p)
{
	return (((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u8)p[3]);
}

static unsigned int sh366101_buf2u32_lt(unsigned char *p)
{
	return (((u32)p[3] << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | (u8)p[0]);
}

static unsigned char sh366101_i2c_addr_of_2_kernel(unsigned char addr)
{
	return ((u8)(addr) >> 1);
}

static int sh366101_decode_i2c_read(struct sh366101_dev* di, struct sh_decoder* decoder,
	unsigned char* buf, unsigned int len)
{
	static struct i2c_msg msg[SH366101_I2C_READ_LEN];
	unsigned char addr = sh366101_i2c_addr_of_2_kernel(decoder->addr);
	int ret;

	if (!di->client->adapter)
		return -ENODEV;

	mutex_lock(&di->i2c_rw_lock);

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].buf = &(decoder->reg);
	msg[0].len = sizeof(u8);
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = decoder->length;
	ret = (int)i2c_transfer(di->client->adapter, msg, ARRAY_SIZE(msg));

	mutex_unlock(&di->i2c_rw_lock);

	return ret;
}

static int sh366101_decode_i2c_write(struct sh366101_dev* di, struct sh_decoder* decoder)
{
	static struct i2c_msg msg[SH366101_I2C_WRITE_LEN];
	static unsigned char write_buf[SH366101_WRITE_BUF_MAX_LEN];
	unsigned char addr = sh366101_i2c_addr_of_2_kernel(decoder->addr);
	unsigned char length = decoder->length;
	int ret;

	if (!di->client->adapter)
		return -ENODEV;

	/* prevent array out of bounds */
	if ((length <= 0) || (length + 1 >= SH366101_WRITE_BUF_MAX_LEN)) {
		hwlog_err("i2c write buffer fail: length invalid\n");
		return -EINVAL;
	}

	mutex_lock(&di->i2c_rw_lock);

	memset(write_buf, 0, SH366101_WRITE_BUF_MAX_LEN);
	write_buf[0] = decoder->reg;
	memcpy(&write_buf[SH366101_I2C_WRITE_LEN], &(decoder->buf_first_val), length);

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].buf = write_buf;
	msg[0].len = sizeof(u8) * (length + 1); /* include '\0' */

	ret = i2c_transfer(di->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		hwlog_err("i2c write buffer fail: can't write reg 0x%02x\n", decoder->reg);

	mutex_unlock(&di->i2c_rw_lock);

	return (ret < 0) ? ret : 0;
}

static int sh366101_read_word(struct i2c_client *client, unsigned char reg, unsigned short *value)
{
	unsigned short data0 = 0;

	if (!client) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	if (power_i2c_u8_read_word(client, reg, &data0, false)) {
		hwlog_err("i2c read word fail: can't read from reg 0x%02x\n", reg);
		return -EIO;
	}

	*value = data0;

	return 0;
}

static int sh366101_write_word(struct i2c_client *client, unsigned char reg, unsigned short val)
{
	if (!client) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	return power_i2c_u8_write_word(client, reg, val);
}

static int sh366101_read_sbs_word(struct sh366101_dev *di, unsigned int reg, unsigned short *val)
{
	int ret;

	mutex_lock(&di->i2c_rw_lock);
	if ((reg & SH366101_CMDMASK_ALTMAC_R) == SH366101_CMDMASK_ALTMAC_R) {
		ret = sh366101_write_word(di->client, SH366101_CMD_ALTMAC, (u16)reg);
		if (ret < 0)
			goto sh366101_read_sbs_word_end;

		msleep(SH366101_CMD_SBS_DELAY);
		ret = sh366101_read_word(di->client, SH366101_CMD_ALTBLOCK, val);
	} else {
		ret = sh366101_read_word(di->client, (u8)reg, val);
	}
sh366101_read_sbs_word_end:
	mutex_unlock(&di->i2c_rw_lock);

	return ret;
}

static int sh366101_write_sbs_word(struct sh366101_dev *di, unsigned int reg, unsigned short val)
{
	int ret;

	mutex_lock(&di->i2c_rw_lock);
	ret = sh366101_write_word(di->client, (u8)reg, val);
	mutex_unlock(&di->i2c_rw_lock);

	return ret;
}

static int sh366101_get_device_id(struct i2c_client *client)
{
	struct sh366101_dev *di = i2c_get_clientdata(client);
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_DEVICE_ID], &data);
	if (ret) {
		hwlog_err("failed to read device_id, ret=%d\n", ret);
		return ret;
	}

	di->chip_id = (int)data;
	hwlog_info("device_id=0x%04x\n", data);

	return 0;
}

static int sh366101_hal_fg_init(struct i2c_client *client)
{
	struct sh366101_dev *di = i2c_get_clientdata(client);
	int ret;

	hwlog_info("sh366101_hal_fg_init\n");
	mutex_lock(&di->data_lock);

	/* sh366101 i2c read check */
	ret = sh366101_get_device_id(client);
	if (ret) {
		hwlog_err("sh366101_hal_fg_init: fail to do i2c read %d\n", ret);
		mutex_unlock(&di->data_lock);
		return ret;
	}

	mutex_unlock(&di->data_lock);
	hwlog_info("hal fg init ok\n");

	return 0;
}

static struct device_node *sh366101_get_child_node(struct device *dev)
{
	const char *battery_name = NULL;
	const char *batt_model_name = NULL;
	struct device_node *np = dev->of_node;
	struct device_node *child_node = NULL;
	struct device_node *default_node = NULL;

	batt_model_name = bat_model_name();
	for_each_child_of_node(np, child_node) {
		if (power_dts_read_string(power_dts_tag(HWLOG_TAG),
			child_node, "batt_name", &battery_name)) {
			hwlog_err("childnode without batt_name property");
			continue;
		}
		if (!battery_name)
			continue;
		if (!default_node)
			default_node = child_node;
			hwlog_info("search battery data, battery_name: %s\n", battery_name);
		if (!batt_model_name || !strcmp(battery_name, batt_model_name))
			break;
	}

	if (!child_node) {
		if (default_node) {
			hwlog_info("cannt match childnode, use first\n");
			child_node = default_node;
		} else {
			hwlog_info("cannt find any childnode, use father\n");
			child_node = np;
		}
	}

	return child_node;
}

static int sh366101_write_buffer(struct i2c_client* client, unsigned char reg, unsigned char length, unsigned char* val)
{
	static struct i2c_msg msg[SH366101_I2C_WRITE_LEN];
	static unsigned char write_buf[SH366101_WRITE_BUF_MAX_LEN];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	if ((length <= 0) || (length + 1 >= SH366101_WRITE_BUF_MAX_LEN)) {
		hwlog_err("i2c write buffer fail: length invalid\n");
		return -EINVAL;
	}

	memset(write_buf, 0, SH366101_WRITE_BUF_MAX_LEN);
	write_buf[0] = reg;
	memcpy(&write_buf[1], val, length);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = write_buf;
	msg[0].len = sizeof(u8) * (length + 1);

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		hwlog_err("i2c write buffer fail: can't write reg 0x%02x\n", reg);
		return (int)ret;
	}

	return 0;
}

static int sh366101_write_block(struct sh366101_dev* di, unsigned int reg, unsigned char length, unsigned char* val)
{
	int ret;
	int i;
	unsigned short checksum;
	unsigned char sum;

	if (length > SH366101_BLOCK_LEN_MAX)
		length = SH366101_BLOCK_LEN_MAX;

	mutex_lock(&di->i2c_rw_lock);
	if ((reg & SH366101_CMDMASK_ALTMAC_W) == SH366101_CMDMASK_ALTMAC_W) {
		ret = sh366101_write_word(di->client, SH366101_CMD_ALTMAC, (u16)reg);
		if (ret < 0)
			goto sh366101_write_block_end;
		msleep(SH366101_CMD_SBS_DELAY);

		ret = sh366101_write_buffer(di->client, SH366101_CMD_ALTBLOCK, length, val);
		if (ret < 0)
			goto sh366101_write_block_end;
		msleep(SH366101_CMD_SBS_DELAY);

		sum = (u8)reg + (u8)(reg >> SH366101_I2C_BLOCK_SHIFT);
		for (i = 0; i < length; i++)
			sum = (unsigned char)(val[i] + sum);
		sum = 0xFF - sum;
		checksum = length + SH366101_I2C_LEN_ADD;
		checksum = (unsigned short)(((unsigned int)checksum << SH366101_I2C_BLOCK_SHIFT) + sum);

		ret = sh366101_write_word(di->client, SH366101_CMD_ALTCHK, checksum);
		if (ret < 0)
			goto sh366101_write_block_end;
	} else {
		ret = sh366101_write_buffer(di->client, (u8)reg, length, val);
	}
sh366101_write_block_end:
	mutex_unlock(&di->i2c_rw_lock);

	return ret;
}

static int sh366101_read_block(struct sh366101_dev* di, unsigned int reg, unsigned char length, unsigned char* val)
{
	int ret;
	int i;
	unsigned char sum;
	unsigned short checksum;
	unsigned char cmd[SH366101_CMD_LEN] = { 0 };

	cmd[0] = SH366101_CMD_ALTBLOCK;
	mutex_lock(&di->i2c_rw_lock);
	if ((reg & SH366101_CMDMASK_ALTMAC_R) == SH366101_CMDMASK_ALTMAC_R) {
		ret = sh366101_write_word(di->client, SH366101_CMD_ALTMAC, (u16)reg);
		if (ret < 0)
			goto sh366101_read_block_end;
		msleep(SH366101_CMD_SBS_DELAY);

		if (length > SH366101_BLOCK_LEN_MAX)
			length = SH366101_BLOCK_LEN_MAX;

		ret = power_i2c_read_block(di->client, cmd, sizeof(cmd), val, length);
		if (ret < 0)
			goto sh366101_read_block_end;
		msleep(SH366101_CMD_SBS_DELAY);

		ret = sh366101_read_word(di->client, SH366101_CMD_ALTCHK, &checksum);
		if (ret < 0)
			goto sh366101_read_block_end;

		i = (checksum >> SH366101_I2C_BLOCK_SHIFT) - SH366101_I2C_LEN_ADD;
		if (i <= 0)
			goto sh366101_read_block_end;

		sum = (u8)(reg & 0xFF) + (u8)((reg >> SH366101_I2C_BLOCK_SHIFT) & 0xFF);
		while (i--)
			sum = (unsigned char)(val[i] + sum);
		sum = 0xFF - sum;
		if (sum != (u8)checksum)
			ret = -EINVAL;
		else
			ret = 0;
	} else {
		ret = power_i2c_read_block(di->client, (u8 *)&reg, sizeof(u8), val, length);
	}

sh366101_read_block_end:
	mutex_unlock(&di->i2c_rw_lock);

	return ret;
}

static int sh366101_print_buffer(char* str, int strlen, unsigned char* buf, int buflen)
{
	int i;
	int j;

	if ((strlen <= 0) || (buflen <= 0))
		return -EINVAL;

	memset(str, 0, strlen * sizeof(char));

	j = min(buflen, strlen / SH366101_PRINT_BUFFER_FORMAT_LEN);
	for (i = 0; i < j; i++)
		sprintf(&str[i * SH366101_PRINT_BUFFER_FORMAT_LEN], "%02x ", buf[i]);

	return i * SH366101_PRINT_BUFFER_FORMAT_LEN;
}

static int sh366101_file_decode_process(struct sh366101_dev* di, struct device_node *np, char* profile_name)
{
	struct device* dev = &di->client->dev;
	unsigned char* buf = NULL;
	unsigned char* buf_read = NULL;
	char str_debug[SH366101_FILEDECODE_STRLEN];
	int buflen;
	int result;
	int wait_ms;
	int i, j;
	int line_length;
	int retry;

	/* battery_params node */
	np = of_find_node_by_name(of_node_get(np), "battery_params");
	if (np == NULL) {
		hwlog_err("file_decode_process: Cannot find child node \"battery_params\"\n");
		return -EINVAL;
	}

	hwlog_info("file_decode_process: start\n");

	buflen = of_property_count_u8_elems(np, profile_name);
	hwlog_info("file_decode_process: ele_len=%d, key=%s\n", buflen, profile_name);

	buf = (u8*)devm_kzalloc(dev, buflen, 0);
	buf_read = (u8*)devm_kzalloc(dev, SH366101_BUF_MAX_LENGTH, 0);
	if ((buf == NULL) || (buf_read == NULL)) {
		result = SH366101_ERRORTYPE_ALLOC;
		hwlog_err("file_decode_process: kzalloc error\n");
		goto main_process_error;
	}

	result = of_property_read_u8_array(np, profile_name, buf, buflen);
	if (result) {
		hwlog_err("file_decode_process: read dts fail %s\n", profile_name);
		goto main_process_error;
	}
	sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN, buf, SH366101_DECODE_BUF_LENGTH);
	hwlog_info("file_decode_process: first data=%s\n", str_debug);

	i = 0;
	j = 0;
	while (i < buflen) {
		if (buf[i + SH366101_INDEX_TYPE] == SH366101_OPERATE_WAIT) {
			wait_ms = ((int)buf[i + SH366101_INDEX_WAIT_HIGH] * SH366101_INDEX_WAIT_UNIT)
				+ (int)buf[i + SH366101_INDEX_WAIT_LOW];

			if (buf[i + SH366101_INDEX_WAIT_LENGTH] == SH366101_OPERATE_WRITE) {
				msleep(wait_ms);
				i += SH366101_LINELEN_WAIT;
			} else {
				sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN,
					&buf[i + SH366101_INDEX_TYPE], SH366101_DECODE_BUF_LENGTH);
				hwlog_err("file_decode_process wait error, index=%d, str=%s\n", i, str_debug);
				result = SH366101_ERRORTYPE_LINE;
				goto main_process_error;
			}
		} else if (buf[i + SH366101_INDEX_TYPE] == SH366101_OPERATE_READ) {
			line_length = buf[i + SH366101_INDEX_LENGTH];
			if (line_length <= 0) {
				result = SH366101_ERRORTYPE_LINE;
				goto main_process_error;
			}

			/* iap addr may differ from default addr */
			if (sh366101_decode_i2c_read(di, (struct sh_decoder*)&buf[i + SH366101_INDEX_ADDR], buf_read, buflen) < 0) {
				sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN,
					&buf[i + SH366101_INDEX_TYPE], SH366101_DECODE_BUF_LENGTH);
				hwlog_err("file_decode_process read error, index=%d, str=%s\n", i, str_debug);
				result = SH366101_ERRORTYPE_COMM;
				goto main_process_error;
			}

			i += SH366101_LINELEN_READ;
		} else if (buf[i + SH366101_INDEX_TYPE] == SH366101_OPERATE_COMPARE) {
			line_length = buf[i + SH366101_INDEX_LENGTH];
			if (line_length <= 0) {
				result = SH366101_ERRORTYPE_LINE;
				goto main_process_error;
			}

			for (retry = 0; retry < SH366101_COMPARE_RETRY_CNT; retry++) {
				/* iap addr may differ from default addr */
				if (sh366101_decode_i2c_read(di, (struct sh_decoder*)&buf[i + SH366101_INDEX_ADDR], buf_read, buflen) < 0) {
					sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN,
						&buf[i + SH366101_INDEX_TYPE], SH366101_DECODE_BUF_LENGTH);
					hwlog_err("file_decode_process compare_read error, index=%d, str=%s\n", i, str_debug);
					result = SH366101_ERRORTYPE_COMM;
					goto file_decode_process_compare_loop_end;
				}

				sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN, buf_read, line_length);
				hwlog_info("file_decode_process loop compare: ic read=%s\n", str_debug);

				result = 0;
				for (j = 0; j < line_length; j++) {
					if (buf[SH366101_INDEX_DATA + i + j] != buf_read[j]) {
						result = SH366101_ERRORTYPE_COMPARE;
						break;
					}
				}

				if (result == 0)
					break;

				/* compare fail */
				sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN,
					&buf[i + SH366101_INDEX_TYPE], SH366101_DECODE_BUF_LENGTH);
				hwlog_err("file_decode_process compare error, index=%d, retry=%d, host=%s\n", i, retry, str_debug);
				sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN,
					buf_read, SH366101_DECODE_BUF_LENGTH);
				hwlog_err("ic=%s\n", str_debug);

			file_decode_process_compare_loop_end:
				msleep(SH366101_COMPARE_RETRY_WAIT);
			}

			if (retry >= SH366101_COMPARE_RETRY_CNT)
				goto main_process_error;

			i += SH366101_LINELEN_COMPARE + line_length;
		} else if (buf[i + SH366101_INDEX_TYPE] == SH366101_OPERATE_WRITE) {
			line_length = buf[i + SH366101_INDEX_LENGTH];
			if (line_length <= 0) {
				result = SH366101_ERRORTYPE_LINE;
				goto main_process_error;
			}

			/* iap addr may differ from default addr */
			if (sh366101_decode_i2c_write(di, (struct sh_decoder*)&buf[i + SH366101_INDEX_ADDR]) != 0) {
				sh366101_print_buffer(str_debug, sizeof(char) * SH366101_FILEDECODE_STRLEN,
					&buf[i + SH366101_INDEX_TYPE], SH366101_DECODE_BUF_LENGTH);
				hwlog_err("file_decode_process write error, index=%d, str=%s\n", i, str_debug);
				result = SH366101_ERRORTYPE_COMM;
				goto main_process_error;
			}

			i += SH366101_LINELEN_WRITE + line_length;
		} else {
			result = SH366101_ERRORTYPE_LINE;
			goto main_process_error;
		}
	}
	result = SH366101_ERRORTYPE_NONE;

main_process_error:
	hwlog_info("file_decode_process end: result=%d\n", result);
	return result;
}

static int sh366101_get_user_info(struct device* dev, int length, unsigned char* pbuf)
{
	struct i2c_client* client = to_i2c_client(dev);
	struct sh366101_dev* di = i2c_get_clientdata(client);
	int ret;

	if (length > SH366101_LEN_USERBUFFER)
		length = SH366101_LEN_USERBUFFER;

	ret = sh366101_read_block(di, SH366101_CMD_USERBUFFER, length, pbuf);
	if (ret < 0) {
		hwlog_err("fg_get_user_info error, ret=%d\n", ret);
		return ret;
	}

	return 0;
}

static int sh366101_set_user_info(struct device* dev, int length, unsigned char* pbuf)
{
	struct i2c_client* client = to_i2c_client(dev);
	struct sh366101_dev* di = i2c_get_clientdata(client);
	int ret;

	if (length > SH366101_LEN_USERBUFFER)
		length = SH366101_LEN_USERBUFFER;

	ret = sh366101_write_block(di, SH366101_CMD_USERBUFFER, length, pbuf);
	if (ret < 0) {
		hwlog_err("fg_get_user_info error, ret=%d\n", ret);
		return ret;
	}

	return 0;
}

static int sh36610_read_temperature(struct sh366101_dev *di, enum sh366101_temperature_type temperature_type)
{
	int ret;
	unsigned int temp;
	unsigned short data = 0;

	if (temperature_type == SH366101_TEMPERATURE_IN) {
		temp = di->regs[SH366101_REG_SH366101_TEMPERATURE_IN];
	} else if (temperature_type == SH366101_TEMPERATURE_EX) {
		temp = di->regs[SH366101_REG_SH366101_TEMPERATURE_EX];
	} else {
		return -EINVAL;
	}

	ret = sh366101_read_sbs_word(di, temp, &data);
	if (ret < 0) {
		hwlog_err("could not read temperature, ret=%d\n", ret);
		return ret;
	}

	mutex_lock(&di->data_lock);
	di->batt_temp = (int)data - SH366101_TEMPER_OFFSET;
	mutex_unlock(&di->data_lock);

	return di->batt_temp;
}

static int sh366101_read_soc(struct sh366101_dev *di)
{
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_SOC], &data);
	if (ret < 0) {
		hwlog_err("could not read soc, ret=%d\n", ret);
		return ret;
	}
	hwlog_info("%s soc=%d\n", __func__, data);

	mutex_lock(&di->data_lock);
	di->batt_soc = data;
	mutex_unlock(&di->data_lock);

	return di->batt_soc;
}

static int sh366101_read_volt(struct sh366101_dev *di)
{
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_VOLTAGE], &data);
	if (ret < 0) {
		hwlog_err("could not read voltage, ret=%d\n", ret);
		return ret;
	}

	mutex_lock(&di->data_lock);
	di->batt_volt = (int)data * SH366101_MA_TO_UA;
	di->aver_batt_volt = (((di->aver_batt_volt) * 4) + (int)data) / 5; /* cal avgvoltage */
	mutex_unlock(&di->data_lock);

	return (int)data;
}

static int sh366101_read_current(struct sh366101_dev *di)
{
	int ret;
	unsigned short cntl = 0;
	unsigned short data = 0;
	unsigned short temp = 0;
	unsigned char buf[SH366101_CALIINFO_LEN];

	ret = sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, &temp);
	if (ret < 0) {
		hwlog_err("sh366101_read_current error, ret=%d\n", ret);
		return ret;
	}

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_CNTL], &cntl);
	if (ret < 0) {
		hwlog_err("sh366101_read_cntl error, ret=%d\n", ret);
		return ret;
	}

	if ((temp & SH366101_CMD_MASK_OEM_CALI) == SH366101_CMD_MASK_OEM_CALI) {
		ret = sh366101_read_block(di, SH366101_CMD_READ_CALICURRENT, SH366101_CALIINFO_LEN, buf);
		if (ret < 0) {
			hwlog_err("fgcali_read_current read error, ret=%d\n", ret);
			return ret;
		}

		/* get current form byte9 */
		ret = (s16)sh366101_buf2u16_lt(&buf[8]);
	} else {
		ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_CURRENT], &data);
		if (ret < 0) {
			hwlog_err("could not read current, ret=%d\n", ret);
			return ret;
		}
		ret = (s16)data;
	}
	hwlog_info("sh366101_read_current: oemflag=0x%04X, current=%d, cntl=0x%04X\n", temp, ret, cntl);

	return ret;
}

static int sh366101_read_terminate_voltage(struct sh366101_dev* di)
{
	int ret;
	unsigned char buf[SH366101_LEN_TERMINATEVOLT];

	ret = sh366101_read_block(di, SH366101_CMD_TERMINATEVOLT, SH366101_LEN_TERMINATEVOLT, buf);
	if (ret < 0) {
		hwlog_err("could not read terminate voltage, ret=%d\n", ret);
		return ret;
	}

	ret = (int)(buf[0] | ((u16)buf[1] << SH366101_I2C_BLOCK_SHIFT));
	mutex_lock(&di->data_lock);
	di->terminate_voltage = ret * SH366101_MA_TO_UA;
	mutex_unlock(&di->data_lock);

	return 0;
}

static int sh366101_gauge_unseal(struct sh366101_dev* di)
{
	int ret;
	unsigned short flag = 0;

	ret = sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, (u16)SH366101_CMD_UNSEALKEY);
	if (ret < 0)
		goto sh366101_gauge_unseal_End;
	msleep(SH366101_CMD_SBS_DELAY);

	ret = sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, (u16)(SH366101_CMD_UNSEALKEY >> SH366101_CMD_KEY_SHIFT));
	if (ret < 0)
		goto sh366101_gauge_unseal_End;
	msleep(SH366101_CMD_SBS_DELAY);

	ret = 0;

sh366101_gauge_unseal_End:
	ret = sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, &flag);
	if (ret >= 0)
		ret = ((flag & SH366101_CMD_MASK_OEM_UNSEAL) == 0) ? 0 : -1; /* -1 means gauge unseal fail */

	return ret;
}

static int sh366101_gauge_seal(struct sh366101_dev* di)
{
	return sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, SH366101_CMD_SEAL);
}

static int sh366101_read_fcc(struct sh366101_dev *di)
{
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_BAT_FCC], &data);
	if (ret < 0) {
		hwlog_err("could not read fcc, ret=%d\n", ret);
		return ret;
	}
	mutex_lock(&di->data_lock);
	di->batt_fcc = (int)((s16)data * SH366101_MA_TO_UA);
	mutex_unlock(&di->data_lock);

	return di->batt_fcc;
}

static int sh366101_get_cycle(struct sh366101_dev *di)
{
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_SOC_CYCLE], &data);
	if (ret < 0) {
		hwlog_err("read cycle reg fail ret=%d\n", ret);
		return 0;
	}

	mutex_lock(&di->data_lock);
	di->batt_soc_cycle = data;
	mutex_unlock(&di->data_lock);

	return di->batt_soc_cycle;
}

static int sh366101_is_ready(void *dev_data)
{
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;

	/* sh366101 is ok */
	return 1;
}

static int sh366101_is_battery_exist(void *dev_data)
{
	int temp = 0;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;

	temp = sh36610_read_temperature(di, SH366101_TEMPERATURE_EX);
	if ((temp <= SH366101_TEMP_ABR_LOW) || (temp >= SH366101_TEMP_ABR_HIGH))
		return 0;

	/* battery is existing */
	return 1;
}

static int sh366101_read_battery_soc(void *dev_data)
{
	int soc = 0;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;

	soc = sh366101_read_soc(di);

	return soc;
}

static int sh366101_read_battery_vol(void *dev_data)
{
	int voltage = 0;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;
	voltage = sh366101_read_volt(di);

	return voltage;
}

static int sh366101_read_battery_current(void *dev_data)
{
	int cur = 0;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;
	cur = sh366101_read_current(di);

	return cur;
}

static int sh366101_read_battery_temperature(void *dev_data)
{
	int temp = 0;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;
	temp = sh36610_read_temperature(di, SH366101_TEMPERATURE_EX);

	return temp;
}

static int sh366101_read_battery_fcc(void *dev_data)
{
	int fcc;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;
	fcc = sh366101_read_fcc(di) / SH366101_UA_PER_MA;

	return fcc;
}

static int sh366101_read_battery_cycle(void *dev_data)
{
	int cycle;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return 0;
	cycle = sh366101_get_cycle(di);

	return cycle;
}

static int sh366101_set_battery_low_voltage(int vol, void *dev_data)
{
	struct sh366101_dev *di = dev_data;

	if (!di)
		return -EPERM;

	return 0;
}

static int sh366101_set_last_capacity(int capacity, void *dev_data)
{
	struct sh366101_dev *di = dev_data;
	unsigned char soc[SH366101_LEN_USERBUFFER] = {0};

	if (!di)
		return 0;

	soc[0] = (u8)capacity;
	(void)sh366101_set_user_info(di->dev, SH366101_LEN_USERBUFFER, soc);
	hwlog_info("sh366101_set_last_capacity=%d\n", soc[0]);

	return 0;
}

static int sh366101_get_last_capacity(void *dev_data)
{
	int cap;
	int last_cap;
	struct sh366101_dev *di = dev_data;
	unsigned char soc[SH366101_LEN_USERBUFFER] = { 0 };

	if (!di)
		return -EPERM;

	(void)sh366101_get_user_info(di->dev, SH366101_LEN_USERBUFFER, soc);
	hwlog_info("sh366101_get_last_capacity=%d\n", soc[0]);
	cap = sh366101_read_soc(di);
	last_cap = (int)soc[0];

	if ((last_cap <= 0) || (cap <= 0))
		return cap;

	if (abs(last_cap - cap) >= SH366301_CAPACITY_TH)
		return cap;

	return (int)soc[0];
}

static int sh366101_read_battery_rm(void *dev_data)
{
	int ret;
	int fcc;
	int capacity;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return -EPERM;

	capacity = sh366101_read_soc(di);
	fcc = sh366101_read_fcc(di);
	/* get percentage */
	ret = capacity * fcc / SH366101_FULL_CAPACITY;

	return ret;
}

static int sh366101_read_battery_charge_counter(void *dev_data)
{
	int ret;
	int fcc;
	int capacity;
	struct sh366101_dev *di = dev_data;

	if (!di)
		return -EPERM;

	capacity = sh366101_read_soc(di);
	fcc = sh366101_read_fcc(di);
	/* get percentage */
	ret = capacity * fcc / SH366101_FULL_CAPACITY;

	return ret;
}

static int sh366101_set_vterm_dec(int vterm, void *dev_data)
{
	struct sh366101_dev *di = dev_data;

	if (!di)
		return -EPERM;

	return -EPERM;
}

static const char *sh366101_get_coul_model(void *dev_data)
{
	return "sh36xx";
}

static int sh366101_read_rmc(struct sh366101_dev* di)
{
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_BAT_RMC], &data);
	if (ret < 0) {
		hwlog_err("could not read rmc, ret=%d\n", ret);
		return ret;
	}

	mutex_lock(&di->data_lock);
	di->batt_rmc = (int)((s16)data * SH366101_MA_TO_UA);
	mutex_unlock(&di->data_lock);

	return 0;
}

static int sh366101_read_designcap(struct sh366101_dev* di)
{
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_DESIGN_CAPCITY], &data);
	if (ret < 0) {
		hwlog_err("could not read design cap, ret=%d\n", ret);
		return ret;
	}

	mutex_lock(&di->data_lock);
	di->batt_designcap = (int)((s16)data * SH366101_MA_TO_UA);
	mutex_unlock(&di->data_lock);

	return 0;
}

static void sh366101_read_ocv(struct sh366101_dev* di)
{
	int ret;
	unsigned short data = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_OCV], &data);
	if (ret < 0) {
		hwlog_err("could not read ocv, ret=%d\n", ret);
		return;
	}

	mutex_lock(&di->data_lock);
	di->batt_ocv = (int)((s16)data * SH366101_MA_TO_UA);
	mutex_unlock(&di->data_lock);
}

static int sh366101_read_status(struct sh366101_dev* di)
{
	int ret;
	unsigned short flags1 = 0;
	unsigned short cntl = 0;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_CNTL], &cntl);
	if (ret < 0)
		return ret;

	ret = sh366101_read_sbs_word(di, di->regs[SH366101_REG_STATUS], &flags1);
	if (ret < 0)
		return ret;

	hwlog_info("cntl=0x%04x, bat_flags=0x%04x\n", cntl, flags1);
	mutex_lock(&di->data_lock);
	di->batt_present = !!(flags1 & SH366101_STATUS_BATT_PRESENT);
	di->batt_ot = !!(flags1 & SH366101_STATUS_HIGH_TEMPERATURE);
	di->batt_ut = !!(flags1 & SH366101_STATUS_LOW_TEMPERATURE);
	di->batt_tc = !!(flags1 & SH366101_STATUS_TERM_SOC);
	di->batt_fc = !!(flags1 & SH366101_STATUS_FULL_SOC);
	di->batt_soc1 = !!(flags1 & SH366101_STATUS_LOW_SOC2);
	di->batt_socp = !!(flags1 & SH366101_STATUS_LOW_SOC1);
	di->batt_dsg = !!(flags1 & SH366101_OP_STATUS_CHG_DISCHG);
	mutex_unlock(&di->data_lock);

	return 0;
}

static int sh366101_read_gaugeinfo_block0(struct sh366101_dev* di, unsigned char *buf,
	char *str, unsigned int time, long long tick)
{
	int i;
	int ret;

	/* cali Info */
	ret = sh366101_read_block(di, SH366101_CMD_CALIINFO, SH366101_CALIINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read caliinfo, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "elasp=%d, ", tick);
	i += sprintf(&str[i], "voltage=%d, ", (s16)sh366101_buf2u16_lt(&buf[12]));
	i += sprintf(&str[i], "current=%d, ", (s16)sh366101_buf2u32_lt(&buf[8]));
	i += sprintf(&str[i], "ts1 temp=%d, ", (s16)(sh366101_buf2u16_lt(&buf[22]) - SH366101_TEMPER_OFFSET));
	i += sprintf(&str[i], "int temper=%d, ", (s16)(sh366101_buf2u16_lt(&buf[18]) - SH366101_TEMPER_OFFSET));

	return i;
}

static int sh366101_read_gaugeinfo_block1(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i = 0;
	int ret;
	unsigned short temp = 0;

	if (sh366101_read_block(di, SH366101_CMD_CHARGESTATUS, SH366101_LEN_CHARGESTATUS, buf) < 0) {
		hwlog_err("sh366101_gaugelog: could not read chargestatus\n");
		return -EINVAL;
	}
	ret = sh366101_buf2u16_bg(&buf[1]) | (((u32)buf[0]) << SH366101_BUF2U16_SHIFT);
	memset(str, 0, SH366101_GAUGESTR_LEN);
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "chg status=0x%06x, ", (u32)ret & 0xFFFFFF);
	i += sprintf(&str[i], "degrade flag=0x%08x, ", sh366101_buf2u32_bg(&buf[3]));

	/* term volt */
	if (sh366101_read_block(di, SH366101_CMD_TERMINATEVOLT, SH366101_LEN_TERMINATEVOLT, buf) < 0) {
		hwlog_err("sh366101_gaugelog: could not read terminatevolt\n");
		return -EINVAL;
	}

	i += sprintf(&str[i], "term volt=%d, ", sh366101_buf2u16_lt(&buf[0]));
	i += sprintf(&str[i], "term volttime=%d, ", buf[2]);

	if (sh366101_read_sbs_word(di, SH366101_CMD_CONTROLSTATUS, &temp) < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_controlstatus\n");
		return -EINVAL;
	}
	i += sprintf(&str[i], "control status=0x%04x, ", temp);

	if (sh366101_read_sbs_word(di, SH366101_CMD_RUNFLAG, &temp) < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_runflag\n");
		return -EINVAL;
	}
	i += sprintf(&str[i], "flags=0x%04X, ", temp);

	if (sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, &temp) < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_oemflag\n");
		return -EINVAL;
	}
	i += sprintf(&str[i], "oem flag=0x%04x, ", temp);

	hwlog_info("sh366101_gaugelog: chargestatus is %s\n", str);

	/* lifetime info. */
	if (sh366101_read_block(di, SH366101_CMD_LIFETIMEADC, SH366101_LEN_LIFETIMEADC, buf) < 0) {
		hwlog_err("sh366101_gaugelog: could not read lifetimeadc\n");
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "lt_max volt=%d, ", (s16)sh366101_buf2u16_bg(&buf[0]));
	i += sprintf(&str[i], "lt_min volt=%d, ", (s16)sh366101_buf2u16_bg(&buf[2]));
	i += sprintf(&str[i], "lt_max chg cur=%d, ", (s16)sh366101_buf2u16_bg(&buf[4]));
	i += sprintf(&str[i], "lt_max dsg cur=%d, ", (s16)sh366101_buf2u16_bg(&buf[6]));
	i += sprintf(&str[i], "lt_max temper=%d, ", (s8)buf[8]);
	i += sprintf(&str[i], "lt_min temper=%d, ", (s8)buf[9]);
	i += sprintf(&str[i], "lt_max int temper=%d, ", (s8)buf[10]);
	i += sprintf(&str[i], "lt_min int temper=%d, ", (s8)buf[11]);

	return i;
}

static int sh366101_read_gaugeinfo_block2(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i;

	hwlog_info("sh366101_gaugelog: lifetimeadc is %s\n", str);

	/* gauge info */
	if (sh366101_read_block(di, SH366101_CMD_GAUGEINFO, SH366101_GAUGEINFO_LEN, buf) < 0) {
		hwlog_err("sh366101_gaugelog: could not read gaugeinfo\n");
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "run state=0x%08x, ", sh366101_buf2u32_lt(&buf[0]));
	i += sprintf(&str[i], "gauge state=0x%08x, ", sh366101_buf2u32_lt(&buf[4]));
	i += sprintf(&str[i], "gauge s2=0x%04x, ", sh366101_buf2u16_lt(&buf[8]));
	i += sprintf(&str[i], "work state=0x%04x, ", sh366101_buf2u16_lt(&buf[10]));
	i += sprintf(&str[i], "time inc=%d, ", buf[12]);
	i += sprintf(&str[i], "main tick=%d, ", buf[13]);
	i += sprintf(&str[i], "sys tick=%d, ", buf[14]);
	i += sprintf(&str[i], "clock h=%d, ", sh366101_buf2u16_lt(&buf[15]));
	i += sprintf(&str[i], "ram checkt=%d, ", buf[17]);
	i += sprintf(&str[i], "auto calit=%d, ", buf[18]);
	i += sprintf(&str[i], "lt hour=%d, ", buf[19]);
	i += sprintf(&str[i], "lt timer=%d, ", buf[20]);
	i += sprintf(&str[i], "flash t=%d, ", buf[21]);
	i += sprintf(&str[i], "lt flag=0x%02x, ", buf[22]);
	i += sprintf(&str[i], "rsts=0x%02x, ", buf[23]);

	return i;
}

static int sh366101_read_gaugeinfo_block3(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i;
	int ret;

	hwlog_info("sh366101_gaugelog: sh366101_cmd_gaugeinfo is %s\n", str);

	ret = sh366101_read_block(di, SH366101_CMD_GAUGEBLOCK2, SH366101_GAUGEINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_gaugeblock2, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "qf1=0x%02x, ", buf[0]);
	i += sprintf(&str[i], "qf2=0x%02x, ", buf[1]);
	i += sprintf(&str[i], "pack qmax=%d, ", (s16)sh366101_buf2u16_bg(&buf[2]));
	i += sprintf(&str[i], "cycle count=%d, ", sh366101_buf2u16_bg(&buf[14]));
	i += sprintf(&str[i], "qmax count=%d, ", sh366101_buf2u16_bg(&buf[16]));
	i += sprintf(&str[i], "qmax cycle=%d, ", sh366101_buf2u16_bg(&buf[18]));
	i += sprintf(&str[i], "vat eoc=%d, ", (s16)sh366101_buf2u16_bg(&buf[4]));
	i += sprintf(&str[i], "iat eoc=%d, ", (s16)sh366101_buf2u16_bg(&buf[6]));
	i += sprintf(&str[i], "chg veoc=%d, ", (s16)sh366101_buf2u16_bg(&buf[8]));
	i += sprintf(&str[i], "avilr=%d, ", (s16)sh366101_buf2u16_bg(&buf[10]));
	i += sprintf(&str[i], "avplr=%d, ", (s16)sh366101_buf2u16_bg(&buf[12]));
	i += sprintf(&str[i], "model count=%d, ", sh366101_buf2u16_bg(&buf[20]));
	i += sprintf(&str[i], "model cycle=%d, ", sh366101_buf2u16_bg(&buf[22]));
	i += sprintf(&str[i], "vct count=%d, ", sh366101_buf2u16_bg(&buf[24]));
	i += sprintf(&str[i], "vct cycle=%d, ", sh366101_buf2u16_bg(&buf[26]));
	i += sprintf(&str[i], "relax cycle=%d, ", sh366101_buf2u16_bg(&buf[28]));
	i += sprintf(&str[i], "ratio cycle=%d, ", sh366101_buf2u16_bg(&buf[30]));

	return i;
}

static int sh366101_read_gaugeinfo_block4(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i;
	int ret;

	ret = sh366101_read_block(di, SH366101_CMD_GAUGEBLOCK3, SH366101_GAUGEINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_gaugeblock3, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "sfr_rc=%d, ", (s16)sh366101_buf2u16_bg(&buf[0]));
	i += sprintf(&str[i], "sfr_dcc=%d, ", (s16)sh366101_buf2u16_bg(&buf[2]));
	i += sprintf(&str[i], "sfr_icc=%d, ", (s16)sh366101_buf2u16_bg(&buf[4]));
	i += sprintf(&str[i], "rcoffset=%d, ", (s16)sh366101_buf2u16_bg(&buf[6]));
	i += sprintf(&str[i], "c0dod1=%d, ", (s16)sh366101_buf2u16_bg(&buf[8]));
	i += sprintf(&str[i], "pas col=%d, ", (s16)sh366101_buf2u16_bg(&buf[10]));
	i += sprintf(&str[i], "pas egy=%d, ", (s16)sh366101_buf2u16_bg(&buf[12]));
	i += sprintf(&str[i], "qstart=%d, ", (s16)sh366101_buf2u16_bg(&buf[14]));
	i += sprintf(&str[i], "estart=%d, ", (s16)sh366101_buf2u16_bg(&buf[16]));
	i += sprintf(&str[i], "fast tim=%d, ", buf[18]);
	i += sprintf(&str[i], "filflg=0x%02X, ", buf[19]);
	i += sprintf(&str[i], "state time=%d, ", sh366101_buf2u32_bg(&buf[20]));
	i += sprintf(&str[i], "state hour=%d, ", sh366101_buf2u16_bg(&buf[24]));
	i += sprintf(&str[i], "state sec=%d, ", sh366101_buf2u16_bg(&buf[26]));
	i += sprintf(&str[i], "ocv tim=%d, ", sh366101_buf2u16_bg(&buf[28]));
	i += sprintf(&str[i], "ra cal t1=%d, ", sh366101_buf2u16_bg(&buf[30]));

	return i;
}

static int sh366101_read_gaugeinfo_block5(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i;
	int ret;
	ret = sh366101_read_block(di, SH366101_CMD_GAUGEBLOCK4, SH366101_GAUGEINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_gaugeblock4, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "gu inx=0x%02x, ", buf[0]);
	i += sprintf(&str[i], "gu load=0x%02x, ", buf[1]);
	i += sprintf(&str[i], "gu status=0x%08x, ", sh366101_buf2u32_bg(&buf[2]));
	i += sprintf(&str[i], "eod load=%d, ", (s16)sh366101_buf2u16_bg(&buf[6]));
	i += sprintf(&str[i], "c ratio=%d, ", (s16)sh366101_buf2u16_bg(&buf[8]));
	i += sprintf(&str[i], "c0 dod0=%d, ", (s16)sh366101_buf2u16_bg(&buf[10]));
	i += sprintf(&str[i], "c0 eoc=%d, ", (s16)sh366101_buf2u16_bg(&buf[12]));
	i += sprintf(&str[i], "c0 eod=%d, ", (s16)sh366101_buf2u16_bg(&buf[14]));
	i += sprintf(&str[i], "c0 acv=%d, ", (s16)sh366101_buf2u16_bg(&buf[16]));
	i += sprintf(&str[i], "them t=%d, ", (s16)sh366101_buf2u16_bg(&buf[18]));
	i += sprintf(&str[i], "told=%d, ", (s16)sh366101_buf2u16_bg(&buf[20]));
	i += sprintf(&str[i], "tout=%d, ", (s16)sh366101_buf2u16_bg(&buf[22]));
	i += sprintf(&str[i], "rc raw=%d, ", (s16)sh366101_buf2u16_bg(&buf[24]));
	i += sprintf(&str[i], "fc craw=%d, ", (s16)sh366101_buf2u16_bg(&buf[26]));
	i += sprintf(&str[i], "re raw=%d, ", (s16)sh366101_buf2u16_bg(&buf[28]));
	i += sprintf(&str[i], "fce raw=%d, ", (s16)sh366101_buf2u16_bg(&buf[30]));

	return i;
}

static int sh366101_read_gaugeinfo_block6(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i;
	int ret;

	ret = sh366101_read_block(di, SH366101_CMD_GAUGEBLOCK5, SH366101_GAUGEINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_gaugeblock5, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "ideal fcc=%d, ", (s16)sh366101_buf2u16_bg(&buf[0]));
	i += sprintf(&str[i], "ideal fce=%d, ", (s16)sh366101_buf2u16_bg(&buf[2]));
	i += sprintf(&str[i], "fil rc=%d, ", (s16)sh366101_buf2u16_bg(&buf[4]));
	i += sprintf(&str[i], "fil fcc=%d, ", (s16)sh366101_buf2u16_bg(&buf[6]));
	i += sprintf(&str[i], "fsoc=%d, ", buf[8]);
	i += sprintf(&str[i], "true rc=%d, ", (s16)sh366101_buf2u16_bg(&buf[9]));
	i += sprintf(&str[i], "true fcc=%d, ", (s16)sh366101_buf2u16_bg(&buf[11]));
	i += sprintf(&str[i], "rsoc=%d, ", buf[13]);
	i += sprintf(&str[i], "fil re=%d, ", (s16)sh366101_buf2u16_bg(&buf[14]));
	i += sprintf(&str[i], "fil fce=%d, ", (s16)sh366101_buf2u16_bg(&buf[16]));
	i += sprintf(&str[i], "fsocw=%d, ", buf[18]);
	i += sprintf(&str[i], "true re=%d, ", (s16)sh366101_buf2u16_bg(&buf[19]));
	i += sprintf(&str[i], "true fce=%d, ", (s16)sh366101_buf2u16_bg(&buf[21]));
	i += sprintf(&str[i], "rsocw=%d, ", buf[23]);
	i += sprintf(&str[i], "equ rc=%d, ", (s16)sh366101_buf2u16_bg(&buf[24]));
	i += sprintf(&str[i], "equ fcc=%d, ", (s16)sh366101_buf2u16_bg(&buf[26]));
	i += sprintf(&str[i], "equ re=%d, ", (s16)sh366101_buf2u16_bg(&buf[28]));
	i += sprintf(&str[i], "equ fce=%d, ", (s16)sh366101_buf2u16_bg(&buf[30]));

	return i;
}

static int sh366101_read_gaugeinfo_block7(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i;
	int ret;

	ret = sh366101_read_block(di, SH366101_CMD_GAUGEBLOCK6, SH366101_GAUGEINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_gaugeblock6, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "gauge s3=0x%04x, ", sh366101_buf2u16_bg(&buf[0]));
	i += sprintf(&str[i], "ccon1=0x%02x, ", buf[2]);
	i += sprintf(&str[i], "ccon2=0x%02x, ", buf[3]);
	i += sprintf(&str[i], "model s=0x%02x, ", buf[4]);
	i += sprintf(&str[i], "fg update=0x%02x, ", buf[5]);
	i += sprintf(&str[i], "fm grid=0x%02x, ", buf[6]);
	i += sprintf(&str[i], "toggle cnt=%d, ", buf[7]);
	i += sprintf(&str[i], "or update=0x%02x, ", buf[8]);
	i += sprintf(&str[i], "up state=0x%02x, ", buf[9]);
	i += sprintf(&str[i], "chg vol=%d, ", (s16)sh366101_buf2u16_bg(&buf[10]));
	i += sprintf(&str[i], "tap cur=%d, ", (s16)sh366101_buf2u16_bg(&buf[12]));
	i += sprintf(&str[i], "chg cur=%d, ", (s16)sh366101_buf2u16_bg(&buf[14]));
	i += sprintf(&str[i], "chg res=%d, ", (s16)sh366101_buf2u16_bg(&buf[16]));
	i += sprintf(&str[i], "prev i=%d, ", (s16)sh366101_buf2u16_bg(&buf[18]));
	i += sprintf(&str[i], "delta c=%d, ", (s16)sh366101_buf2u16_bg(&buf[20]));
	i += sprintf(&str[i], "socjmp cnt=%d, ", buf[22]);
	i += sprintf(&str[i], "sowjmp cnt=%d, ", buf[23]);
	i += sprintf(&str[i], "ocv vcell=%d, ", (s16)sh366101_buf2u16_bg(&buf[24]));
	i += sprintf(&str[i], "fg meas=%d, ", (s16)sh366101_buf2u16_bg(&buf[26]));
	i += sprintf(&str[i], "fg prid=%d, ", (s16)sh366101_buf2u16_bg(&buf[28]));
	i += sprintf(&str[i], "fast time=%d, ", (s16)sh366101_buf2u16_bg(&buf[30]));

	return i;
}

static int sh366101_read_gaugeinfo_block8(struct sh366101_dev* di, unsigned char *buf, char *str, unsigned int time)
{
	int i;
	int ret;

	/* gauge fusion model */
	ret = sh366101_read_block(di, SH366101_CMD_GAUGEBLOCK_FG, SH366101_GAUGEINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_gaugeblock_fg, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "fusionmodel=");
	for (ret = 0; ret < SH366101_FUSIONMODEL_CNT; ret++)
		i += sprintf(&str[i], "0x%04x ", sh366101_buf2u16_bg(&buf[ret * 2]));

	hwlog_info("sh366101_gaugelog: fusionmodel is %s\n", str);

	/* cadc info */
	ret = sh366101_read_block(di, SH366101_CMD_CADCINFO, SH366101_GAUGEINFO_LEN, buf);
	if (ret < 0) {
		hwlog_err("sh366101_gaugelog: could not read sh366101_cmd_cadcinfo, ret=%d\n", ret);
		return -EINVAL;
	}

	memset(str, 0, SH366101_GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "tick=%d, ", time);
	i += sprintf(&str[i], "teoffset=%d, ", (s16)sh366101_buf2u16_lt(&buf[0]));
	i += sprintf(&str[i], "user offset=%d, ", (s16)sh366101_buf2u16_lt(&buf[2]));
	i += sprintf(&str[i], "board offset=%d, ", (s16)sh366101_buf2u16_lt(&buf[4]));
	i += sprintf(&str[i], "cadc25deg=%d, ", (s16)sh366101_buf2u16_lt(&buf[6]));
	i += sprintf(&str[i], "cadckr=%d, ", (s16)sh366101_buf2u16_lt(&buf[8]));
	i += sprintf(&str[i], "chosen offset=%d, ", (s16)sh366101_buf2u16_lt(&buf[10]));
	i += sprintf(&str[i], "te liner=%d, ", (s16)sh366101_buf2u16_lt(&buf[12]));
	i += sprintf(&str[i], "user liner=%d, ", (s16)sh366101_buf2u16_lt(&buf[14]));
	i += sprintf(&str[i], "cur liner=%d, ", (s16)sh366101_buf2u16_lt(&buf[16]));
	i += sprintf(&str[i], "cor=%d, ", (s16)sh366101_buf2u16_lt(&buf[18]));
	i += sprintf(&str[i], "cadc=%d, ", (int)sh366101_buf2u32_lt(&buf[20]));
	i += sprintf(&str[i], "current=%d, ", (s16)sh366101_buf2u16_lt(&buf[24]));
	i += sprintf(&str[i], "pack config=0x%04x, ", sh366101_buf2u16_lt(&buf[26]));

	return i;
}

static void sh366101_time_check(unsigned long long jiffies_now, long long *tick, struct sh366101_dev* di)
{
	if (*tick < 0) {
		*tick = (s64)(SH366101_U64_MAXVALUE - (u64)di->log_last_update);
		/* get next tick */
		*tick += jiffies_now + 1;
		hwlog_err("sh366101_gaugelog: tick < 0\n");
	}

	*tick /= HZ;
}

static void sh366101_read_gaugeinfo_block(struct sh366101_dev* di)
{
	static unsigned char buf[SH366101_GAUGEINFO_LEN];
	static char str[SH366101_GAUGESTR_LEN];

	unsigned long long jiffies_now = get_jiffies_64();
	long long tick = (s64)(jiffies_now - di->log_last_update);

	sh366101_time_check(jiffies_now, &tick, di);

	if (tick < SH366101_GAUGE_LOG_MIN_TIMESPAN) {
		hwlog_err("sh366101_gaugelog: tick < sh366101_gauge_log_min_timespan\n");
		return;
	}

	if (!mutex_trylock(&di->cali_lock)) {
		hwlog_err("sh366101_gaugelog: could not get mutex\n");
		return;
	}

	di->log_last_update = jiffies_now;

	if (sh366101_read_gaugeinfo_block0(di, buf, str, (u32)jiffies_now, tick) < 0)
		goto fg_read_gaugeinfo_block_end;

	hwlog_info("sh366101_gaugelog: sh366101_cmd_caliinfo is %s\n", str);

	if (sh366101_read_gaugeinfo_block1(di, buf, str, (u32)jiffies_now) < 0)
		goto fg_read_gaugeinfo_block_end;

	if (sh366101_read_gaugeinfo_block2(di, buf, str, (u32)jiffies_now) < 0)
		goto fg_read_gaugeinfo_block_end;

	if (sh366101_read_gaugeinfo_block3(di, buf, str, (u32)jiffies_now) < 0)
		goto fg_read_gaugeinfo_block_end;

	hwlog_info("sh366101_gaugelog: gaugeblock2 is %s\n", str);

	if (sh366101_read_gaugeinfo_block4(di, buf, str, (u32)jiffies_now) < 0)
		goto fg_read_gaugeinfo_block_end;

	hwlog_info("sh366101_gaugelog: gaugeblock3 is %s\n", str);

	if (sh366101_read_gaugeinfo_block5(di, buf, str, (u32)jiffies_now) < 0)
		goto fg_read_gaugeinfo_block_end;

	hwlog_info("sh366101_gaugelog: gaugeblock4 is %s\n", str);

	if (sh366101_read_gaugeinfo_block6(di, buf, str, (u32)jiffies_now) < 0)
		goto fg_read_gaugeinfo_block_end;

	hwlog_info("sh366101_gaugelog: gaugeblock5 is %s\n", str);

	if (sh366101_read_gaugeinfo_block7(di, buf, str, (u32)jiffies_now) < 0)
		goto fg_read_gaugeinfo_block_end;

	hwlog_info("sh366101_gaugelog: gaugeblock6 is %s\n", str);

	sh366101_read_gaugeinfo_block8(di, buf, str, (u32)jiffies_now);

	hwlog_info("sh366101_gaugelog: cadcinfo is %s\n", str);

fg_read_gaugeinfo_block_end:
	mutex_unlock(&di->cali_lock);
}

static void sh366101_refresh_status(struct sh366101_dev* di)
{
	bool last_batt_inserted = di->batt_present;
	static int last_soc, last_temp;

	(void)sh366101_read_status(di);

	if (!last_batt_inserted && di->batt_present) { /* battery inserted */
		hwlog_info("battery inserted\n");
	} else if (last_batt_inserted && !di->batt_present) { /* battery removed */
		hwlog_info("battery removed\n");
		di->batt_soc = -ENODATA;
		di->batt_fcc = -ENODATA;
		di->batt_volt = -ENODATA;
		di->batt_curr = -ENODATA;
		di->batt_temp = -ENODATA;
	}

	if (di->batt_present) {
		sh366101_read_gaugeinfo_block(di);

		sh366101_read_battery_current(di);
		sh366101_read_soc(di);
		sh366101_read_ocv(di);
		sh366101_read_volt(di);
		sh366101_get_cycle(di);
		sh366101_read_rmc(di);
		sh366101_read_fcc(di);
		sh366101_read_designcap(di);
		if (di->en_temp_in)
			sh36610_read_temperature(di, SH366101_TEMPERATURE_IN);
		else if (di->en_temp_ex)
			sh36610_read_temperature(di, SH366101_TEMPERATURE_EX);

		hwlog_info("rsoc:%d, volt:%d, current:%d, temperature:%d\n",
			di->batt_soc, di->batt_volt, di->batt_curr, di->batt_temp);
		hwlog_info("rm:%d, fc:%d, fast:%d\n", di->batt_rmc, di->batt_fcc, di->fast_mode);

		last_soc = di->batt_soc;
		last_temp = di->batt_temp;
	}

	di->last_update = jiffies;
}

static struct coul_interface_ops sh366101_ops = {
	.type_name = "main",
	.is_coul_ready = sh366101_is_ready,
	.is_battery_exist = sh366101_is_battery_exist,
	.get_battery_capacity = sh366101_read_battery_soc,
	.get_battery_voltage = sh366101_read_battery_vol,
	.get_battery_current = sh366101_read_battery_current,
	.get_battery_avg_current = sh366101_read_battery_current,
	.get_battery_temperature = sh366101_read_battery_temperature,
	.get_battery_fcc = sh366101_read_battery_fcc,
	.get_battery_cycle = sh366101_read_battery_cycle,
	.set_battery_low_voltage = sh366101_set_battery_low_voltage,
	.set_battery_last_capacity = sh366101_set_last_capacity,
	.get_battery_last_capacity = sh366101_get_last_capacity,
	.get_battery_rm = sh366101_read_battery_rm,
	.get_battery_charge_counter = sh366101_read_battery_charge_counter,
	.set_vterm_dec = sh366101_set_vterm_dec,
	.get_coul_model = sh366101_get_coul_model,
};

static struct coul_interface_ops sh366101_aux_ops = {
	.type_name = "aux",
	.is_coul_ready = sh366101_is_ready,
	.is_battery_exist = sh366101_is_battery_exist,
	.get_battery_capacity = sh366101_read_battery_soc,
	.get_battery_voltage = sh366101_read_battery_vol,
	.get_battery_current = sh366101_read_battery_current,
	.get_battery_avg_current = sh366101_read_battery_current,
	.get_battery_temperature = sh366101_read_battery_temperature,
	.get_battery_fcc = sh366101_read_battery_fcc,
	.get_battery_cycle = sh366101_read_battery_cycle,
	.set_battery_low_voltage = sh366101_set_battery_low_voltage,
	.set_battery_last_capacity = sh366101_set_last_capacity,
	.get_battery_last_capacity = sh366101_get_last_capacity,
	.get_battery_rm = sh366101_read_battery_rm,
	.get_battery_charge_counter = sh366101_read_battery_charge_counter,
	.set_vterm_dec = sh366101_set_vterm_dec,
	.get_coul_model = sh366101_get_coul_model,
};

static int sh366101_cali_force_e2rom_update(struct sh366101_dev *di)
{
	unsigned char buf_force_e2rom_update[SH366101_LEN_FORCE_UPDATE_E2ROM] = { 0x45, 0x32, 0x55, 0x50 };
	int ret;

	ret = sh366101_write_block(di, SH366101_CMD_FORCE_UPDATE_E2ROM, SH366101_LEN_FORCE_UPDATE_E2ROM,
		buf_force_e2rom_update);

	msleep(SH366101_DELAY_WRITEE2ROM);

	return ret;
}

static int sh366101_cali_set_e2rom_offset(struct sh366101_dev *di, int current_input)
{
	unsigned char buf[SH366101_RATIO_READ_LEN];
	unsigned short temp = (unsigned short)current_input;
	int ret;

	hwlog_info("fgcali_set_e2rom_offset current_input=%d\n", current_input);

	/* get high 8 bits and low 8 bits */
	buf[0] = (temp >> 8) & 0xFF;
	buf[1] = temp & 0xFF;
	ret = sh366101_write_block(di, SH366101_CMD_E2ROM_BOARDOFFSET, SH366101_RATIO_READ_LEN, buf);
	msleep(SH366101_CMD_SBS_DELAY);

	return ret;
}

static int sh366101_cali_set_e2rom_ratio(struct sh366101_dev *di, unsigned int current_input)
{
	unsigned char buf[SH366101_RATIO_READ_LEN];
	int ret;

	hwlog_info("fgcali_set_e2rom_ratio current_input=%d\n", current_input);

	/* get high 8 bits and low 8 bits */
	buf[0] = (current_input >> 8) & 0xFF;
	buf[1] = current_input & 0xFF;
	ret = sh366101_write_block(di, SH366101_CMD_E2ROM_CURRENTRATIO, SH366101_RATIO_READ_LEN, buf);
	msleep(SH366101_CMD_SBS_DELAY);

	return ret;
}

static int sh366101_cali_set_e2rom_sensor_ratio(struct sh366101_dev* di, unsigned int sensor_input)
{
	int ret;
	unsigned char buf[SH366101_RATIO_READ_LEN];

	/* get high 8 bits and low 8 bits */
	buf[0] = (sensor_input >> 8) & 0xFF;
	buf[1] = sensor_input & 0xFF;
	ret = sh366101_write_block(di, SH366101_CMD_E2ROM_SENSORRATIO, SH366101_RATIO_READ_LEN, buf);
	msleep(SH366101_CMD_SBS_DELAY);

	return ret;
}

static int sh366101_cali_set_currentoffset(struct sh366101_dev* di, int offset_input)
{
	int ret;
	int current_offset = di->current_offset;
	unsigned int ratio =
		(u32)(SH366101_CURRENT_INIT * di->current_ratio * SH366101_RATIO_RATIO / SH366101_DEFAULT_CURRENT_RATIO);
	int offset = (s32)((offset_input + SH366101_OFFSET_INIT) * SH366101_OFFSET_RATIO1 / SH366101_OFFSET_RATIO2);
	int is_third_write = 0;
	unsigned int sensor_ratio = SH366101_DEFAULT_SENSOR_RATIO;
	int current_ratio_pre = di->current_ratio_pre;

	di->current_ratio_pre = di->current_ratio;
	di->current_offset = offset_input;

	hwlog_info("sh366101_cali_set_currentoffset: offset=%d, offset1=%d, ratio=%d, ratio1=%d\n",
		offset_input, current_offset, di->current_ratio, di->current_ratio_pre);
	if ((current_offset == SH366101_DEFAULT_CURRENT_OFFSET) && (di->current_ratio == SH366101_DEFAULT_CURRENT_RATIO)) {
		if (current_ratio_pre != SH366101_DEFAULT_CURRENT_RATIO) {
			ratio = SH366101_RATIO_RATIO;
			offset = (int)((s64)SH366101_OFFSET_INIT * SH366101_OFFSET_RATIO1 / SH366101_OFFSET_RATIO2);
		}
	} else {
		/* not first calibration */
		is_third_write = 1;
		ratio = (u32)((u64)ratio * SH366101_DEFAULT_SENSOR_RATIO / SH366101_USER_SENSOR_RATIO);
		sensor_ratio = SH366101_USER_SENSOR_RATIO;
	}

	ret = sh366101_cali_set_e2rom_ratio(di, ratio);
	if (ret < 0) {
		hwlog_err("sh366101_cali_set_currentoffset error, cannot write offset\n");
		goto sh366101_cali_set_currentoffset_end;
	}

	ret = sh366101_cali_set_e2rom_offset(di, offset);
	if (ret < 0) {
		hwlog_err("sh366101_cali_set_currentoffset error, cannot write offset\n");
		goto sh366101_cali_set_currentoffset_end;
	}

	ret = sh366101_cali_set_e2rom_sensor_ratio(di, sensor_ratio);
	if (ret < 0) {
		hwlog_err("sh366101_cali_set_currentoffset error, cannot write offset\n");
		goto sh366101_cali_set_currentoffset_end;
	}

	ret = sh366101_cali_force_e2rom_update(di);
	if (ret < 0) {
		hwlog_err("sh366101_cali_set_currentoffset error, cannot update e2rom\n");
		goto sh366101_cali_set_currentoffset_end;
	}

sh366101_cali_set_currentoffset_end:
	if ((ret < 0) || (is_third_write != 0)) {
		di->current_ratio = 0;
		di->current_ratio_pre = 0;
		di->current_offset = 0;
	}
	return ret;
}

static int sh366101_enter_calibration_mode(struct sh366101_dev* di)
{
	int retry;
	int ret;
	unsigned short flag = 0;
	unsigned char buf[SH366101_BLOCK_LEN_MAX];
	unsigned char bufcmd[SH366101_LEN_ENTER_CALI] = { 0x45, 0x54, 0x43, 0x41, 0x4C, 0x49 };
	unsigned char bufoem[SH366101_LEN_ENTER_OEM] = { 0x65, 0x4E, 0x54, 0x45, 0x52, 0x4F, 0x45, 0x4D };

	for (retry = 0; retry < SH366101_GAUGE_RUNSTATE_RETRY; retry++) {
		ret = sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, &flag);
		msleep(SH366101_CALI_WAIT_TIME);
		if (ret >= 0) {
			if ((flag & SH366101_CMD_MASK_OEM_CALI) == SH366101_CMD_MASK_OEM_CALI) {
				ret = 0;
				goto sh366101_check_calibration_mode_end;
			}
			ret = sh366101_gauge_unseal(di);
			if (ret < 0)
				continue;

			msleep(SH366101_CMD_SBS_DELAY);
			ret = sh366101_read_block(di, SH366101_CMD_E2ROM_DEADBAND, SH366101_BLOCK_LEN_MAX, buf);
			if (ret < 0)
				continue;
			di->deadband = buf[0];
			buf[0] = 0;
			sh366101_write_block(di, SH366101_CMD_E2ROM_DEADBAND, SH366101_CMD_LEN, buf);

			ret = sh366101_write_block(di, SH366101_CMD_ENTER_OEM, SH366101_LEN_ENTER_OEM, bufoem);
			if (ret < 0)
				continue;

			msleep(SH366101_CMD_SBS_DELAY);
			ret = sh366101_write_block(di, SH366101_CMD_ENTER_CALI, SH366101_LEN_ENTER_CALI, bufcmd);
			if (ret >= 0) {
				ret = sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, &flag);
				break;
			}
		}
	}

sh366101_check_calibration_mode_end:
	ret = sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, &flag);
	if (ret >= 0)
		ret = ((flag & SH366101_CMD_MASK_OEM_CALI) == SH366101_CMD_MASK_OEM_CALI) ? 0 : -1; /* -1 means enter calibration mode fail */
	hwlog_info("fg_enter_calibration_mode: ret=%d, oemflag=0x%04x\n", ret, flag);

	return ret;
}

static int sh366101_exit_calibration_mode(struct sh366101_dev* di)
{
	int ret;
	int retry;
	unsigned char buf[1];
	unsigned short flag = 0;
	unsigned char bufcmd[SH366101_LEN_EXIT_CALI] = { 0x65, 0x78, 0x63, 0x61, 0x6C, 0x69 };
	unsigned char bufoem[SH366101_LEN_EXIT_OEM] = { 0x45, 0x78, 0x69, 0x74, 0x6f, 0x65, 0x6d };

	for (retry = 0; retry < SH366101_COMPARE_RETRY_CNT; retry++) {
		buf[0] = di->deadband;
		ret = sh366101_write_block(di, SH366101_CMD_E2ROM_DEADBAND, SH366101_CMD_LEN, buf);
		if (ret < 0)
			continue;
		ret = sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, &flag);
		msleep(SH366101_CMD_SBS_DELAY);
		if (ret >= 0) {
			if ((flag & SH366101_CMD_MASK_OEM_CALI) == 0) {
				ret = 0;
				goto sh366101_check_calibration_mode_end;
			}

			ret = sh366101_write_block(di, SH366101_CMD_EXIT_CALI, SH366101_LEN_EXIT_CALI, bufcmd);
			msleep(SH366101_CMD_SBS_DELAY);
			ret |= sh366101_cali_force_e2rom_update(di);
			ret |= sh366101_write_block(di, SH366101_CMD_EXIT_OEM, SH366101_LEN_EXIT_OEM, bufoem);
			if (ret < 0)
				continue;
		}
		msleep(SH366101_CALI_WAIT_TIME);
	}
	ret = 0;

sh366101_check_calibration_mode_end:
	hwlog_info("fg_exit_calibration_mode: ret=%d, oemflag=0x%04x\n", ret, flag);
	return ret;
}

static int sh366101_enable_cali_mode(int enable, void *dev_data)
{
	struct sh366101_dev* di = dev_data;
	int ret = -EPERM;

	if (!di)
		return ret;

	if (enable)
		ret = sh366101_enter_calibration_mode(di);
	else
		ret = sh366101_exit_calibration_mode(di);

	hwlog_info("sh366101_enable_cali_mode: %d, ret=%d\n", enable, ret);

	return ret;
}

static int sh366101_cali_set_current(struct sh366101_dev* di, int ratio_input)
{
	di->current_ratio = ratio_input;

	return 0;
}

static int sh366101_get_calibration_curr(int *val, void *dev_data)
{
	struct sh366101_dev *di = dev_data;

	if (!di || !val) {
		hwlog_err("di or val is null\n");
		return -EPERM;
	}

	*val = sh366101_read_battery_current(di);

	return 0;
}

static int sh366101_get_calibration_vol(int *val, void *dev_data)
{
	int ret;
	struct sh366101_dev *di = dev_data;

	if (!di || !val) {
		hwlog_err("di or val is null\n");
		return -EPERM;
	}

	ret = sh366101_read_volt(di);
	if (ret < 0)
		return ret;
	*val = ret;
	*val *= POWER_UV_PER_MV;

	return 0;
}

static int sh366101_set_current_gain(unsigned int val, void *dev_data)
{
	struct sh366101_dev *di = dev_data;

	if (!di || !val) {
		hwlog_err("di or val is null\n");
		return -EPERM;
	}
	sh366101_cali_set_current(di, val);
	hwlog_info("sh366101_set_current_gain val=%d\n", val);

	return 0;
}

static int sh366101_set_voltage_gain(unsigned int val, void *dev_data)
{
	return 0;
}

static int sh366101_set_current_offset(int val, void *dev_data)
{
	struct sh366101_dev *di = dev_data;

	if (!di)
		return -EPERM;
	hwlog_info("sh366101_set_current_offset val=%d\n", val);
	sh366101_cali_set_currentoffset(di, val);

	return 0;
}

static void sh366101_bat_work(struct work_struct *work)
{
	struct delayed_work *delay_work = NULL;
	struct sh366101_dev *di = NULL;

	delay_work = container_of(work, struct delayed_work, work);
	if (!delay_work)
		return;
	di = container_of(delay_work, struct sh366101_dev, battery_delay_work);
	if (!di)
		return;

	di->voltage = sh366101_read_battery_vol(di);
	di->ui_soc = sh366101_read_battery_soc(di);
	di->curr = sh366101_read_battery_current(di);
	di->temp = sh366101_read_battery_temperature(di);
	di->cycle = sh366101_read_battery_cycle(di);
	hwlog_info("vol=%d cap=%d current=%d temp=%d cycle=%d\n",
		di->voltage, di->ui_soc, di->curr, di->temp, di->cycle);

	sh366101_refresh_status(di);
	queue_delayed_work(di->shfg_workqueue, &di->battery_delay_work,
		msecs_to_jiffies(SH366101_QUEUE_DELAYED_WORK_TIME));
}


static struct coul_cali_ops sh366101_cali_ops = {
	.dev_name = "aux",
	.get_cali_current = sh366101_get_calibration_curr,
	.get_cali_voltage = sh366101_get_calibration_vol,
	.set_current_offset = sh366101_set_current_offset,
	.set_current_gain = sh366101_set_current_gain,
	.set_voltage_gain = sh366101_set_voltage_gain,
	.set_cali_mode = sh366101_enable_cali_mode,
};

/* main battery gauge use aux calibration data for compatible */
static struct coul_cali_ops sh366101_aux_cali_ops = {
	.dev_name = "main",
	.get_cali_current = sh366101_get_calibration_curr,
	.get_cali_voltage = sh366101_get_calibration_vol,
	.set_current_offset = sh366101_set_current_offset,
	.set_current_gain = sh366101_set_current_gain,
	.set_voltage_gain = sh366101_set_voltage_gain,
	.set_cali_mode = sh366101_enable_cali_mode,
};

static int sh366101_get_log_head(char *buffer, int size, void *dev_data)
{
	struct sh366101_dev *di = dev_data;
	if (!di || !buffer)
		return -EPERM;
	if (di->ic_role == SH366101_IC_TYPE_MAIN)
		snprintf(buffer, size, "vol    soc    current   temp   cycle   ");
	else
		snprintf(buffer, size, "vol1   soc1   current1  temp1  cycle1  ");

	return 0;
}

static int sh366101_dump_log_data(char *buffer, int size, void *dev_data)
{
	struct sh366101_dev *di = dev_data;
	if (!di || !buffer)
		return -EPERM;

	snprintf(buffer, size, "%-7d%-7d%-10d%-7d%-8d",
		di->voltage, di->ui_soc, di->curr, di->temp, di->cycle);

	return 0;
}

static struct power_log_ops sh366101_log_ops = {
	.dev_name = "sh366101",
	.dump_log_head = sh366101_get_log_head,
	.dump_log_content = sh366101_dump_log_data,
};

static struct power_log_ops sh366101_aux_log_ops = {
	.dev_name = "sh366101_aux",
	.dump_log_head = sh366101_get_log_head,
	.dump_log_content = sh366101_dump_log_data,
};

static void sh366101_get_version(struct sh366101_dev *di, struct device_node *np, struct sh_decoder *decoder)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "version_main",
		&di->version_main, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "version_date",
		&di->version_date, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "version_afi",
		&di->version_afi, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "version_afi2",
		&di->version_afi2, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "version_ts",
		&di->version_ts, 0);
	(void)power_dts_read_u8(power_dts_tag(HWLOG_TAG), np, "iap_twiadr",
		&decoder->addr, 0);

	hwlog_info("check_chip_version: main=0x%04x, date=0x%08x, afi=0x%04x, ts=0x%04x\n",
		di->version_main, di->version_date, di->version_afi, di->version_ts);

	decoder->reg = (u8)SH366101_CMD_IAPSTATE_CHECK;
	decoder->length = SH366101_IAP_READ_LEN;
}

static int sh366101_check_fw_version(struct sh366101_dev *di, unsigned short *temp,
	unsigned char *iap_read, struct sh_decoder *decoder)
{
	int ret;
	if ((sh366101_decode_i2c_read(di, decoder, iap_read, SH366101_IAP_READ_LEN) >= 0) &&
		(iap_read[0] != 0) && (iap_read[1] != 0)) {
		hwlog_err("check_chip_version: ic is in iap mode, force update all\n");
		ret = SH366101_CHECK_VERSION_WHOLE_CHIP;
		return ret;
	}
	msleep(SH366101_CMD_SBS_DELAY);
	/* unseal ic */
	if (sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, (u16)SH366101_CMD_UNSEALKEY) < 0) {
		ret = SH366101_CHECK_VERSION_ERR;
		return ret;
	}
	msleep(SH366101_CMD_SBS_DELAY);

	if (sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, (u16)(SH366101_CMD_UNSEALKEY >> SH366101_CMD_KEY_SHIFT)) < 0) {
		ret = SH366101_CHECK_VERSION_ERR;
		return ret;
	}
	msleep(SH366101_CMD_SBS_DELAY);

	/* check fw version. fw update must update afi(for iap check flag) */
	if (sh366101_read_sbs_word(di, SH366101_CMD_FWVERSION_MAIN, temp) < 0) {
		ret = SH366101_CHECK_VERSION_ERR;
		return ret;
	}
	hwlog_info("chip_version: ic main=0x%04x\n", *temp);

	return 0;
}

static int sh366101_check_afi_version(struct sh366101_dev *di, unsigned char *buf,
	unsigned short *temp, unsigned short *sector_flag)
{
	unsigned int ret = 0;
	/* check afi. afi version 2 */
	if (sh366101_read_block(di, SH366101_CMD_AFI_STATIC_SUM, SH366101_LEN_USERBUFFER, buf) < 0)
		return SH366101_CHECK_VERSION_ERR;

	if (sh366101_read_sbs_word(di, SH366101_CMD_SECTOR_FLAG, sector_flag) < 0)
		return SH366101_CHECK_VERSION_ERR;

	di->chip_afi = sh366101_buf2u16_lt(&buf[0]);
	di->chip_afi2 = sh366101_buf2u16_lt(&buf[6]);
	hwlog_info("chip_version: ic afi=0x%04x, ic afi2=0x%04x, sector_flag=0x%04x\n",
		di->chip_afi, di->chip_afi2, *sector_flag);

	if ((*sector_flag & SH366101_MASK_SECTOR_FLAG) != 0)
		ret |= SH366101_CHECK_VERSION_AFI;

	if (di->chip_afi2 == 0) {
		if (di->chip_afi != di->version_afi)
			ret |= SH366101_CHECK_VERSION_AFI;
	} else if (di->chip_afi2 != di->version_afi2) {
		ret |= SH366101_CHECK_VERSION_AFI;
	}

	/* check ts */
	if (sh366101_read_sbs_word(di, SH366101_CMD_TS_VER, temp) < 0)
		return SH366101_CHECK_VERSION_ERR;

	hwlog_info("chip_version: ic ts=0x%04x\n", *temp);
	if (*temp != di->version_ts)
		ret |= SH366101_CHECK_VERSION_TS;

	return (int)ret;
}

static int sh366101_check_chip_version(struct sh366101_dev *di, struct device_node *np)
{
	struct sh_decoder decoder;
	unsigned char iap_read[SH366101_IAP_READ_LEN];
	int ret;
	unsigned int date;
	unsigned short temp;
	unsigned char buf[SH366101_LEN_USERBUFFER];
	unsigned short sector_flag = 0;

	/* battery_params node */
	np = of_find_node_by_name(of_node_get(np), "battery_params");
	if (np == NULL) {
		hwlog_err("check_chip_version: cannot find child node \"battery_params\"\n");
		return SH366101_CHECK_VERSION_ERR;
	}

	sh366101_get_version(di, np, &decoder);

	ret = sh366101_check_fw_version(di, &temp, iap_read, &decoder);
	if (ret != 0)
		goto Check_Chip_Version_End;

	if (temp < di->version_main) {
		ret = SH366101_CHECK_VERSION_WHOLE_CHIP;
		goto Check_Chip_Version_End;
	} else if (temp > di->version_main) {
		ret = SH366101_CHECK_VERSION_OK;
	} else { /* version equal, check date */
		if (sh366101_read_sbs_word(di, SH366101_CMD_FWDATE1, &temp) < 0) {
			ret = SH366101_CHECK_VERSION_ERR;
			goto Check_Chip_Version_End;
		}
		msleep(SH366101_CMD_SBS_DELAY);
		date = (u32)temp << SH366101_FW_DATE_SHIFT;

		if (sh366101_read_sbs_word(di, SH366101_CMD_FWDATE2, &temp) < 0) {
			ret = SH366101_CHECK_VERSION_ERR;
			goto Check_Chip_Version_End;
		}
		date |= (temp & SH366101_FW_DATE_MASK);
		hwlog_info("chip_version: ic date=0x%08x\n", date);
		if (date < di->version_date) {
			ret = SH366101_CHECK_VERSION_WHOLE_CHIP;
			goto Check_Chip_Version_End;
		}
		ret = SH366101_CHECK_VERSION_OK;
	}
	ret = sh366101_check_afi_version(di, buf, &temp, &sector_flag);
	if (ret == SH366101_CHECK_VERSION_ERR)
		goto Check_Chip_Version_End;

Check_Chip_Version_End:
	sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, SH366101_CMD_SEAL);
	return ret;
}

static int sh366101_read_runstate_para(struct sh366101_dev* di, unsigned short *oemflag)
{
	unsigned int soc_flag;
	unsigned int qen_flag;

	if (sh366101_read_soc(di) < 0)
		return -EINVAL;
	msleep(SH366101_CMD_SBS_DELAY);

	if (sh366101_read_volt(di) < 0)
		return -EINVAL;
	msleep(SH366101_CMD_SBS_DELAY);

	if (sh36610_read_temperature(di, SH366101_TEMPERATURE_IN) < 0)
		return -EINVAL;

	if (sh366101_read_terminate_voltage(di) < 0)
		return -EINVAL;
	msleep(SH366101_CMD_SBS_DELAY);

	if (sh366101_read_sbs_word(di, SH366101_CMD_OEMFLAG, oemflag) < 0)
		return -EINVAL;

	soc_flag = !!(di->batt_temp < SH366101_TEMPER_MIN_RESET);
	if (di->batt_curr <= 0) {
		/* get soc_flag */
		soc_flag |= ((di->batt_volt > SH366101_VOLT_MIN_RESET) && (di->batt_soc < SH366101_SOC_MIN_RESET)) && 1;
		soc_flag |= ((di->batt_soc == 0) && ((di->batt_volt - di->terminate_voltage) > SH366101_DELTA_VOLT)) && 1;
	}
	if (soc_flag) {
		if (sh366101_gauge_unseal(di) < 0)
			return -EINVAL;

		if (sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, (u16)SH366101_CMD_RESET) < 0)
			return -EINVAL;
		msleep(SH366101_DELAY_RESET);
	}
	qen_flag = !!((*oemflag & SH366101_CMD_MASK_OEM_GAUGEEN) != SH366101_CMD_MASK_OEM_GAUGEEN);
	if (qen_flag) {
		if (sh366101_gauge_unseal(di) < 0)
			return -EINVAL;

		if (sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, (u16)SH366101_CMD_ENABLE_GAUGE) < 0)
			return -EINVAL;
		msleep(SH366101_DELAY_ENABLE_GAUGE);
	}

	hwlog_info ("sh366101_read_runstate_para: QEN_FLAG=%u, SOC_FLAG=%u", qen_flag, soc_flag);

	return 0;
}

static int sh366101_gauge_runstate_check(struct sh366101_dev* di)
{
	int ret;
	unsigned short oemflag;
	int retry_cnt;
	unsigned int lt_flag;
	unsigned int cali_checked = 0;
	unsigned char buf_comm[SH366101_LEN_USERBUFFER];

	hwlog_info("sh366101_gauge_runstate_check start\n");

	/* In case por with poor connection */
	for (retry_cnt = 0; retry_cnt < SH366101_GAUGE_RUNSTATE_RETRY; retry_cnt++) {
		if (sh366101_read_runstate_para(di, &oemflag) < 0)
			goto sh366101_gauge_runstate_check_End;

		lt_flag = !!((oemflag & SH366101_CMD_MASK_OEM_LIFETIMEEN) != SH366101_CMD_MASK_OEM_LIFETIMEEN);
		if (lt_flag) {
			ret = sh366101_gauge_unseal(di);
			if (ret < 0)
				goto sh366101_gauge_runstate_check_End;

			ret = sh366101_write_sbs_word(di, SH366101_CMD_ALTMAC, (u16)SH366101_CMD_ENABLE_LIFETIME);
			if (ret < 0)
				goto sh366101_gauge_runstate_check_End;
			msleep(SH366101_DELAY_ENABLE_GAUGE);
		}

		if (!cali_checked) {
			ret = sh366101_gauge_unseal(di);
			if (ret < 0)
				goto sh366101_gauge_runstate_check_End;

			ret = sh366101_read_block(di, SH366101_CMD_OEMINFO, SH366101_LEN_USERBUFFER, buf_comm);
			if (ret < 0)
				goto sh366101_gauge_runstate_check_End;
			ret = sh366101_buf2u16_lt(&buf_comm[SH366101_INDEX_OEMINFO_INTFLAG]);
			di->is_enable_autocali = !!((unsigned int)ret & SH366101_MASK_OEMINFO_AUTOCALI);
			/* calibrated */
			cali_checked = 1;
		}
		ret = 0;
		break;

sh366101_gauge_runstate_check_End:
		msleep(SH366101_CMD_SBS_DELAY << SH366101_CMDSBS_DELAY_SHIFT);
	}
	hwlog_info ("fg_gauge_runstate_check: soc=%d, volt=%d, termVolt=%d, OEMFlag=0x%04X, LifeTime_Flag=%u, AutoCali=%u",
		di->batt_soc, di->batt_volt, di->terminate_voltage, oemflag, lt_flag, di->is_enable_autocali);

	sh366101_gauge_seal(di);

	return ret;
}

static int sh366101_parse_sub(struct sh366101_dev *di, struct device_node *np)
{
	int ret;
	int version_ret;
	int retry;

	version_ret = sh366101_check_chip_version(di, np);
	if (version_ret == SH366101_CHECK_VERSION_ERR) {
		hwlog_info("probe: check version error\n");
	} else if (version_ret == SH366101_CHECK_VERSION_OK) {
		hwlog_info("probe: check version ok\n");
	} else {
		hwlog_info("probe: check version update: %x\n", version_ret);

		ret = SH366101_ERRORTYPE_NONE;

		if ((unsigned int)version_ret & SH366101_CHECK_VERSION_FW) {
			hwlog_info("probe: firmware update start\n");
			for (retry = 0; retry < SH366101_FILE_DECODE_RETRY; retry++) {
				ret = sh366101_file_decode_process(di, np, "sinofs_image_data");
				if (ret == SH366101_ERRORTYPE_NONE)
					break;
				msleep(SH366101_FILE_DECODE_DELAY);
			}
			hwlog_info("probe: firmware update end, ret=%d\n", ret);
		}
		if (ret != SH366101_ERRORTYPE_NONE)
			goto sh_fg_probe_fwupdate_end;

		if ((unsigned int)version_ret & SH366101_CHECK_VERSION_TS) {
			hwlog_info("probe: ts update start\n");
			for (retry = 0; retry < SH366101_FILE_DECODE_RETRY; retry++) {
				ret = sh366101_file_decode_process(di, np, "sinofs_ts_data");
				if (ret == SH366101_ERRORTYPE_NONE)
					break;
				msleep(SH366101_FILE_DECODE_DELAY);
			}
			hwlog_info("probe: ts update end, ret=%d\n", ret);
		}

		if (ret != SH366101_ERRORTYPE_NONE)
			goto sh_fg_probe_fwupdate_end;

		if ((unsigned int)version_ret & SH366101_CHECK_VERSION_AFI) {
			hwlog_info("probe: afi update start\n");
			for (retry = 0; retry < SH366101_FILE_DECODE_RETRY; retry++) {
				ret = sh366101_file_decode_process(di, np, "sinofs_afi_data");
				if (ret == SH366101_ERRORTYPE_NONE)
					break;
				msleep(SH366101_FILE_DECODE_DELAY);
			}
			hwlog_info("probe: afi update end, ret=%d\n", ret);
		}
			if (ret != SH366101_ERRORTYPE_NONE)
				goto sh_fg_probe_fwupdate_end;
	}

sh_fg_probe_fwupdate_end:
	return 0;
}

static int sh366101_parse_dt(struct device *dev, struct sh366101_dev *di)
{
	int ret;
	struct device_node *np = dev->of_node;
	struct device_node *child_node = sh366101_get_child_node(dev);

	if (!child_node)
		return -ENOMEM;

	ret = sh366101_parse_sub(di, child_node);
	if (ret < 0) {
		hwlog_err("sh366101 parse sub fail\n");
		return ret;
	}

	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "ic_role",
		&di->ic_role, SH366101_IC_TYPE_MAIN);

	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "test_data",
		&di->test_data, SH366101_TEST_DATA_DEFAULT);

	hwlog_info("test_data=%d\n", di->test_data);

	return 0;
}

static void sh366101_para_init(struct i2c_client *client, struct sh366101_dev *di)
{
	unsigned int *regs = NULL;

	hwlog_info("sh366101_init start\n");
	di->dev = &client->dev;
	di->client = client;
	di->current_ratio = 0;
	di->current_ratio_pre = 0;
	di->current_offset = 0;
	di->deadband = SH366101_DEADBAND_DEFAULT;
	di->sensor_ratio = SH366101_USER_SENSOR_RATIO;

	regs = sh366101_regs;
	memcpy(di->regs, regs, SH366101_NUM_REGS * sizeof(u32));
	i2c_set_clientdata(client, di);

	mutex_init(&di->i2c_rw_lock);
	mutex_init(&di->data_lock);
	mutex_init(&di->cali_lock);

	sh366101_refresh_status(di);
}

static void sh366101_ops_register(struct sh366101_dev *di)
{
	hwlog_info("sh366101_ops_register start\n");
#ifdef CONFIG_HUAWEI_HW_DEV_DCT
	if (di->ic_role == SH366101_IC_TYPE_MAIN)
		set_hw_dev_flag(DEV_I2C_GAUGE_IC);
	else
		set_hw_dev_flag(DEV_I2C_GAUGE_IC_AUX);
#endif

	if (di->ic_role == SH366101_IC_TYPE_MAIN) {
		sh366101_log_ops.dev_data = (void *)di;
		sh366101_ops.dev_data = (void *)di;
		sh366101_cali_ops.dev_data = (void *)di;
		power_log_ops_register(&sh366101_log_ops);
		coul_interface_ops_register(&sh366101_ops);
		coul_cali_ops_register(&sh366101_cali_ops);
	} else {
		sh366101_aux_log_ops.dev_data = (void *)di;
		sh366101_aux_ops.dev_data = (void *)di;
		sh366101_aux_cali_ops.dev_data = (void *)di;
		power_log_ops_register(&sh366101_aux_log_ops);
		coul_interface_ops_register(&sh366101_aux_ops);
		coul_cali_ops_register(&sh366101_aux_cali_ops);
	}
}

static int sh366101_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct sh366101_dev *di = NULL;
	struct power_devices_info_data *power_dev_info = NULL;

	hwlog_info("sh366101_probe: start\n");

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	sh366101_para_init(client, di);

	ret = sh366101_parse_dt(&client->dev, di);
	if (ret) {
		hwlog_err("parse dts fail\n");
		goto err_0;
	}

	ret = sh366101_gauge_runstate_check(di); /* gauge enable */
	if (ret) {
		hwlog_err("failed to enable gauge\n");
		goto err_0;
	}

	ret = sh366101_hal_fg_init(client);
	if (ret) {
		hwlog_err("Failed to Initialize sh366101\n");
		goto err_0;
	}

	sh366101_ops_register(di);

	power_dev_info = power_devices_info_register();
	if (power_dev_info) {
		power_dev_info->dev_name = di->dev->driver->name;
		power_dev_info->dev_id = (unsigned int)di->chip_id;
		power_dev_info->ver_id = 0;
	}

	di->shfg_workqueue = create_singlethread_workqueue("shfg_gauge");
	INIT_DELAYED_WORK(&di->battery_delay_work, sh366101_bat_work);
	queue_delayed_work(di->shfg_workqueue, &di->battery_delay_work,
		msecs_to_jiffies(SH366101_QUEUE_DELAYED_WORK_TIME));
	bsoh_unregister_sub_sys(BSOH_SUB_SYS_BASP);
	hwlog_info("sh366101_probe: end\n");

	return 0;

err_0:
	devm_kfree(&client->dev, di);
	return ret;
}

static int sh366101_remove(struct i2c_client *client)
{
	return 0;
}

#ifdef CONFIG_PM
static int sh366101_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh366101_dev *di = NULL;

	if (!client)
		return 0;

	di = i2c_get_clientdata(client);
	if (!di)
		return 0;
	hwlog_info("sh366101_suspend cancel work\n");
	cancel_delayed_work_sync(&di->battery_delay_work);

	return 0;
}

static int sh366101_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh366101_dev *di = NULL;

	if (!client)
		return 0;

	di = i2c_get_clientdata(client);
	if (!di)
		return 0;

	queue_delayed_work(di->shfg_workqueue, &di->battery_delay_work,
		msecs_to_jiffies(SH366101_QUEUE_DELAYED_WORK_TIME));
	hwlog_info("sh366101_resume queue work\n");

	return 0;
}

static const struct dev_pm_ops sh366101_pm_ops = {
	.suspend = sh366101_suspend,
	.resume = sh366101_resume,
};
#define SH366101_PM_OPS (&sh366101_pm_ops)
#else
#define SH366101_PM_OPS (NULL)
#endif

static const struct i2c_device_id sh366101_id_table[] = {
	{ "sh366101", 0 },
	{},
};

static const struct of_device_id sh366101_match_table[] = {
	{ .compatible = "sh,sh366101", },
	{},
};

static struct i2c_driver sh366101_driver = {
	.probe = sh366101_probe,
	.remove = sh366101_remove,
	.id_table = sh366101_id_table,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sh366101",
		.of_match_table = sh366101_match_table,
		.pm = SH366101_PM_OPS,
	},
};

static int __init sh366101_init(void)
{
	return i2c_add_driver(&sh366101_driver);
}

static void __exit sh366101_exit(void)
{
	i2c_del_driver(&sh366101_driver);
}

module_init(sh366101_init);
module_exit(sh366101_exit);

MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
MODULE_DESCRIPTION("sh366101 Fuel Gauge Driver");
MODULE_LICENSE("GPL v2");
