/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rstm32g031_scp.h
 *
 * rstm32g031 scp protocol header file
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

#ifndef _RSTM32G031_SCP_H_
#define _RSTM32G031_SCP_H_

#include "rstm32g031.h"

/* recerse_scp */
#define RSTM32G031_SCP_ACK_RETRY_CYCLE                      4
#define RSTM32G031_SCP_RESTART_TIME                         4
#define RSTM32G031_SCP_DATA_LEN                             8
#define RSTM32G031_SCP_RETRY_TIME                           2
#define RSTM32G031_SCP_DETECT_MAX_COUT                      20 /* reverse_scp detect max count */
#define RSTM32G031_SCP_NO_ERR                               0
#define RSTM32G031_SCP_IS_ERR                               1

#define RSTM32G031_ENABLE                                   1
#define RSTM32G031_DISABLE                                  0

/* ID reg=0x00 */
#define RSTM32G031_ID_REG                                   0x00
#define RSTM32G031_ID_REG_MASK                              0xFF
#define RSTM32G031_ID_REG_SHIFT                             0
#define RSTM32G031_ID_REG_VALUE                             0x35

/* FUN_SEL reg=0x01 */
#define RSTM32G031_FUN_SEL_REG                              0x01
#define RSTM32G031_FUN_SEL_REG_MASK                         0xFF
#define RSTM32G031_FUN_SEL_REG_SHIFT                        0
#define RSTM32G031_FUN_SEL_REG_RSCP                         0x01
#define RSTM32G031_FUN_SEL_REG_SCP                          0x02

/* RSCP_CTL reg=0x10 */
#define RSTM32G031_RSCP_CTL_REG                             0x10
#define RSTM32G031_RSCP_CTL_REG_SLEEP_EN_MASK               BIT(2)
#define RSTM32G031_RSCP_CTL_REG_SLEEP_EN_SHIFT              2
#define RSTM32G031_RSCP_CTL_REG_FORCE_RST_MASK              BIT(1)
#define RSTM32G031_RSCP_CTL_REG_FORCE_RST_SHIFT             1
#define RSTM32G031_RSCP_CTL_REG_RSCP_EN_MASK                BIT(0)
#define RSTM32G031_RSCP_CTL_REG_RSCP_EN_SHIFT               0

/* RSCP_CFG reg=0x11 */
#define RSTM32G031_RSCP_CFG_REG                             0x11
#define RSTM32G031_RSCP_CFG_REG_LOG_EN_MASK                 BIT(2)
#define RSTM32G031_RSCP_CFG_REG_LOG_EN_SHIFT                2
#define RSTM32G031_RSCP_CFG_REG_UART_EN_MASK                BIT(1)
#define RSTM32G031_RSCP_CFG_REG_UART_EN_SHIFT               1
#define RSTM32G031_RSCP_CFG_REG_WATCH_DOG_EN_MASK           BIT(0)
#define RSTM32G031_RSCP_CFG_REG_WATCH_DOG_EN_SHIFT          0

/* ISR_EVT reg=0x12 */
#define RSTM32G031_ISR_EVT_REG                              0x12
#define RSTM32G031_ISR_EVT_REG_EVT_NEW_LOG_MASK             BIT(7)
#define RSTM32G031_ISR_EVT_REG_EVT_NEW_LOG_SHIFT            7
#define RSTM32G031_ISR_EVT_REG_EVT_MSTR_RST_MASK            BIT(5)
#define RSTM32G031_ISR_EVT_REG_EVT_MSTR_RST_SHIFT           5
#define RSTM32G031_ISR_EVT_REG_EVT_DP_DEASSERT_MASK         BIT(4)
#define RSTM32G031_ISR_EVT_REG_EVT_DP_DEASSERT_SHIFT        4
#define RSTM32G031_ISR_EVT_REG_EVT_SCP_VI_CFG_MASK          BIT(3)
#define RSTM32G031_ISR_EVT_REG_EVT_SCP_VI_CFG_SHIFT         3
#define RSTM32G031_ISR_EVT_REG_EVT_SCP_EN_TRIG_MASK         BIT(2)
#define RSTM32G031_ISR_EVT_REG_EVT_SCP_EN_TRIG_SHIFT        2
#define RSTM32G031_ISR_EVT_REG_EVT_SCP_FIRST_PING_MASK      BIT(1)
#define RSTM32G031_ISR_EVT_REG_EVT_SCP_FIRST_PING_SHIFT     1
#define RSTM32G031_ISR_EVT_REG_EVT_DCP_DET_MASK             BIT(0)
#define RSTM32G031_ISR_EVT_REG_EVT_DCP_DET_SHIFT            0

/* ISR_ERR reg=0x13 */
#define RSTM32G031_ISR_ERR_REG                              0x13
#define RSTM32G031_ISR_ERR_EVT_SCP_ERR_MAX_COUNT_MASK       BIT(7)
#define RSTM32G031_ISR_ERR_EVT_SCP_ERR_MAX_COUNT_SHIFT      7
#define RSTM32G031_ISR_ERR_ERR_I2C_ERROR_MASK               BIT(5)
#define RSTM32G031_ISR_ERR_ERR_I2C_ERROR_SHIFT              5
#define RSTM32G031_ISR_ERR_ERR_INVALID_REG_MASK             BIT(4)
#define RSTM32G031_ISR_ERR_ERR_INVALID_REG_SHIFT            4
#define RSTM32G031_ISR_ERR_ERR_INVALID_CMD_MASK             BIT(3)
#define RSTM32G031_ISR_ERR_ERR_INVALID_CMD_SHIFT            3
#define RSTM32G031_ISR_ERR_ERR_TIMMING_MASK                 BIT(2)
#define RSTM32G031_ISR_ERR_ERR_TIMMING_SHIFT                2
#define RSTM32G031_ISR_ERR_ERR_CRC_MASK                     BIT(1)
#define RSTM32G031_ISR_ERR_ERR_CRC_SHIFT                    1
#define RSTM32G031_ISR_ERR_ERR_PAR_MASK                     BIT(0)
#define RSTM32G031_ISR_ERR_ERR_ERR_PAR_SHIFT                0

/* SCP_STATUS reg=0x14 */
#define RSTM32G031_SCP_STATUS_REG                           0x14
#define RSTM32G031_SCP_STATUS_REG_MASK                      0XFF
#define RSTM32G031_SCP_STATUS_REG_SHIFT                     0

/* SCP_FSM reg=0x15 */
#define RSTM32G031_SCP_FSM_REG                              0x15
#define RSTM32G031_SCP_FSM_REG_MASK                         0XFF
#define RSTM32G031_SCP_FSM_REG_SHIFT                        0

/* REG_ADDR reg=0x21 */
#define RSTM32G031_REG_ADDR_REG                             0x21
#define RSTM32G031_REG_ADDR_REG_MASK                        0xFF
#define RSTM32G031_REG_ADDR_REG_SHIFT                       0

/* REG_DATA reg=0x22 */
#define RSTM32G031_REG_DATA_REG                             0x22
#define RSTM32G031_REG_DATA_REG_MASK                        0xFF
#define RSTM32G031_REG_DATA_REG_SHIFT                       0

#define RSTM32G031_REG_DATA_REG_VOLTAGE_SET                 0xB8
#define RSTM32G031_REG_DATA_REG_VOLTAGE_GET                 0xA8

#define RSTM32G031_REG_DATA_REG_CURRENT_SET                 0xcb
#define RSTM32G031_REG_DATA_REG_CURRENT_GET                 0xc9
#define RSTM32G031_REG_DATA_REG_CURRENT_STEP                50

#define RSTM32G031_REG_CTRL_BYTE0                           0xa0
#define RSTM32G031_REG_OUTPUT_MODE_MASK                     BIT(6)
#define RSTM32G031_REG_OUTPUT_MODE_SHIFT                    6

#define RSTM32G031_REG_DATA_REG_SSTS_POWER                  0xa5
#define RSTM32G031_REG_SSTS_POWER_DPARTO_MASK               (BIT(1) | BIT(2) | BIT(3))
#define RSTM32G031_REG_SSTS_POWER_DPARTO_SHIFT              1
#define RSTM32G031_REG_SSTS_POWER_ENABLE_MASK               BIT(0)
#define RSTM32G031_REG_SSTS_POWER_ENABLE_SHIFT              0
#define RSTM32G031_REG_SSTS_POWER_ENABLE                    1
#define RSTM32G031_REG_SSTS_POWER_DISABLE                   0
#define RSTM32G031_REG_SSTS_POWER_DROP_FACTOR               8

#define RSTM32G031_REG_DATA_REG_IN_TEMP                     0xa6
#define RSTM32G031_REG_DATA_REG_OUT_TEMP                    0xa7

/* log reg */
#define RSTM32G031_REG_SCP_LOG_STATUS                       0xf0
#define RSTM32G031_REG_SCP_LOG_NOT_EMPTY_MASK               BIT(0)
#define RSTM32G031_REG_SCP_LOG_NOT_EMPTY_SHIFT              0

#define RSTM32G031_REG_SCP_LOG_GET_ONE_DATA                 0xf1
#define RSTM32G031_REG_SCP_LOG_GET_ONE_DATA_VALUE           0x01

#define RSTM32G031_REG_SCP_LOG_INFO                         0xf2
#define RSTM32G031_REG_SCP_LOG_INFO_ERROR_MASK              BIT(2)
#define RSTM32G031_REG_SCP_LOG_INFO_ERROR_SHIFT             2

#define RSTM32G031_REG_SCP_LOG_ADDR                         0xf3
#define RSTM32G031_REG_SCP_LOG_DATA                         0xf4
#define RSTM32G031_REG_SCP_LOG_OLD_DATA                     0xf5

int rstm32g031_hwscp_register(struct rstm32g031_device_info *di);
bool rstm32g031_init_self_check(struct rstm32g031_device_info *di);
int rstm32g031_pre_init_check(struct rstm32g031_device_info *di);
int rstm32g031_enable_sleep(void *dev_data, int enable);
int rstm32g031_ic_reset(void *dev_data);

#endif /* _RSTM32G031_SCP_H_ */
