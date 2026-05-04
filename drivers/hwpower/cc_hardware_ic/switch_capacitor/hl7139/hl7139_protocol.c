// SPDX-License-Identifier: GPL-2.0
/*
 * hl7139_protocol.c
 *
 * hl7139 protocol driver
 *
 * Copyright (c) 2021-2021 Huawei Technologies Co., Ltd.
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

#include "hl7139_protocol.h"
#include "hl7139_i2c.h"
#include <linux/delay.h>
#include <linux/mutex.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/protocol/adapter_protocol.h>
#include <chipset_common/hwpower/protocol/adapter_protocol_scp.h>
#include <chipset_common/hwpower/protocol/adapter_protocol_fcp.h>
#include <chipset_common/hwpower/common_module/power_delay.h>

#define HWLOG_TAG hl7139_protocol
HWLOG_REGIST();

static int hl7139_scp_wdt_reset_by_sw(struct hl7139_device_info *di)
{
	int ret;

	ret = hl7139_write_byte(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_SCP_STIMER_REG, HL7139_SCP_STIMER_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_0_REG, HL7139_RT_BUFFER_0_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_1_REG, HL7139_RT_BUFFER_1_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_2_REG, HL7139_RT_BUFFER_2_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_3_REG, HL7139_RT_BUFFER_3_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_4_REG, HL7139_RT_BUFFER_4_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_5_REG, HL7139_RT_BUFFER_5_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_6_REG, HL7139_RT_BUFFER_6_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_7_REG, HL7139_RT_BUFFER_7_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_8_REG, HL7139_RT_BUFFER_8_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_9_REG, HL7139_RT_BUFFER_9_WDT_RESET);
	ret += hl7139_write_byte(di, HL7139_RT_BUFFER_10_REG, HL7139_RT_BUFFER_10_WDT_RESET);

	return ret;
}

/* just for reset-DM when slave ping lossed */
static int hl7139_reset_protocol_register(struct hl7139_device_info *di)
{
	int ret;

	ret = hl7139_write_byte(di, HL7139_DP_MAN_CTL_REG, HL7139_MAN_MODE);
	ret += hl7139_write_byte(di, HL7139_FORCE_DPDM_CTL_REG, HL7139_FORCE_DP_P6V);
	ret += hl7139_write_byte(di, HL7139_PASSWORD_0_REG, HL7139_PASSWORD_00_MASK);
	ret += hl7139_write_byte(di, HL7139_PASSWORD_0_REG, HL7139_PASSWORD_01_MASK);
	ret += hl7139_write_byte(di, HL7139_PASSWORD_1_REG, HL7139_PASSWORD_10_MASK);
	ret += hl7139_write_byte(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_RESET);
	ret += hl7139_write_byte(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_ENABLE_SCP);
	ret += hl7139_write_byte(di, HL7139_PASSWORD_1_REG, HL7139_PASSWORD_1_CLOSE);
	ret += hl7139_write_byte(di, HL7139_PASSWORD_0_REG, HL7139_PASSWORD_0_CLOSE);
	ret += hl7139_write_byte(di, HL7139_FORCE_DPDM_CTL_REG, HL7139_FORCE_DPDM_EXIT);
	ret += hl7139_write_byte(di, HL7139_DP_MAN_CTL_REG, HL7139_DP_MAN_EXIT);

	return ret;
}

static void hl7139_set_scp_fsw(int fsw, struct hl7139_device_info *di)
{
	int ref_fsw;
	unsigned int new_fsw;
	int shift;

	if ((fsw > HL7139_SCP_MAX_FSW) || (fsw < HL7139_SCP_MIN_FSW)) {
		hwlog_err("fsw error!\n");
		return;
	}

	/* Adjusts the communication frequency of each chip based on the reference of each chip */
	ref_fsw = di->back_regd8 & HL7139_SCP_MASK2;
	shift = (HL7139_SCP_BASE_FSW - fsw) / HL7139_SCP_BASE_FSW_STEP;
	if ((ref_fsw >= HL7139_FSW_ESCAPE_POINT) &&
		((ref_fsw - shift) < HL7139_FSW_ESCAPE_POINT) && (fsw <= HL7139_SCP_FSW_920))
		shift += 4;
	else if ((ref_fsw >= HL7139_FSW_ESCAPE_POINT) &&
		((ref_fsw - shift) < HL7139_FSW_ESCAPE_POINT) && (fsw > HL7139_SCP_FSW_920))
		shift += 3;
	else if ((ref_fsw < HL7139_FSW_ESCAPE_POINT) &&
		((ref_fsw - shift) >= HL7139_FSW_ESCAPE_POINT) && (fsw > HL7139_SCP_BASE_FSW))
		shift -= 3;

	new_fsw = ref_fsw - shift;
	new_fsw = (di->back_regd8 & HL7139_SCP_BASE_FSW_MASK) | (new_fsw & HL7139_SCP_MASK2);
	/* enter test mode */
	hl7139_write_byte(di, HL7139_TEST_MODE_REG, HL7139_TEST_MODE_PASS1);
	hl7139_write_byte(di, HL7139_TEST_MODE_REG, HL7139_TEST_MODE_PASS2);
	hl7139_write_byte(di, HL7139_SCP_BASE_FSW_REG, new_fsw);
	/* exit test mode */
	hl7139_write_byte(di, HL7139_TEST_MODE_REG, HL7139_TEST_MODE_EXIT);
	hwlog_info("set scp fsw %d value: 0x%x backd8 0x%x\n", fsw, new_fsw, di->back_regd8);
}

/* check whether slave ping lossed */
static int hl7139_scp_set_fsw_by_errack(u8 reg_val1, u8 reg_val2, struct hl7139_device_info *di)
{
	int slv_norep_freq[SLV_NOREP_FREQ_NUM] = { 0, 900, 880, 920, 1000 };
	int crc_err_freq[CRC_ERR_FREQ_NUM] = { 0, 940, 1060, 920, 1080, 1000 };

	/* slave no response error */
	if ((reg_val2 & HL7139_SCP_ISR2_CMD_CPL_MASK) &&
		!(reg_val2 & HL7139_SCP_ISR2_SLV_R_CPL_MASK)) {
		if (slv_norep_freq[di->slv_norep_cnt % SLV_NOREP_FREQ_NUM] != 0)
			hl7139_set_scp_fsw(slv_norep_freq[di->slv_norep_cnt % SLV_NOREP_FREQ_NUM], di);
		di->slv_norep_cnt++;
		if (di->slv_norep_cnt >= SLV_NOREP_FREQ_NUM)
			di->slv_norep_cnt = 0;
		(void)hl7139_reset_protocol_register(di);
		hwlog_err("scp transfer fail, slave no response error: ISR1 = 0x%x, ISR2 = 0x%x\n",
			reg_val1, reg_val2);
		return -EPERM;
	}
	if ((reg_val1 & HL7139_SCP_ISR1_ACK_PARRX_MASK) &&
		!(reg_val1 & HL7139_SCP_ISR1_ACK_CRCRX_MASK) &&
		!(reg_val1 & HL7139_SCP_ISR1_ERR_ACK_L_MASK)) {
		/* crc or par error */
		if (crc_err_freq[di->crc_err_cnt % CRC_ERR_FREQ_NUM] != 0)
			hl7139_set_scp_fsw(crc_err_freq[di->crc_err_cnt % CRC_ERR_FREQ_NUM], di);
		di->crc_err_cnt++;
		if (di->crc_err_cnt >= CRC_ERR_FREQ_NUM)
			di->crc_err_cnt = 0;
		(void)hl7139_reset_protocol_register(di);
		hwlog_err("scp transfer fail, CRC or PAR error: ISR1 = 0x%x, ISR2 = 0x%x\n",
			reg_val1, reg_val2);
		return -EPERM;
	}
	return 0;
}

static int hl7139_scp_cmd_transfer_check(struct hl7139_device_info *di)
{
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	int i = 0;
	int ret0;
	int ret1;

	do {
		power_msleep(DT_MSLEEP_50MS, 0, NULL); /* wait 50ms for each cycle */
		ret0 = hl7139_read_byte(di, HL7139_SCP_ISR1_REG, &reg_val1);
		ret1 = hl7139_read_byte(di, HL7139_SCP_ISR2_REG, &reg_val2);
		if (ret0 || ret1) {
			hwlog_err("check reg read failed\n");
			break;
		}
		hwlog_info("reg_val1(0x%x), reg_val2(0x%x), scp_isr_backup[0] = 0x%x, scp_isr_backup[1] = 0x%x\n",
			reg_val1, reg_val2, di->scp_isr_backup[0], di->scp_isr_backup[1]);
		/* interrupt work can hook the interrupt value first. so it is necessily to do backup via isr_backup */
		reg_val1 |= di->scp_isr_backup[0];
		reg_val2 |= di->scp_isr_backup[1];
		/* check whether Slave ping lossed */
		if (((reg_val1 & HL7139_SCP_ISR1_ACK_CRCRX_MASK) ||
			(reg_val1 & HL7139_SCP_ISR1_ACK_PARRX_MASK)) &&
			!(reg_val2 & HL7139_SCP_ISR2_SLV_R_CPL_MASK)) {
			ret0 = hl7139_reset_protocol_register(di);
			hwlog_info("reset protocol, ret0 = %d\n", ret0);
			return -EPERM;
		}
		if (reg_val1 || reg_val2) {
			if (((reg_val2 & HL7139_SCP_ISR2_ACK_MASK) &&
				(reg_val2 & HL7139_SCP_ISR2_CMD_CPL_MASK)) &&
				!(reg_val1 & (HL7139_SCP_ISR1_ACK_CRCRX_MASK |
				HL7139_SCP_ISR1_ACK_PARRX_MASK |
				HL7139_SCP_ISR1_ERR_ACK_L_MASK)))
				return 0;
			hwlog_err("scp transfer not complete, ISR1 = 0x%x, ISR2 = 0x%x, index = %d\n",
				reg_val1, reg_val2, i);
			return -EPERM;
		}
		i++;
		if (di->dc_ibus_ucp_happened)
			i = HL7139_SCP_ACK_RETRY_CYCLE;
	} while (i < HL7139_SCP_ACK_RETRY_CYCLE);

	hwlog_err("scp adapter transfer time out\n");
	return -EPERM;
}

static int hl7139_scp_cmd_transfer_check_1(struct hl7139_device_info *di)
{
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	u8 pre_val1 = 0;
	u8 pre_val2 = 0;
	int i = 0;
	int ret0;
	int ret1;

	do {
		power_msleep(DT_MSLEEP_50MS, 0, NULL);
		ret0 = hl7139_read_byte(di, HL7139_SCP_ISR1_REG, &pre_val1);
		ret1 = hl7139_read_byte(di, HL7139_SCP_ISR2_REG, &pre_val2);
		if (ret0 || ret1) {
			hwlog_err("check_1 reg read failed\n");
			break;
		}
		hwlog_info("pre_val1(0x%x), pre_val2(0x%x), scp_isr_backup[0](0x%x), scp_isr_backup[1](0x%x)\n",
			pre_val1, pre_val2, di->scp_isr_backup[0], di->scp_isr_backup[1]);
		/* save insterrupt value to reg_val1/2 from starting scp cmd to SLV_R_CPL interrupt */
		reg_val1 |= pre_val1;
		reg_val2 |= pre_val2;

		/* solve old version bug via config fsw by isr ack err type */
		if (di->rev_id == HL7139_OLD_VERSION &&
			hl7139_scp_set_fsw_by_errack(reg_val1, reg_val2, di))
			return -EPERM;

		if (reg_val1 || reg_val2) {
			if (((reg_val2 & HL7139_SCP_ISR2_ACK_MASK) &&
				(reg_val2 & HL7139_SCP_ISR2_CMD_CPL_MASK) &&
				(reg_val2 & HL7139_SCP_ISR2_SLV_R_CPL_MASK)) &&
				!(reg_val1 & (HL7139_SCP_ISR1_ACK_CRCRX_MASK |
				HL7139_SCP_ISR1_ACK_PARRX_MASK | HL7139_SCP_ISR1_ERR_ACK_L_MASK)))
				return 0;
			hwlog_err("scp transfer fail, ISR1 = 0x%x, ISR2 = 0x%x, index = %d\n",
				reg_val1, reg_val2, i);
			return -EPERM;
		}
		i++;
		if (di->dc_ibus_ucp_happened)
			i = HL7139_SCP_ACK_RETRY_CYCLE_1;
	} while (i < HL7139_SCP_ACK_RETRY_CYCLE_1);
	hwlog_err("scp adapter transfer time out\n");
	return -EPERM;
}

static void hl7139_scp_protocol_restart(struct hl7139_device_info *di)
{
	u8 reg_val = 0;
	int ret;
	int i;

	mutex_lock(&di->scp_detect_lock);

	/* detect scp charger, wait for ping succ */
	for (i = 0; i < HL7139_SCP_RESTART_TIME; i++) {
		usleep_range(9000, 10000); /* wait 9ms for each cycle */
		ret = hl7139_read_byte(di, HL7139_SCP_STATUS_REG, &reg_val);
		if (ret) {
			hwlog_err("read det attach err, ret: %d\n", ret);
			continue;
		}

		if (reg_val & HL7139_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK)
			break;
	}

	if (i == HL7139_SCP_RESTART_TIME) {
		hwlog_err("wait for slave fail\n");
		mutex_unlock(&di->scp_detect_lock);
		return;
	}
	mutex_unlock(&di->scp_detect_lock);
	hwlog_info("disable and enable scp protocol accp status is 0x%x\n", reg_val);
}

static int hl7139_scp_adapter_reg_read(u8 *val, u8 reg, void *dev_data)
{
	int ret;
	int i;
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	u8 retrycnt = HL7139A_SCP_RETRY_TIME;
	struct hl7139_device_info *di = dev_data;

	mutex_lock(&di->accp_adapter_reg_lock);
	hwlog_info("CMD = 0x%x, REG = 0x%x\n", HL7139_SCP_CMD_SBRRD, reg);
	if (di->rev_id == HL7139_OLD_VERSION)
		retrycnt = HL7139_SCP_RETRY_TIME;
	di->crc_err_cnt = 0;
	di->slv_norep_cnt = 0;
	for (i = 0; i < retrycnt; i++) {
		/* init */
		hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
			HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);

		/* before send cmd, clear isr interrupt registers */
		ret = hl7139_read_byte(di, HL7139_SCP_ISR1_REG, &reg_val1);
		ret += hl7139_read_byte(di, HL7139_SCP_ISR2_REG, &reg_val2);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_0_REG, HL7139_SCP_CMD_SBRRD);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_1_REG, reg);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_2_REG, 1);
		/* initial scp_isr_backup[0],[1] due to catching the missing isr by interrupt_work */
		di->scp_isr_backup[0] = 0;
		di->scp_isr_backup[1] = 0;
		ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SCP_UI_SYNC_MASK, HL7139_SCP_CTL_SCP_UI_SYNC_SHIFT, 0);
		ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
			HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_START);
		if (ret) {
			hwlog_err("write error, ret is %d\n", ret);
			/* manual init */
			hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
				HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -EPERM;
		}

		/* check cmd transfer success or fail */
		if (di->rev_id == 0) {
			if (hl7139_scp_cmd_transfer_check(di) == 0) {
				/* recived data from adapter */
				ret = hl7139_read_byte(di, HL7139_RT_BUFFER_12_REG, val);
				break;
			}
		} else {
			if (hl7139_scp_cmd_transfer_check_1(di) == 0) {
				/* recived data from adapter */
				ret = hl7139_read_byte(di, HL7139_RT_BUFFER_12_REG, val);
				break;
			}
		}

		hl7139_scp_protocol_restart(di);
		if (di->dc_ibus_ucp_happened)
			i = retrycnt;
	}
	if (i >= retrycnt) {
		hwlog_err("ack error, retry %d times\n", i);
		ret = -EINVAL;
	}
	mutex_unlock(&di->accp_adapter_reg_lock);

	return ret;
}

static int hl7139_scp_adapter_reg_read_block(u8 reg, u8 *val, u8 num,
	void *dev_data)
{
	int ret;
	int i;
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	u8 retrycnt = HL7139A_SCP_RETRY_TIME;
	u8 *p = val;
	u8 data_len = (num < HL7139_SCP_DATA_LEN) ? num : HL7139_SCP_DATA_LEN;
	struct hl7139_device_info *di = dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -ENOMEM;
	}
	mutex_lock(&di->accp_adapter_reg_lock);

	hwlog_info("CMD = 0x%x, REG = 0x%x, Num = 0x%x\n",
		HL7139_SCP_CMD_MBRRD, reg, data_len);
	if (di->rev_id == HL7139_OLD_VERSION)
		retrycnt = HL7139_SCP_RETRY_TIME;
	di->crc_err_cnt = 0;
	di->slv_norep_cnt = 0;
	for (i = 0; i < retrycnt; i++) {
		/* init */
		hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
			HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);

		/* before sending cmd, clear isr registers */
		ret = hl7139_read_byte(di, HL7139_SCP_ISR1_REG, &reg_val1);
		ret += hl7139_read_byte(di, HL7139_SCP_ISR2_REG, &reg_val2);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_0_REG, HL7139_SCP_CMD_MBRRD);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_1_REG, reg);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_2_REG, data_len);
		/* initial scp_isr_backup[0],[1] */
		di->scp_isr_backup[0] = 0;
		di->scp_isr_backup[1] = 0;
		ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SCP_UI_SYNC_MASK,
			HL7139_SCP_CTL_SCP_UI_SYNC_SHIFT, 0);
		ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
			HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_START);
		if (ret) {
			hwlog_err("read error ret is %d\n", ret);
			/* manual init */
			hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
				HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -EPERM;
		}

		/* check cmd transfer success or fail */
		if (di->rev_id == 0) {
			if (hl7139_scp_cmd_transfer_check(di) == 0) {
				/* recived data from adapter */
				ret = hl7139_read_block(di, p, HL7139_RT_BUFFER_12_REG, data_len);
				break;
			}
		} else {
			if (hl7139_scp_cmd_transfer_check_1(di) == 0) {
				/* recived data from adapter */
				ret = hl7139_read_block(di, p, HL7139_RT_BUFFER_12_REG, data_len);
				break;
			}
		}

		hl7139_scp_protocol_restart(di);
		if (di->dc_ibus_ucp_happened)
			i = retrycnt;
	}
	if (i >= retrycnt) {
		hwlog_err("ack error, retry %d times\n", i);
		ret = -EINVAL;
	}
	mutex_unlock(&di->accp_adapter_reg_lock);

	if (ret) {
		/* manual init */
		hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
			HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);
		return ret;
	}

	num -= data_len;
	/* max is HL7139_SCP_DATA_LEN. remaining data is read in below */
	if (num) {
		p += data_len;
		reg += data_len;
		ret = hl7139_scp_adapter_reg_read_block(reg, p, num, di);
		if (ret) {
			/* manual init */
			hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
				HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);
			hwlog_err("read error, ret is %d\n", ret);
			return -EPERM;
		}
	}
	return 0;
}

static int hl7139_scp_adapter_reg_write(u8 val, u8 reg, void *dev_data)
{
	int ret;
	int i;
	u8 reg_val1 = 0;
	u8 reg_val2 = 0;
	u8 retrycnt = HL7139A_SCP_RETRY_TIME;
	struct hl7139_device_info *di = dev_data;

	mutex_lock(&di->accp_adapter_reg_lock);
	hwlog_info("CMD = 0x%x, REG = 0x%x, val = 0x%x\n",
		HL7139_SCP_CMD_SBRWR, reg, val);
	if (di->rev_id == HL7139_OLD_VERSION)
		retrycnt = HL7139_SCP_RETRY_TIME;
	di->crc_err_cnt = 0;
	di->slv_norep_cnt = 0;
	for (i = 0; i < retrycnt; i++) {
		/* init */
		hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
			HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);

		/* before send cmd, clear accp interrupt registers */
		ret = hl7139_read_byte(di, HL7139_SCP_ISR1_REG, &reg_val1);
		ret += hl7139_read_byte(di, HL7139_SCP_ISR2_REG, &reg_val2);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_0_REG, HL7139_SCP_CMD_SBRWR);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_1_REG, reg);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_2_REG, 1);
		ret += hl7139_write_byte(di, HL7139_RT_BUFFER_3_REG, val);
		/* initial scp_isr_backup[0],[1] */
		di->scp_isr_backup[0] = 0;
		di->scp_isr_backup[1] = 0;
		ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SCP_UI_SYNC_MASK,
			HL7139_SCP_CTL_SCP_UI_SYNC_SHIFT, 0);
		ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
			HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_START);
		if (ret) {
			hwlog_err("write error, ret is %d\n", ret);
			/* manual init */
			hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SNDCMD_MASK,
				HL7139_SCP_CTL_SNDCMD_SHIFT, HL7139_SCP_CTL_SNDCMD_RESET);
			mutex_unlock(&di->accp_adapter_reg_lock);
			return -EPERM;
		}

		/* check cmd transfer success or fail */
		if (di->rev_id == 0) {
			if (hl7139_scp_cmd_transfer_check(di) == 0)
				break;
		} else {
			if (hl7139_scp_cmd_transfer_check_1(di) == 0)
				break;
		}

		hl7139_scp_protocol_restart(di);
		if (di->dc_ibus_ucp_happened)
			i = retrycnt;
	}
	if (i >= retrycnt) {
		hwlog_err("ack error, retry %d times\n", i);
		ret = -EINVAL;
	}

	mutex_unlock(&di->accp_adapter_reg_lock);
	return ret;
}

static int hl7139_fcp_master_reset(void *dev_data)
{
	struct hl7139_device_info *di = dev_data;
	int ret;

	if (!di) {
		hwlog_err("di is null\n");
		return -EPERM;
	}

	ret = hl7139_scp_wdt_reset_by_sw(di);
	if (ret)
		return -EPERM;

	usleep_range(10000, 11000); /* wait 10ms for operate effective */

	return 0;
}

static int hl7139_fcp_adapter_detect_enable(struct hl7139_device_info *di)
{
	int ret;
	u8 reg_val = 0;

	ret = hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_EN_SCP_MASK,
		HL7139_SCP_CTL_EN_SCP_SHIFT, 0);
	ret += hl7139_write_byte(di, HL7139_DP_MAN_CTL_REG, HL7139_MAN_MODE);
	ret += hl7139_read_byte(di, HL7139_DP_MAN_CTL_REG, &reg_val);
	if ((reg_val & HL7139_DP_DM_STAT_MASK) == HL7139_DP_DM_HIGH) {
		hwlog_err("dpdm stat:0x%x\n", reg_val);
		ret += hl7139_write_byte(di, HL7139_FORCE_DPDM_CTL_REG, HL7139_FORCE_PULL_DOWN_DP);
		power_msleep(DT_MSLEEP_10MS, 0, NULL);
		ret += hl7139_write_byte(di, HL7139_FORCE_DPDM_CTL_REG, 0);
	}
	ret += hl7139_write_byte(di, HL7139_DP_MAN_CTL_REG, 0);

	return ret;
}

static int hl7139_fcp_adapter_reset(void *dev_data)
{
	int ret;
	struct hl7139_device_info *di = dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -EPERM;
	}

	ret = hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_MSTR_RST_MASK,
		HL7139_SCP_CTL_MSTR_RST_SHIFT, 1);
	usleep_range(20000, 21000); /* wait 20ms for operate effective */
	ret += hl7139_scp_wdt_reset_by_sw(di);
	ret += hl7139_config_vbuscon_ovp_ref_mv(HL7139_VGS_SEL_INIT, di);
	ret += hl7139_config_vbus_ovp_ref_mv(HL7139_VBUS_OVP_INIT, di);

	return ret;
}

static void hl7139_fcp_adapter_detect_reset(struct hl7139_device_info *di)
{
	hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_EN_SCP_MASK,
		HL7139_SCP_CTL_EN_SCP_SHIFT, 0);
	hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SCP_DET_EN_MASK,
		HL7139_SCP_CTL_SCP_DET_EN_SHIFT, 0);
	/* reset scp registers when EN_SCP is changed to 0 */
	hl7139_scp_wdt_reset_by_sw(di);
	hl7139_fcp_adapter_reset(di);
}

static int hl7139_fcp_read_switch_status(void *dev_data)
{
	return 0;
}

static int hl7139_is_fcp_charger_type(void *dev_data)
{
	u8 reg_val = 0;
	int ret;
	struct hl7139_device_info *di = dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -EPERM;
	}

	if (hl7139_is_support_fcp(di))
		return 0;

	ret = hl7139_read_byte(di, HL7139_SCP_STATUS_REG, &reg_val);
	if (ret)
		return 0;

	if (reg_val & HL7139_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK)
		return 1;
	return 0;
}

static int hl7139_fcp_adapter_detect(struct hl7139_device_info *di)
{
	u8 reg_val = 0;
	int vbus_uvp;
	int i;
	int ret;

	mutex_lock(&di->scp_detect_lock);
	ret = hl7139_read_byte(di, HL7139_SCP_STATUS_REG, &reg_val);
	if (ret) {
		hwlog_err("read HL7139_SCP_STATUS_REG fail, ret:%d\n", ret);
		mutex_unlock(&di->scp_detect_lock);
		return ADAPTER_DETECT_OTHER;
	}

	/* confirm enable hand success status */
	if (!(reg_val & HL7139_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK)) {
		ret += hl7139_fcp_adapter_detect_enable(di);
		if (ret) {
			hl7139_fcp_adapter_detect_reset(di);
			mutex_unlock(&di->scp_detect_lock);
			return ADAPTER_DETECT_OTHER;
		}
	}

	ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_EN_SCP_MASK,
		HL7139_SCP_CTL_EN_SCP_SHIFT, 1);
	ret += hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SCP_DET_EN_MASK,
		HL7139_SCP_CTL_SCP_DET_EN_SHIFT, 1);
	if (ret) {
		hwlog_err("SCP enable detect fail, ret is %d\n", ret);
		hl7139_fcp_adapter_detect_reset(di);
		mutex_unlock(&di->scp_detect_lock);
		return -EPERM;
	}
	/* waiting for scp set */
	for (i = 0; i < HL7139_SCP_DETECT_MAX_COUT; i++) {
		ret = hl7139_read_byte(di, HL7139_SCP_STATUS_REG, &reg_val);
		hwlog_info("HL7139_SCP_STATUS_REG 0x%x\n", reg_val);
		if (ret) {
			hwlog_err("read det attach err, ret:%d\n", ret);
			continue;
		}
		vbus_uvp = hl7139_get_vbus_uvp_status(di);
		if (vbus_uvp) {
			hwlog_err("0x%x vbus uv happen, adapter plug out\n", vbus_uvp);
			break;
		}
		if (reg_val & HL7139_SCP_STATUS_ENABLE_HAND_SUCCESS_MASK)
			break;
		msleep(HL7139_SCP_POLL_TIME);
	}
	if ((i == HL7139_SCP_DETECT_MAX_COUT) || vbus_uvp) {
		hl7139_fcp_adapter_detect_reset(di);
		hwlog_err("CHG_SCP_ADAPTER_DETECT_OTHER return\n");
		mutex_unlock(&di->scp_detect_lock);
		return ADAPTER_DETECT_OTHER;
	}

	mutex_unlock(&di->scp_detect_lock);
	return ret;
}

static int hl7139_fcp_stop_charge_config(void *dev_data)
{
	struct hl7139_device_info *di = dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -EPERM;
	}

	hl7139_fcp_master_reset(di);
	hl7139_write_mask(di, HL7139_SCP_CTL_REG, HL7139_SCP_CTL_SCP_DET_EN_MASK,
		HL7139_SCP_CTL_SCP_DET_EN_SHIFT, 0);

	return 0;
}

static int hl7139_scp_reg_read(u8 *val, u8 reg, void *dev_data)
{
	int ret;
	struct hl7139_device_info *di = dev_data;

	if (di->scp_error_flag) {
		hwlog_err("scp timeout happened, do not read reg = 0x%x\n", reg);
		return -EPERM;
	}

	ret = hl7139_scp_adapter_reg_read(val, reg, dev_data);
	if (ret) {
		hwlog_err("error reg = 0x%x\n", reg);
		if (reg != HWSCP_ADP_TYPE0)
			di->scp_error_flag = HL7139_SCP_IS_ERR;

		return -EPERM;
	}

	return 0;
}

static int hl7139_scp_reg_write(u8 val, u8 reg, void *dev_data)
{
	int ret;
	struct hl7139_device_info *di = dev_data;

	if (di->scp_error_flag) {
		hwlog_err("scp timeout happened, do not write reg = 0x%x\n", reg);
		return -EPERM;
	}

	ret = hl7139_scp_adapter_reg_write(val, reg, dev_data);
	if (ret) {
		hwlog_err("error reg = 0x%x\n", reg);
		di->scp_error_flag = HL7139_SCP_IS_ERR;
		return -EPERM;
	}

	return 0;
}

static int hl7139_self_check(void *dev_data)
{
	return 0;
}

static int hl7139_scp_chip_reset(void *dev_data)
{
	return hl7139_fcp_master_reset(dev_data);
}

static int hl7139_scp_reg_read_block(int reg, int *val, int num,
	void *dev_data)
{
	int ret;
	int i;
	u8 data = 0;
	struct hl7139_device_info *di = dev_data;

	if (!val || !dev_data) {
		hwlog_err("val or dev_data is null\n");
		return -EPERM;
	}

	di->scp_error_flag = HL7139_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = hl7139_scp_reg_read(&data, reg + i, dev_data);
		if (ret) {
			hwlog_err("scp read failed, reg=0x%x\n", reg + i);
			return -EPERM;
		}
		val[i] = data;
	}

	return 0;
}

static int hl7139_scp_reg_write_block(int reg, const int *val, int num,
	void *dev_data)
{
	int ret;
	int i;
	struct hl7139_device_info *di = dev_data;

	if (!val || !dev_data) {
		hwlog_err("val or dev_data is null\n");
		return -EPERM;
	}

	di->scp_error_flag = HL7139_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = hl7139_scp_reg_write(val[i], reg + i, dev_data);
		if (ret) {
			hwlog_err("scp write failed, reg=0x%x\n", reg + i);
			return -EPERM;
		}
	}

	return 0;
}

static int hl7139_scp_detect_adapter(void *dev_data)
{
	struct hl7139_device_info *di = dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -EPERM;
	}

	return hl7139_fcp_adapter_detect(di);
}

static int hl7139_fcp_reg_read_block(int reg, int *val, int num,
	void *dev_data)
{
	int ret, i;
	u8 data = 0;
	struct hl7139_device_info *di = dev_data;

	if (!val || !dev_data) {
		hwlog_err("val or dev_data is null\n");
		return -EPERM;
	}

	di->scp_error_flag = HL7139_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = hl7139_scp_reg_read(&data, reg + i, dev_data);
		if (ret) {
			hwlog_err("fcp read failed, reg=0x%x\n", reg + i);
			return -EPERM;
		}
		val[i] = data;
	}
	return 0;
}

static int hl7139_fcp_reg_write_block(int reg, const int *val, int num,
	void *dev_data)
{
	int ret, i;
	struct hl7139_device_info *di = dev_data;

	if (!val || !dev_data) {
		hwlog_err("val or dev_data is null\n");
		return -EPERM;
	}

	di->scp_error_flag = HL7139_SCP_NO_ERR;

	for (i = 0; i < num; i++) {
		ret = hl7139_scp_reg_write(val[i], reg + i, dev_data);
		if (ret) {
			hwlog_err("fcp write failed, reg=0x%x\n", reg + i);
			return -EPERM;
		}
	}

	return 0;
}

static int hl7139_fcp_detect_adapter(void *dev_data)
{
	struct hl7139_device_info *di = dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -EPERM;
	}

	return hl7139_fcp_adapter_detect(di);
}

static int hl7139_pre_init(void *dev_data)
{
	struct hl7139_device_info *di = dev_data;

	if (!di) {
		hwlog_err("di is null\n");
		return -EPERM;
	}

	return hl7139_self_check(di);
}

static int hl7139_scp_adapter_reset(void *dev_data)
{
	return hl7139_fcp_adapter_reset(dev_data);
}

static struct hwscp_ops hl7139_hwscp_ops = {
	.chip_name = "hl7139",
	.reg_read = hl7139_scp_reg_read_block,
	.reg_write = hl7139_scp_reg_write_block,
	.reg_multi_read = hl7139_scp_adapter_reg_read_block,
	.detect_adapter = hl7139_scp_detect_adapter,
	.soft_reset_master = hl7139_scp_chip_reset,
	.soft_reset_slave = hl7139_scp_adapter_reset,
	.pre_init = hl7139_pre_init,
};

static struct hwfcp_ops hl7139_hwfcp_ops = {
	.chip_name = "hl7139",
	.reg_read = hl7139_fcp_reg_read_block,
	.reg_write = hl7139_fcp_reg_write_block,
	.detect_adapter = hl7139_fcp_detect_adapter,
	.soft_reset_master = hl7139_fcp_master_reset,
	.soft_reset_slave = hl7139_fcp_adapter_reset,
	.get_master_status = hl7139_fcp_read_switch_status,
	.stop_charging_config = hl7139_fcp_stop_charge_config,
	.is_accp_charger_type = hl7139_is_fcp_charger_type,
};

int hl7139_hwscp_register(struct hl7139_device_info *di)
{
	hl7139_hwscp_ops.dev_data = (void *)di;
	return hwscp_ops_register(&hl7139_hwscp_ops);
}

int hl7139_hwfcp_register(struct hl7139_device_info *di)
{
	hl7139_hwfcp_ops.dev_data = (void *)di;
	return hwfcp_ops_register(&hl7139_hwfcp_ops);
}
