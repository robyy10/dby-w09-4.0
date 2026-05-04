/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>

/* bbox/BBOX - blackbox */
#define YEAR_BASE 1900
#define SECONDS_PER_MINUTE 60
#define AID_ROOT 0
#define AID_SYSTEM 1000
#define BBOX_DIR_LIMIT 0775
#define BBOX_FILE_LIMIT 0664
#define PATH_MAX_LEN 256

/*
 * format:
 * [topCategoryName],module[moduleName],category[categoryName],\
 * event[eventName],time[seconds from 1970-01-01 00:00:00 UTC-tick],\
 * sysreboot[true|false],errordesc[errorDescription],logpath[logpath]\n
 */
#define HISTORY_LOG_FORMAT "[%s],module[%s],category[%s],event[%s],"\
	"time[%s],sysreboot[%s],errdesc[%s],logpath[%s]\n"
#define TIMESTAMP_FORMAT "%04d%02d%02d%02d%02d%02d-%08llu"

void sys_reset(void);
void change_own_mode(char *path, int uid, int gid, int mode);
int full_write_file(const char *pfile_path, char *buf,
		    size_t buf_size, bool read_file);
int create_log_dir(const char *path);
void get_timestamp(char *buf, size_t buf_size);
unsigned long long get_ticks(void);
