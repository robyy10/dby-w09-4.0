/*
 * Copyright (C) 2013 Huawei Device Co.Ltd
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#ifndef PIL_Q6V5_MSS_LOG
#define PIL_Q6V5_MSS_LOG
#include <linux/types.h>
#include <linux/soc/qcom/smem.h>

void save_modem_reset_log(char reason[], int reasonLength);
void wpss_reset_save_log(char reason[], int reasonLength);

int create_modem_log_queue(void);
void destroy_modem_log_queue(void);

#endif
