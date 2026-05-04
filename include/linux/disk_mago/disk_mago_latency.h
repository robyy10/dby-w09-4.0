/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: get latency information and provide judge disk mago function
 * Author: gaoenbo
 * Create: 2023-02-13
 */
#ifndef DISK_MAGO_LATENCY_H
#define DISK_MAGO_LATENCY_H

struct mago_io_latency_stat {
	struct delayed_work mago_latency_work;
	unsigned long queue_run_interval; /* Interval time of work queue */
	char *host_name;
	unsigned long rdiff;              /* Accumulate read io latency */
	unsigned long rios;               /* Accumulate read io number */
	unsigned long wdiff;              /* Accumulate write io latency */
	unsigned long wios;               /* Accumulate write io number */
	unsigned long max_rdiff;          /* Current read max latency in work queue */
	unsigned long max_wdiff;          /* Current write max latency in work queue */

	unsigned int read_status;         /* Read io latency status */
	unsigned int write_status;        /* Write io latency status */
	unsigned int avg_io_rlatency;     /* Read avarage latency */
	unsigned int max_io_rlatency;     /* Read max latency */
	unsigned int avg_io_wlatency;     /* Write avarage latency */
	unsigned int max_io_wlatency;     /* Write max latency */
};

struct block_dev_dyn_info;
struct gendisk;
int get_latency_info(struct gendisk *disk, struct block_dev_dyn_info *dinfo);
bool is_disk_mago(void);
#endif
