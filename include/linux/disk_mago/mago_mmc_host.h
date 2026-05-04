/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: provide mago latency function to mmc driver
 * Author: gaoenbo
 * Create: 2023-02-13
 */
#ifndef MAGO_MMC_HOST_H
#define MAGO_MMC_HOST_H

struct sdhci_host;
void mago_mmc_host_pre_init(struct sdhci_host *host);
void mago_mmc_io_latency_mrq_end(struct mmc_host *mmc_host, struct mmc_request *mrq);
void mago_mmc_host_init(struct mmc_host *host);
void mago_mmc_host_exit(struct mmc_host *host);
#endif
