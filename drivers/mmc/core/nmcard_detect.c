/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2027. All Rights Reserved.
 * Description: nmcard detect.
 * Author: yangke
 * Create: 2021-4-20
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/regulator/consumer.h>
#include <linux/mmc/nmcard_detect.h>

#include "core.h"
#include "mmc_ops.h"
#include "pwrseq.h"
#include "../host/sdhci-msm.h"

#define SLEEP_MS_TIME_FOR_DETECT_UNSTABLE	20
#define SD_1V8_DISABLE	0
#define SD_1V8_ENABLE	1
#define OCR_REGISTER_BIT_0_TO_27_MASK 0xFFFFFFF
#define OCR_REGISTER_SD 0xFF8080
#define OCR_REGISTER_BIT_0_TO_27_MASK 0xFFFFFFF
#define OCR_REGISTER_SD 0xFF8080

static DEFINE_MUTEX(card_insert_muxlock);
static const unsigned freqs[] = { 400000, 300000, 200000, 100000 };
static bool g_detect_first_flag = 0;

bool sdhci_msm_vdd_is_enabled(struct mmc_host *mmc_host)
{
	int ret = 0;
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = pltfm_host->priv;
	struct sdhci_msm_pltfm_data *pdata = msm_host->pdata;
	struct sdhci_msm_slot_reg_data *curr_slot;

	curr_slot = pdata->vreg_data;
	if (!curr_slot) {
		pr_err("%s: vreg info unavailable,assuming vdd is enabled\n", __func__);
		return true;
	}
	ret = regulator_is_enabled(curr_slot->vdd_data->reg);
	if (ret)
		return true;
	else
		return false;
}

int mmc_detect_mmc(struct mmc_host *host)
{
	int err;
	u32 ocr;

	BUG_ON(!host);
	WARN_ON(!host->claimed);
	/* Set correct bus mode for MMC before attempting attach */
	if (!mmc_host_is_spi(host))
		mmc_set_bus_mode(host, MMC_BUSMODE_OPENDRAIN);

	err = mmc_send_op_cond(host, 0, &ocr);
	pr_err("%s:%d err=%d ocr=0x%08X bit[0:27]:0x%08X\n",
		__func__, __LINE__, err, ocr,
		ocr & OCR_REGISTER_BIT_0_TO_27_MASK);

	if ((ocr & OCR_REGISTER_BIT_0_TO_27_MASK) != OCR_REGISTER_SD) {
		pr_err("%s:%d OCR register bit[0:27] is not 0x0FF8080!!\n",
			__func__, __LINE__);
		err = -1;
	}

	return err;
}

/* return 0 if sd or mmc card detected,return non-0 if not */
static int mmc_rescan_detect_nm_card(struct mmc_host *host, unsigned int freq)
{
	int nm_card_detected = -1;
	host->f_init = freq;
	mmc_power_up(host, host->ocr_avail);
	/*
	 * Some eMMCs (with VCCQ always on) may not be reset after power up, so
	 * do a hardware reset if possible.
	 */

	mmc_hw_reset_for_init(host);
	mmc_go_idle(host);

	if (!(host->caps2 & MMC_CAP2_NO_MMC)) {
		if (!mmc_detect_mmc(host))
			nm_card_detected = 0;
	}

	mmc_power_off(host);
	return nm_card_detected;
}

int mmc_detect_nm_card(struct mmc_host *host)
{
	int i = 0;
	int nm_card_detected = -1;
	if (!mmc_rescan_detect_nm_card(host, max(freqs[i], host->f_min)))
		nm_card_detected = 0;

	return nm_card_detected;
}

void sdhci_vdd_1v8_voltage(struct mmc_host *mmc, int level_value)
{
	int err;
	int gpio_num = mmc->sd_1v8_enable_gpio;
	err = gpio_request(gpio_num, "gpio_num");
	if (err < 0) {
		pr_err("Can`t request gpio number %d\n", gpio_num);
		return;
	}

	pr_err("%s:%s mmc gpio num: %d, level_value: %d\n",
		mmc_hostname(mmc), __func__, gpio_num, level_value);
	gpio_direction_output(gpio_num, 0);
	gpio_set_value(gpio_num, level_value);
	gpio_free(gpio_num);
}

int nm_card_send_cmd_detect(struct mmc_host *host, int status)
{
	struct mmc_host *mmc_host_temp = host;
	int detect_result;
	/* cd-gpio is low, consider cmd1 failed */
	if (status == 1)
		return status;

	/* enable 1v8 vdd */
	sdhci_vdd_1v8_voltage(mmc_host_temp, SD_1V8_ENABLE);

	mmc_claim_host(mmc_host_temp);
	msleep(SLEEP_MS_TIME_FOR_DETECT_UNSTABLE);
	pr_err("%s:%s enter CMD1-RESPONSE STATUS detect stage after sleep 20 ms\n",
		mmc_hostname(mmc_host_temp), __func__);
	detect_result = mmc_detect_nm_card(mmc_host_temp);

	msleep(SLEEP_MS_TIME_FOR_DETECT_UNSTABLE);

	if (!detect_result)
		pr_err("%s:%s SD is inserted and detected now(CMD1 success)\n",
			mmc_hostname(mmc_host_temp), __func__);
	else
		pr_err("%s: %s SD is inserted and detected now(CMD1 failed)\n",
			mmc_hostname(mmc_host_temp), __func__);

	sdhci_vdd_1v8_voltage(mmc_host_temp, SD_1V8_DISABLE);
	msleep(SLEEP_MS_TIME_FOR_DETECT_UNSTABLE);
	mmc_release_host(mmc_host_temp);
	return detect_result;
}

void nmcard_detect_run(struct mmc_host *mmc)
{
	int level;
	int cd_det;

#ifdef CONFIG_GPIOLIB
	cd_det = gpio_get_value(mmc->cd_gpio);
	pr_err("mmc1:%s: gpio_get_value cd_det=%d, cd_gpio=%d\n",
		__func__, cd_det, mmc->cd_gpio);
#endif
	/* card remove flag init to 0 */
	if (!cd_det)
		g_detect_first_flag = 0;

	if (g_detect_first_flag == 1) {
		pr_err("mmc card detect first only\n");
		return ;
	}
	if (sdhci_msm_vdd_is_enabled(mmc)) {
		pr_err("mmc vdd is enabled,can not detect\n");
		return ;
	}
	mutex_lock(&card_insert_muxlock);
	mmc->card_inserted = (cd_det == 1) ? 1 : 0;
	mmc->nm_card_cmd_detect_result = 1; /* init detect result */
	level = nm_card_send_cmd_detect(mmc, !(cd_det));
	if (level == 0)
		mmc->card_inserted = 1;
	else
		mmc->card_inserted = 0;

	mmc->nm_card_cmd_detect_result = level;
	mutex_unlock(&card_insert_muxlock);
	/* card detect first only flag set to 1 */
	if (cd_det)
		g_detect_first_flag = 1;
}
