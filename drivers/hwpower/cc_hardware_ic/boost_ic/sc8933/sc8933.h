/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sc8933.h
 *
 * sc8933 header file
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

#ifndef _SC8933_H_
#define _SC8933_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <chipset_common/hwpower/common_module/power_log.h>
#include <chipset_common/hwpower/reverse_charge/reverse_charge.h>

#define CHIP_DEV_NAME_LEN                     32
#define SC8933_BUF_LEN                        80
#define BUF_LEN                               26
#define SC8933_REG_NUM                        17

#define SC8933_IDLE_STATE                     1
#define SC8933_NORMAL_STATE                   0
#define SC8933_SENSE_R_TYPICAL                100
#define SC8933_SENSE_R_ACTUAL                 33

struct sc8933_device_info {
	struct i2c_client *client;
	struct device *dev;
	struct work_struct irq_work;
	struct workqueue_struct *int_wq;
	struct power_log_ops log_ops;
	struct boost_ops bst_ops;
	char name[CHIP_DEV_NAME_LEN];
	int gpio_int;
	int irq_int;
	int vbus_comp;
	int sense_r_typical;
	int sense_r_actual;
	int chip_already_init;
};

/* MODE reg=0x01 */
#define SC8933_MODE_REG                       0x01
#define SC8933_SHORT_CTRL_MASK                BIT(7)
#define SC8933_SHORT_CTRL_SHIFT               7
#define SC8933_FB_SEL_MASK                    BIT(6)
#define SC8933_FB_SEL_SHIFT                   6
#define SC8933_ITRKL_SET_MASK                 BIT(5)
#define SC8933_ITRKL_SET_SHIFT                5
#define SC8933_VLDO_SET_MASK                  BIT(4)
#define SC8933_VLDO_SET_SHIFT                 4
#define SC8933_VB_SET_MASK                    BIT(3)
#define SC8933_VB_SET_SHIFT                   3
#define SC8933_C_DIR_MASK                     BIT(2)
#define SC8933_C_DIR_SHIFT                    2
#define SC8933_PASS_THROUGH_MASK              BIT(1)
#define SC8933_PASS_THROUGH_SHIFT             1
#define SC8933_DIR_MASK                       BIT(0)
#define SC8933_DIR_SHIFT                      0

/* PWR_SET reg=0x02 */
#define SC8933_PWR_SET_REG                    0x02
#define SC8933_FSW_SET_MASK                   (BIT(7) | BIT(6))
#define SC8933_FSW_SET_SHIFT                  6
#define SC8933_FORCE_CH_MASK                  BIT(4)
#define SC8933_FORCE_CH_SHIFT                 4
#define SC8933_IDLE_MASK                      BIT(3)
#define SC8933_IDLE_SHIFT                     3
#define SC8933_DRV_LS_SET_MASK                BIT(2)
#define SC8933_DRV_LS_SET_SHIFT               2
#define SC8933_DRV_HS_SET_MASK                BIT(1)
#define SC8933_DRV_HS_SET_SHIFT               1
#define SC8933_PSTOP_MASK                     BIT(0)
#define SC8933_PSTOP_SHIFT                    0

/* PWR_PATH reg=0x03 */
#define SC8933_PWR_PATH_REG                   0x03
#define SC8933_OVP_SET_MASK                   BIT(7)
#define SC8933_OVP_SET_SHIFT                  7
#define SC8933_BLOCKSS_MASK                   BIT(5)
#define SC8933_BLOCKSS_SHIFT                  5
#define SC8933_VBUS_DISPATH_MASK              BIT(4)
#define SC8933_VBUS_DISPATH_SHIFT             4
#define SC8933_VC_DISPATH_MASK                BIT(3)
#define SC8933_VC_DISPATH_SHIFT               3
#define SC8933_VAG_ON_MASK                    BIT(2)
#define SC8933_VAG_ON_SHIFT                   2
#define SC8933_VBG_ON_MASK                    BIT(1)
#define SC8933_VBG_ON_SHIFT                   1
#define SC8933_VCG_ON_MASK                    BIT(0)
#define SC8933_VCG_ON_SHIFT                   0

/* IMON reg=0x04 */
#define SC8933_IMON_REG                       0x04
#define SC8933_INDET_SET_MASK                 BIT(7)
#define SC8933_INDET_SET_SHIFT                7
#define SC8933_RECH_SET_MASK                  BIT(6)
#define SC8933_RECH_SET_SHIFT                 6
#define SC8933_EN_OOA_MASK                    BIT(5)
#define SC8933_EN_OOA_SHIFT                   5
#define SC8933_CBLCOMP_CTRL_MASK              (BIT(4) | BIT(3))
#define SC8933_CBLCOMP_CTRL_SHIFT             3
#define SC8933_IMON_SEL_MASK                  (BIT(2) | BIT(1) | BIT(0))
#define SC8933_IMON_SEL_SHIFT                 0

/* CHAR_SET reg=0x05 */
#define SC8933_CHAR_SET_REG                   0x05
#define SC8933_VINREG_SET_MASK                (BIT(7) | BIT(6) | BIT(5))
#define SC8933_VINREG_SET_SHIFT               5
#define SC8933_ITERM_SET_MASK                 (BIT(4) | BIT(3))
#define SC8933_ITERM_SET_SHIFT                3
#define SC8933_VBAT_SET_MASK                  (BIT(2) | BIT(1) | BIT(0))
#define SC8933_VBAT_SET_SHIFT                 0

/* IBUS_LIM reg=0x06 */
#define SC8933_IBUS_LIM_REG                   0x06
#define SC8933_IBUS_LIM_MASK                  0xFF
#define SC8933_IBUS_LIM_SHIFT                 0
#define SC8933_IBUS_LIM_BASE                  25

/* VBUS_SET_MSB reg=0x07 */
#define SC8933_VBUS_SET_MSB_REG               0x07
#define SC8933_VBUS_SET_MSB_MASK              0xFF
#define SC8933_VBUS_SET_MSB_SHIFT             0
#define SC8933_VBUS_BASE                      5000

/* VBUS_SET_LSB reg=0x08 */
#define SC8933_VBUS_SET_LSB_REG               0x08
#define SC8933_VBUS_SET_LSB_MASK              (BIT(1) | BIT(0))
#define SC8933_VBUS_SET_LSB_SHIFT             0
#define SC8933_VBUS_LSB                       10
#define SC8933_VBUS_LOW_BITS                  2

/* LOAD reg=0x09 */
#define SC8933_LOAD_REG                       0x09
#define SC8933_IBUS_LIM_LOAD_MASK             BIT(1)
#define SC8933_IBUS_LIM_LOAD_SHIFT            1
#define SC8933_VBUS_SET_LOAD_MASK             BIT(0)
#define SC8933_VBUS_SET_LOAD_SHIFT            0
#define SC8933_IBUS_LIM_LOAD_ENABLE           1
#define SC8933_VBUS_SET_LOAD_ENABLE           1

/* IBAT_LIM reg=0x0A */
#define SC8933_IBAT_LIM_REG                   0x0A
#define SC8933_MIN_IBUS_CLAMP_SET_MASK        (BIT(3) | BIT(2))
#define SC8933_MIN_IBUS_CLAMP_SET_SHIFT       2
#define SC8933_REG_IBAT_LIM_MASK              (BIT(1) | BIT(0))
#define SC8933_REG_IBAT_LIM_SHIFT             0

/* PRO_SET reg=0x0B */
#define SC8933_PRO_SET_REG                    0x0B
#define SC8933_DIS_TERM_MASK                  BIT(7)
#define SC8933_DIS_TERM_SHIFT                 7
#define SC8933_VBUS_UVP_SET_MASK              BIT(6)
#define SC8933_VBUS_UVP_SET_SHIFT             6
#define SC8933_EN_VBATOVP_MASK                BIT(5)
#define SC8933_EN_VBATOVP_SHIFT               5
#define SC8933_EN_VBUSUVP_MASK                BIT(4)
#define SC8933_EN_VBUSUVP_SHIFT               4
#define SC8933_SHORT_AUTO_RTR_MASK            BIT(3)
#define SC8933_SHORT_AUTO_RTR_SHIFT           3
#define SC8933_EN_INUVP_MASK                  BIT(2)
#define SC8933_EN_INUVP_SHIFT                 2
#define SC8933_INOVP_TH_MASK                  BIT(1)
#define SC8933_INOVP_TH_SHIFT                 1
#define SC8933_EN_ABSOVP_MASK                 BIT(0)
#define SC8933_EN_ABSOVP_SHIFT                0

/* CTRL reg=0x0C */
#define SC8933_CTRL_REG                       0x0C
#define SC8933_RESET_MASK                     BIT(2)
#define SC8933_RESET_SHIFT                    2
#define SC8933_SHORT_RST_MASK                 BIT(0)
#define SC8933_SHORT_RST_SHIFT                0
#define SC8933_RESET_ENABLE                   1

/* LOOP_STA reg=0x0D */
#define SC8933_LOOP_STA_REG                   0x0D
#define SC8933_IBAT_LOOP_MASK                 BIT(3)
#define SC8933_IBAT_LOOP_SHIFT                3
#define SC8933_VINREG_LOOP_MASK               BIT(2)
#define SC8933_VINREG_LOOP_SHIFT              2
#define SC8933_CC_LOOP_MASK                   BIT(1)
#define SC8933_CC_LOOP_SHIFT                  1
#define SC8933_CV_LOOP_MASK                   BIT(0)
#define SC8933_CV_LOOP_SHIFT                  0

/* STA1 reg=0x0E */
#define SC8933_STA1_REG                       0x0E
#define SC8933_OTP_MASK                       BIT(5)
#define SC8933_OTP_SHIFT                      5
#define SC8933_NTC_HOT_MASK                   BIT(4)
#define SC8933_NTC_HOT_SHIFT                  4
#define SC8933_NTC_COOL_CH_MASK               BIT(3)
#define SC8933_NTC_COOL_CH_SHIFT              3
#define SC8933_NTC_COLD_MASK                  BIT(2)
#define SC8933_NTC_COLD_SHIFT                 2
#define SC8933_EOC_MASK                       BIT(1)
#define SC8933_EOC_SHIFT                      1
#define SC8933_ICOM_MASK                      BIT(0)
#define SC8933_ICOM_SHIFT                     0

/* STA2 reg=0x0F */
#define SC8933_STA2_REG                       0x0F
#define SC8933_FB_SC_MASK                     BIT(6)
#define SC8933_FB_SC_SHIFT                    6
#define SC8933_VBAT_OVP_MASK                  BIT(5)
#define SC8933_VBAT_OVP_SHIFT                 5
#define SC8933_ABSOVP_MASK                    BIT(4)
#define SC8933_ABSOVP_SHIFT                   4
#define SC8933_C_IN_OVP_MASK                  BIT(3)
#define SC8933_C_IN_OVP_SHIFT                 3
#define SC8933_B_IN_OVP_MASK                  BIT(2)
#define SC8933_B_IN_OVP_SHIFT                 2
#define SC8933_C_IN_UVP_MASK                  BIT(1)
#define SC8933_C_IN_UVP_SHIFT                 1
#define SC8933_B_IN_UVP_MASK                  BIT(0)
#define SC8933_B_IN_UVP_SHIFT                 0

/* INT reg=0x10 */
#define SC8933_INT_REG                        0x10
#define SC8933_VBUS_SHORT_MASK                BIT(4)
#define SC8933_VBUS_SHORT_SHIFT               4
#define SC8933_INDET_B_MASK                   BIT(3)
#define SC8933_INDET_B_SHIFT                  3
#define SC8933_INDET_A_MASK                   BIT(2)
#define SC8933_INDET_A_SHIFT                  2
#define SC8933_VC_ACOK_MASK                   BIT(1)
#define SC8933_VC_ACOK_SHIFT                  1
#define SC8933_VB_ACOK_MASK                   BIT(0)
#define SC8933_VB_ACOK_SHIFT                  0

/* INT_MASK reg=0x11 */
#define SC8933_INT_MASK_REG                   0x11
#define SC8933_VBUS_SHORT_M_MASK              BIT(4)
#define SC8933_VBUS_SHORT_M_SHIFT             4
#define SC8933_INDET_B_M_MASK                 BIT(3)
#define SC8933_INDET_B_M_SHIFT                3
#define SC8933_INDET_A_M_MASK                 BIT(2)
#define SC8933_INDET_A_M_SHIFT                2
#define SC8933_VC_ACOK_M_MASK                 BIT(1)
#define SC8933_VC_ACOK_M_SHIFT                1
#define SC8933_VB_ACOK_M_MASK                 BIT(0)
#define SC8933_VB_ACOK_M_SHIFT                0

int sc8933_get_vbus_uvp_status(void *dev_data);

#endif /* _SC8933_H_ */
