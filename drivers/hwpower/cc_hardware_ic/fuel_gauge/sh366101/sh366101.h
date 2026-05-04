// SPDX-License-Identifier: GPL-2.0
/*
 * sh366101.h
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

#ifndef _SH366101_H_
#define _SH366101_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <chipset_common/hwpower/coul/coul_interface.h>
#include <chipset_common/hwpower/coul/coul_calibration.h>
#include <chipset_common/hwpower/common_module/power_common_macro.h>
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <chipset_common/hwpower/common_module/power_i2c.h>
#include <chipset_common/hwpower/common_module/power_log.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/battery/battery_model_public.h>
#include <chipset_common/hwpower/battery/battery_soh.h>
#include <chipset_common/hwpower/common_module/power_dts.h>
#include <chipset_common/hwpower/common_module/power_devices_info.h>
#ifdef CONFIG_HUAWEI_HW_DEV_DCT
#include <chipset_common/hwmanufac/dev_detect/dev_detect.h>
#endif

#define SH366101_CMD_ALTMAC                      0x3E
#define SH366101_CMD_ALTBLOCK                    0x40
#define SH366101_CMD_ALTCHK                      0x60
#define SH366101_CMD_SBS_DELAY                   3
#define SH366101_CMDSBS_DELAY_SHIFT              1
#define SH366101_CMDMASK_MASK                    0xFF000000
#define SH366101_CMDMASK_SINGLE                  0x01000000
#define SH366101_CMDMASK_WRITE                   0x80000000
#define SH366101_CMDMASK_ALTMAC_R                0x08000000
#define SH366101_CMDMASK_ALTMAC_W                (SH366101_CMDMASK_WRITE | SH366101_CMDMASK_ALTMAC_R)

#define SH366101_CMD_IAPSTATE_CHECK              0xA0
#define SH366101_CMD_UNSEALKEY                   0x80008000
#define SH366101_CMD_SEAL                        0x20
#define SH366101_CMD_AFI_STATIC_SUM              (SH366101_CMDMASK_ALTMAC_R | 0x00CD)
#define SH366101_CMD_AFI_CHEMID                  (SH366101_CMDMASK_ALTMAC_R | 0x0008)
#define SH366101_CMD_FWVERSION_MAIN              (SH366101_CMDMASK_ALTMAC_R | 0x00F1)
#define SH366101_CMD_FWDATE1                     (SH366101_CMDMASK_ALTMAC_R | 0x00F5)
#define SH366101_CMD_FWDATE2                     (SH366101_CMDMASK_ALTMAC_R | 0x00F6)
#define SH366101_CMD_TS_VER                      (SH366101_CMDMASK_ALTMAC_R | 0x00CC)
#define SH366101_CMD_KEY_SHIFT                   16

#define SH366101_TEMP_ABR_LOW                    (-400)
#define SH366101_TEMP_ABR_HIGH                   800

#define SH366101_IAP_READ_LEN                    2
#define SH366101_RATIO_READ_LEN                  2
#define SH366101_FW_DATE_MASK                    0x00FF
#define SH366101_FW_DATE_SHIFT                   16

/* version check */
#define SH366101_CHECK_VERSION_ERR               (-1)
#define SH366101_CHECK_VERSION_OK                0
#define SH366101_CHECK_VERSION_FW                BIT(0)
#define SH366101_CHECK_VERSION_AFI               BIT(1)
#define SH366101_CHECK_VERSION_TS                BIT(2)
#define SH366101_CHECK_VERSION_WHOLE_CHIP        (SH366101_CHECK_VERSION_FW | SH366101_CHECK_VERSION_AFI | SH366101_CHECK_VERSION_TS)
#define SH366101_CMD_SECTOR_FLAG                 (SH366101_CMDMASK_ALTMAC_R | 0x00C7)
#define SH366101_MASK_SECTOR_FLAG                0x0FFF

/* file decode process */
#define SH366101_OPERATE_READ                    1
#define SH366101_OPERATE_WRITE                   2
#define SH366101_OPERATE_COMPARE                 3
#define SH366101_OPERATE_WAIT                    4

#define SH366101_ERRORTYPE_NONE                  0
#define SH366101_ERRORTYPE_ALLOC                 1
#define SH366101_ERRORTYPE_LINE                  2
#define SH366101_ERRORTYPE_COMM                  3
#define SH366101_ERRORTYPE_COMPARE               4

#define SH366101_INDEX_TYPE                      0
#define SH366101_INDEX_ADDR                      1
#define SH366101_INDEX_REG                       2
#define SH366101_INDEX_LENGTH                    3
#define SH366101_INDEX_DATA                      4
#define SH366101_INDEX_WAIT_LENGTH               1
#define SH366101_INDEX_WAIT_HIGH                 2
#define SH366101_INDEX_WAIT_LOW                  3
#define SH366101_INDEX_WAIT_UNIT                 256
#define SH366101_LINELEN_WAIT                    4
#define SH366101_LINELEN_READ                    4
#define SH366101_LINELEN_COMPARE                 4
#define SH366101_LINELEN_WRITE                   4
#define SH366101_FILEDECODE_STRLEN               96
#define SH366101_COMPARE_RETRY_CNT               2
#define SH366101_COMPARE_RETRY_WAIT              50
#define SH366101_BUF_MAX_LENGTH                  512
#define SH366101_DECODE_BUF_LENGTH               32
#define SH366101_WRITE_BUF_MAX_LEN               160
#define SH366101_FILE_DECODE_RETRY               2
#define SH366101_FILE_DECODE_DELAY               100
#define SH366101_WRITE_BUF_MAX_LEN               160
#define SH366101_PRINT_BUFFER_FORMAT_LEN         3
#define SH366101_CMD_USERBUFFER                  (SH366101_CMDMASK_ALTMAC_W | 0x70)
#define SH366101_LEN_USERBUFFER                  32
#define SH366101_CMD_OEMFLAG                     (SH366101_CMDMASK_ALTMAC_R | 0xC1)
#define SH366101_CMD_MASK_OEM_LIFETIMEEN         0x8000
#define SH366101_CMD_MASK_OEM_GAUGEEN            0x4000

/* gauge enable */
#define SH366101_CMD_ENABLE_GAUGE                0x0021
#define SH366101_CMD_ENABLE_LIFETIME             0x0067
#define SH366101_DELAY_ENABLE_GAUGE              1000 /* write e2rom and gauge init */

#define SH366101_CMD_RESET                       0x0041
#define SH366101_DELAY_RESET                     1000

#define SH366101_CMD_OEMINFO                     (SH366101_CMDMASK_ALTMAC_R | 0x00C5)
#define SH366101_INDEX_OEMINFO_INTFLAG           10
#define SH366101_MASK_OEMINFO_AUTOCALI           0x0080

#define SH366101_DELTA_VOLT                      (200 * SH366101_MA_TO_UA)
#define SH366101_VOLT_MIN_RESET                  (3750 * SH366101_MA_TO_UA)
#define SH366101_SOC_MIN_RESET                   3
#define SH366101_TEMPER_MIN_RESET                (-450)

#define SH366101_CMD_TERMINATEVOLT               (SH366101_CMDMASK_ALTMAC_W | 0xB1)
#define SH366101_LEN_TERMINATEVOLT               3

#define SH366101_MA_TO_UA                        1000
#define SH366101_UA_PER_MA                       1000

#define SH366101_ERRORTYPE_NONE                  0

#define SH366101_CMD_READ_CALICURRENT            (SH366101_CMDMASK_ALTMAC_R | 0xE014)
#define SH366101_CMD_MASK_OEM_CALI               0x000C
#define SH366101_DELAY_WRITEE2ROM                800

/* gaugeinfo block */
#define SH366101_CALIINFO_LEN                    32
#define SH366101_GAUGEINFO_LEN                   32
#define SH366101_GAUGESTR_LEN                    512
#define SH366101_FUSIONMODEL_CNT                 15
#define SH366101_GAUGE_LOG_MIN_TIMESPAN          5
#define SH366101_GAUGE_RUNSTATE_RETRY            20
#define SH366101_U64_MAXVALUE                    0xFFFFFFFFFFFFFFFF
#define SH366101_CMD_CALIINFO                    (SH366101_CMDMASK_ALTMAC_R | 0xE014)
#define SH366101_TEMPER_OFFSET                   2731
#define SH366101_CMD_CHARGESTATUS                (SH366101_CMDMASK_ALTMAC_R | 0x55)
#define SH366101_LEN_CHARGESTATUS                7
#define SH366101_BUF2U16_SHIFT                   16
#define SH366101_BUF2U32_SHIFT                   16

#define SH366101_CMD_CONTROLSTATUS               (SH366101_CMDMASK_ALTMAC_R | 0x0000)
#define SH366101_CMD_RUNFLAG                     0x06
#define SH366101_CMD_LIFETIMEADC                 (SH366101_CMDMASK_ALTMAC_R | 0x6B)
#define SH366101_LEN_LIFETIMEADC                 12
#define SH366101_CMD_GAUGEINFO                   (SH366101_CMDMASK_ALTMAC_R | 0xE0)
#define SH366101_CMD_GAUGEBLOCK1                 (SH366101_CMDMASK_ALTMAC_R | 0xE1)
#define SH366101_CMD_GAUGEBLOCK2                 (SH366101_CMDMASK_ALTMAC_R | 0xE2)
#define SH366101_CMD_GAUGEBLOCK3                 (SH366101_CMDMASK_ALTMAC_R | 0xE3)
#define SH366101_CMD_GAUGEBLOCK4                 (SH366101_CMDMASK_ALTMAC_R | 0xE4)
#define SH366101_CMD_GAUGEBLOCK5                 (SH366101_CMDMASK_ALTMAC_R | 0xE5)
#define SH366101_CMD_GAUGEBLOCK6                 (SH366101_CMDMASK_ALTMAC_R | 0xE6)
#define SH366101_CMD_GAUGEBLOCK7                 (SH366101_CMDMASK_ALTMAC_R | 0xE7)
#define SH366101_CMD_GAUGEBLOCK_FG               (SH366101_CMDMASK_ALTMAC_R | 0xEF)
#define SH366101_CMD_READBLOCK_TIME              15
#define SH366101_CMD_CALIINFO                    (SH366101_CMDMASK_ALTMAC_R | 0xE014)
#define SH366101_CMD_CADCINFO                    (SH366101_CMDMASK_ALTMAC_R | 0xCF)
#define SH366101_CMD_E2ROM_SENSORRATIO           (SH366101_CMDMASK_ALTMAC_W | 0x405C)

#define SH366101_CMD_LEN                         1

/* read status flag */
#define SH366101_STATUS_HIGH_TEMPERATURE         BIT(15)
#define SH366101_STATUS_LOW_TEMPERATURE          BIT(14)
#define SH366101_STATUS_TERM_SOC                 BIT(10)
#define SH366101_STATUS_FULL_SOC                 BIT(9)
#define SH366101_STATUS_BATT_PRESENT             BIT(3)
#define SH366101_STATUS_LOW_SOC2                 BIT(2)
#define SH366101_STATUS_LOW_SOC1                 BIT(1)
#define SH366101_OP_STATUS_CHG_DISCHG            BIT(0)

#define SH366101_CURRENT_ZERO                    100
#define SH366101_CMD_E2ROM_DEADBAND              (SH366101_CMDMASK_ALTMAC_W | 0x404D)

/* calibration */
#define SH366101_CMD_ENTER_CALI                  (SH366101_CMDMASK_ALTMAC_W | 0xE014)
#define SH366101_CMD_ENTER_OEM                   (SH366101_CMDMASK_ALTMAC_W | 0xE001)
#define SH366101_CMD_EXIT_CALI                   (SH366101_CMDMASK_ALTMAC_W | 0xE015)
#define SH366101_CMD_EXIT_OEM                    (SH366101_CMDMASK_ALTMAC_W | 0xE018)
#define SH366101_CMD_MASK_OEM_UNSEAL             0x03

#define SH366101_LEN_ENTER_OEM                   8
#define SH366101_LEN_ENTER_CALI                  6
#define SH366101_LEN_EXIT_CALI                   6
#define SH366101_LEN_EXIT_OEM                    7

#define SH366101_CMD_FORCE_UPDATE_E2ROM          (SH366101_CMDMASK_ALTMAC_W | 0xE012)
#define SH366101_LEN_FORCE_UPDATE_E2ROM          4

#define SH366101_CMD_E2ROM_BOARDOFFSET           (SH366101_CMDMASK_ALTMAC_W | 0x4044)
#define SH366101_OFFSET_RATIO1                   0x10
#define SH366101_OFFSET_RATIO2                   1000
#define SH366101_CMD_E2ROM_CURRENTRATIO          (SH366101_CMDMASK_ALTMAC_W | 0x4040)
#define SH366101_RATIO_RATIO                     0x4000
#define SH366101_CURRENT_INIT                    2.5
#define SH366101_OFFSET_INIT                     (-60 * 1000)

#define SH366101_DEFAULT_SENSOR_RATIO            1000
#define SH366101_USER_SENSOR_RATIO               2000

#define SH366101_DEFAULT_CURRENT_RATIO           1000000
#define SH366101_DEFAULT_CURRENT_OFFSET          0

#define SH366101_CALI_WAIT_TIME                  200

#define SH366101_I2C_READ_LEN                    2
#define SH366101_I2C_WRITE_LEN                   1
#define SH366101_I2C_BLOCK_SHIFT                 8
#define SH366101_I2C_LEN_ADD                     4

#define SH366101_BLOCK_LEN_MAX                   32
#define SH366101_TEST_DATA_DEFAULT               10
#define SH366101_DEADBAND_DEFAULT                10

#define SH366101_QUEUE_DELAYED_WORK_TIME         5000
#define SH366101_FULL_CAPACITY                   100

enum sh366101_temperature_type {
	SH366101_TEMPERATURE_IN = 0,
	SH366101_TEMPERATURE_EX,
};

enum SH366101_reg_index {
	SH366101_REG_DEVICE_ID = 0,
	SH366101_REG_CNTL,
	SH366101_REG_INT,
	SH366101_REG_STATUS,
	SH366101_REG_SOC,
	SH366101_REG_OCV,
	SH366101_REG_VOLTAGE,
	SH366101_REG_CURRENT,
	SH366101_REG_SH366101_TEMPERATURE_IN,
	SH366101_REG_SH366101_TEMPERATURE_EX,
	SH366101_REG_BAT_RMC,
	SH366101_REG_BAT_FCC,
	SH366101_REG_RESET,
	SH366101_REG_SOC_CYCLE,
	SH366101_REG_DESIGN_CAPCITY,
	SH366101_NUM_REGS,
};

static unsigned int sh366101_regs[SH366101_NUM_REGS] = {
	SH366101_CMDMASK_ALTMAC_R | 0x0001, /* device_id */
	SH366101_CMDMASK_ALTMAC_R | 0x0000, /* cntl */
	SH366101_CMDMASK_SINGLE | 0x6E, /* int */
	0x06, /* status */
	0x1C, /* soc */
	0x64, /* ocv */
	0x04, /* voltage */
	0x10, /* current */
	0x1E, /* sh366101_temperature_in */
	0x02, /* sh366101_temperature_ex */
	0x0C, /* bat_rmc */
	0x0E, /* bat_fcc */
	SH366101_CMDMASK_ALTMAC_W | 0x41, /* reset */
	0x1A, /* soc_cycle */
	0x3C, /* sh366101_reg_design_capcity */
};

enum {
	SH366101_IC_TYPE_MAIN = 0,
	SH366101_IC_TYPE_AUX,
	SH366101_IC_TYPE_MAX,
};

struct sh366101_dev {
	struct device *dev;
	struct i2c_client *client;
	struct mutex i2c_rw_lock; /* i2c read/write lock */
	struct mutex data_lock; /* data lock */
	struct mutex cali_lock;
	unsigned int ic_role;
	int chip_id;
	struct workqueue_struct *shfg_workqueue;
	struct delayed_work battery_delay_work;
	int voltage;
	int ui_soc;
	int curr;
	int temp;
	int cycle;
	unsigned int regs[SH366101_NUM_REGS];
	unsigned int test_data;
	int is_enable_autocali;
	/* status tracking */
	bool batt_present;
	bool batt_fc; /* battery full condition */
	bool batt_tc; /* battery full condition */
	bool batt_ot; /* battery over temperature */
	bool batt_ut; /* battery under temperature */
	bool batt_soc1; /* soc low */
	bool batt_socp; /* soc poor */
	bool batt_dsg; /* discharge condition */
	int batt_soc;
	int batt_ocv;
	int batt_fcc; /* full charge capacity */
	int batt_rmc; /* remaining capacity */
	int batt_volt;
	int aver_batt_volt;
	int batt_temp;
	int batt_curr;
	int batt_soc_cycle; /* battery soc cycle */
	int batt_designcap;
	int current_ratio;
	int current_ratio_pre;
	int current_offset;
	unsigned long long last_update;
	unsigned int version_main;
	unsigned int version_date;
	unsigned int version_ts;
	unsigned int version_afi;
	unsigned int version_afi2;
	unsigned int chip_afi;
	unsigned int chip_afi2;
	bool en_temp_ex;
	bool en_temp_in;
	bool fast_mode;
	unsigned char deadband;
	int sensor_ratio;
	unsigned long long log_last_update;
	int terminate_voltage;
};

struct sh_decoder {
	unsigned char addr;
	unsigned char reg;
	unsigned char length;
	unsigned char buf_first_val;
};

#endif /* _SH366101_H_ */
