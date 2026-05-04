/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: get dynamic information and health information of block device
 * Author: yanghan
 * Create: 2023-01-04
 */
#ifndef DISK_MAGO_INFO_H
#define DISK_MAGO_INFO_H

#define CARD_BLOCK_SIZE 512
#define MMC_SEND_EXT_CSD 8                /* adtc R1 */
#define NUM_0 0
#define NUM_1 1
#define NUM_2 2
#define NUM_3 3
#define NUM_4 4
#define NUM_5 5
#define NUM_6 6
#define NUM_7 7
#define NUM_8 8
#define NUM_9 9
#define NUM_10 10
#define HEALTH_DESP_IDN 0x9
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A 268 /* RO */
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B 269 /* RO */
#define READ_DESP_OPCODE 0x1
#define UFS_DESC_SIZE_MAX 128
#define MAX_EXPIRED_TIME  100
#define TOTAL_TIMES       100

struct health_descrptor {
	unsigned char length;
	unsigned char descriptor_type;
	unsigned char pre_eol_info;
	unsigned char lifetime_est_type_a;
	unsigned char lifetime_est_type_b;
};

struct vendor_health_info_data {
	uint8_t lifetime_est_type_a; /* bDeviceLifeTimeEstA */
	uint8_t lifetime_est_type_b; /* bDeviceLifeTimeEstB */
};
#endif
