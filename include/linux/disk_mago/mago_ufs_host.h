/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: provide mago latency function to ufs driver
 * Author: gaoenbo
 * Create: 2023-02-13
 */
#ifndef MAGO_UFS_HOST_H
#define MAGO_UFS_HOST_H

void mago_hba_host_pre_init(struct ufs_hba *hba);
void mago_ufs_io_latency_cmd_end(struct Scsi_Host *scsi_host, struct scsi_cmnd *cmd);
void mago_ufs_host_init(struct Scsi_Host *host);
void mago_ufs_host_exit(struct Scsi_Host *host);
#endif
