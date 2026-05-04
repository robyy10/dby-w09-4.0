// SPDX-License-Identifier: GPL-2.0
/*
 * rstm32g031.c
 *
 * rstm32g031 driver
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

#include "rstm32g031.h"
#include "rstm32g031_i2c.h"
#include "rstm32g031_scp.h"
#include "rstm32g031_fw.h"
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <chipset_common/hwpower/common_module/power_event_ne.h>
#include <chipset_common/hwpower/common_module/power_firmware.h>
#include <chipset_common/hwpower/common_module/power_gpio.h>
#include <chipset_common/hwpower/common_module/power_log.h>
#include <chipset_common/hwpower/common_module/power_dts.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/common_module/power_devices_info.h>
#include <chipset_common/hwpower/common_module/power_event_ne.h>

#define HWLOG_TAG rstm32g031
HWLOG_REGIST();

static ssize_t rstm32g031_fw_write(void *dev_data, const char *buf, size_t size)
{
	struct rstm32g031_device_info *di = (struct rstm32g031_device_info *)dev_data;
	struct power_fw_hdr *hdr = NULL;
	int hdr_size;

	if (!di || !buf)
		return -EINVAL;

	hdr = (struct power_fw_hdr *)buf;
	hdr_size = sizeof(struct power_fw_hdr);

	hwlog_info("size=%ld hdr_size=%d bin_size=%ld\n", size, hdr_size, hdr->bin_size);
	hwlog_info("version_id=%x crc_id=%x patch_id=%x config_id=%x\n",
		hdr->version_id, hdr->crc_id, hdr->patch_id, hdr->config_id);

	rstm32g031_fw_update_online_mtp(di, (u8 *)hdr + hdr_size, hdr->bin_size,
		hdr->version_id);
	return size;
}

static int rstm32g031_log_not_empty(struct rstm32g031_device_info *di)
{
	u8 reg_val = 0;

	if (!di)
		return -ENODEV;

	rstm32g031_read_byte(di, RSTM32G031_REG_SCP_LOG_STATUS, &reg_val);
	if ((reg_val & RSTM32G031_REG_SCP_LOG_NOT_EMPTY_MASK) >>
		RSTM32G031_REG_SCP_LOG_NOT_EMPTY_SHIFT)
		return 1;
	return 0;
}

static int rstm32g031_get_scp_log(struct rstm32g031_device_info *di)
{
	int ret;
	u8 log_data[4] = {0}; /* read 4 bytes */

	ret = rstm32g031_write_byte(di, RSTM32G031_REG_SCP_LOG_GET_ONE_DATA,
		RSTM32G031_REG_SCP_LOG_GET_ONE_DATA_VALUE);

	ret += rstm32g031_read_byte(di, RSTM32G031_REG_SCP_LOG_INFO, &log_data[0]);
	ret += rstm32g031_read_byte(di, RSTM32G031_REG_SCP_LOG_ADDR, &log_data[1]);
	ret += rstm32g031_read_byte(di, RSTM32G031_REG_SCP_LOG_DATA, &log_data[2]);
	ret += rstm32g031_read_byte(di, RSTM32G031_REG_SCP_LOG_OLD_DATA, &log_data[3]);
	if ((log_data[0] & RSTM32G031_REG_SCP_LOG_INFO_ERROR_MASK) >>
		RSTM32G031_REG_SCP_LOG_INFO_ERROR_SHIFT)
		hwlog_err("scp_log_info report error!\n");

	hwlog_info("scp log: [0x%x]=0x%x, [0x%x]=0x%x, [0x%x]=0x%x, [0x%x]=0x%x\n",
		RSTM32G031_REG_SCP_LOG_INFO, log_data[0],
		RSTM32G031_REG_SCP_LOG_ADDR, log_data[1],
		RSTM32G031_REG_SCP_LOG_DATA, log_data[2],
		RSTM32G031_REG_SCP_LOG_OLD_DATA, log_data[3]);
	return ret;
}

static int rstm32g031_handle_log_fifo(struct rstm32g031_device_info *di)
{
	if (!di)
		return -ENODEV;

	while (rstm32g031_log_not_empty(di))
		rstm32g031_get_scp_log(di);

	return 0;
}

static void rstm32g031_interrupt_work(struct work_struct *work)
{
	struct rstm32g031_device_info *di = NULL;
	int ret;
	u8 reg_int[2] = {0};

	di = container_of(work, struct rstm32g031_device_info, irq_work);
	if (!di || !di->client) {
		hwlog_err("di is null\n");
		return;
	}

	ret = rstm32g031_read_byte(di, RSTM32G031_ISR_EVT_REG, &reg_int[0]);
	ret += rstm32g031_read_byte(di, RSTM32G031_ISR_ERR_REG, &reg_int[1]);
	if (ret) {
		hwlog_err("read int reg err, ret:%d\n", ret);
		return;
	}

	if ((reg_int[0] & RSTM32G031_ISR_EVT_REG_EVT_NEW_LOG_MASK) >>
		RSTM32G031_ISR_EVT_REG_EVT_NEW_LOG_SHIFT)
		rstm32g031_handle_log_fifo(di);
	if ((reg_int[0] & RSTM32G031_ISR_EVT_REG_EVT_MSTR_RST_MASK) >>
		RSTM32G031_ISR_EVT_REG_EVT_MSTR_RST_SHIFT) {
		hwlog_err("slave request reset scp\n");
		power_event_bnc_notify(POWER_BNT_REVERSE_CHG,
			POWER_NE_RVS_CHG_RESET_PROTOCOL, NULL);
	}
	if ((reg_int[1] & RSTM32G031_ISR_ERR_EVT_SCP_ERR_MAX_COUNT_MASK) >>
		RSTM32G031_ISR_ERR_EVT_SCP_ERR_MAX_COUNT_SHIFT) {
		hwlog_err("slave scp fail\n");
		power_event_bnc_notify(POWER_BNT_REVERSE_CHG,
			POWER_NE_RVS_CHG_PROTOCOL_FAIL, NULL);
	}

	/* clear irq */
	rstm32g031_write_byte(di, RSTM32G031_ISR_EVT_REG, reg_int[0]);
	rstm32g031_write_byte(di, RSTM32G031_ISR_ERR_REG, reg_int[1]);
	enable_irq(di->irq_int);
}

static irqreturn_t rstm32g031_interrupt(int irq, void *_di)
{
	struct rstm32g031_device_info *di = _di;

	if (!di)
		return IRQ_HANDLED;

	hwlog_info("int happened\n");

	disable_irq_nosync(di->irq_int);
	schedule_work(&di->irq_work);

	return IRQ_HANDLED;
}

static int rstm32g031_irq_init(struct rstm32g031_device_info *di,
	struct device_node *np)
{
	int ret;

	if (di->param_dts.int_gpio_type == RSTM32G031_FW_INT_GPIO_FOR_WAKEUP)
		return power_gpio_config_output(np, "gpio_int", "rstm32g031_gpio_int",
			&di->gpio_int, 1);

	INIT_WORK(&di->irq_work, rstm32g031_interrupt_work);
	ret = power_gpio_config_interrupt(np,
		"gpio_int", "rstm32g031_gpio_int", &di->gpio_int, &di->irq_int);
	if (ret)
		return ret;

	ret = request_irq(di->irq_int, rstm32g031_interrupt,
		IRQF_TRIGGER_FALLING, "rstm32g031_int_irq", di);
	if (ret) {
		hwlog_err("gpio irq request fail\n");
		di->irq_int = -1;
		gpio_free(di->gpio_int);
		return ret;
	}

	enable_irq_wake(di->irq_int);
	return 0;
}

/* print the register head in charging process */
static int rstm32g031_register_head(char *buffer, int size, void *dev_data)
{
	char tmp_buff[BUF_LEN] = {0};
	int i;

	if (!buffer)
		return -ENODEV;

	snprintf(tmp_buff, BUF_LEN, "chip      ");
	strncat(buffer, tmp_buff, strlen(tmp_buff));
	for (i = 0; i < RSTM32G031_REG_NUM; i++) {
		snprintf(tmp_buff, BUF_LEN, "R[0x%02x]  ", i + RSTM32G031_REG_BASE);
		strncat(buffer, tmp_buff, strlen(tmp_buff));
	}

	return 0;
}

/* print the register value in charging process */
static int rstm32g031_dump_reg(char *buffer, int size, void *dev_data)
{
	struct rstm32g031_device_info *di = dev_data;
	u8 reg[RSTM32G031_REG_NUM] = {0};
	char tmp_buff[BUF_LEN] = {0};
	int i;
	int ret;

	if (!buffer || !di || di->in_sleep)
		return -ENODEV;

	snprintf(tmp_buff, BUF_LEN, "%-8s", di->name);
	strncat(buffer, tmp_buff, strlen(tmp_buff));
	for (i = 0; i < RSTM32G031_REG_NUM; i++) {
		ret = rstm32g031_read_byte(di, i + RSTM32G031_REG_BASE, &reg[i]);
		if (ret)
			hwlog_err("dump_register read fail\n");

		snprintf(tmp_buff, BUF_LEN, "0x%-7.2x", reg[i]);
		strncat(buffer, tmp_buff, strlen(tmp_buff));
	}

	return 0;
}

static struct power_log_ops g_rstm32g031_log_ops = {
	.dev_name = "rstm32g031",
	.dump_log_head = rstm32g031_register_head,
	.dump_log_content = rstm32g031_dump_reg,
};

static void rstm32g031_ops_register(struct rstm32g031_device_info *di)
{
	snprintf(di->name, CHIP_DEV_NAME_LEN, "rstm32g031");
	g_rstm32g031_log_ops.dev_data = di;
	power_log_ops_register(&g_rstm32g031_log_ops);
	rstm32g031_hwscp_register(di);
	power_fw_ops_register("rstm32g031", (void *)di,
		NULL, (power_fw_write)rstm32g031_fw_write);
}

static int rstm32g031_parse_dts(struct device_node *np,
	struct rstm32g031_device_info *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "wait_time",
		(u32 *)&(di->param_dts.wait_time), 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "hw_config",
		(u32 *)&(di->param_dts.hw_config), 0x20);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "int_gpio_type",
		(u32 *)&(di->param_dts.int_gpio_type), 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "max_design_ibus",
		(u32 *)&(di->param_dts.max_design_ibus), RSTM32G031_MAX_IBUS);

	if (power_gpio_config_output(np, "gpio_reset", "rstm32g031_gpio_reset",
		&di->gpio_reset, 1))
		goto reset_config_fail;
	if (power_gpio_config_output(np, "gpio_enable", "rstm32g031_gpio_enable",
		&di->gpio_enable, 0))
		goto int_config_fail;

	return 0;

int_config_fail:
	gpio_free(di->gpio_reset);
reset_config_fail:
	return -EINVAL;
}

static int rstm32g031_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct rstm32g031_device_info *di = NULL;
	struct device_node *np = NULL;

	if (!client || !client->dev.of_node || !id)
		return -ENODEV;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &client->dev;
	np = di->dev->of_node;
	di->client = client;
	i2c_set_clientdata(client, di);
	di->chip_already_init = 1;

	if (rstm32g031_parse_dts(np, di))
		goto fail_free_mem;

	power_msleep(di->param_dts.wait_time, 0, NULL);
	/* need update mtp when mtp is latest */
	if (rstm32g031_fw_update_mtp_check(di))
		goto fail_free_gpio;

	if (rstm32g031_fw_get_ver_id(di))
		goto fail_free_gpio;

	if (rstm32g031_irq_init(di, np))
		goto fail_free_gpio;

	if (rstm32g031_pre_init_check(di))
		goto fail_free_gpio;

	rstm32g031_ops_register(di);
	rstm32g031_enable_sleep(di, 1);
	hwlog_info("%s success\n", __func__);
	return 0;

fail_free_gpio:
	gpio_free(di->gpio_enable);
	gpio_free(di->gpio_reset);
fail_free_mem:
	devm_kfree(&client->dev, di);
	return -EPERM;
}

static int rstm32g031_remove(struct i2c_client *client)
{
	struct rstm32g031_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return -ENODEV;

	devm_kfree(&client->dev, di);
	return 0;
}

static void rstm32g031_shutdown(struct i2c_client *client)
{
}

MODULE_DEVICE_TABLE(i2c, rstm32g031);
static const struct of_device_id rstm32g031_of_match[] = {
	{
		.compatible = "rstm32g031",
		.data = NULL,
	},
	{},
};

static const struct i2c_device_id rstm32g031_i2c_id[] = {
	{ "rstm32g031", 0 }, {}
};

static struct i2c_driver rstm32g031_driver = {
	.probe = rstm32g031_probe,
	.remove = rstm32g031_remove,
	.shutdown = rstm32g031_shutdown,
	.id_table = rstm32g031_i2c_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "rstm32g031",
		.of_match_table = of_match_ptr(rstm32g031_of_match),
	},
};

static __init int rstm32g031_init(void)
{
	return i2c_add_driver(&rstm32g031_driver);
}

static __exit void rstm32g031_exit(void)
{
	i2c_del_driver(&rstm32g031_driver);
}

module_init(rstm32g031_init);
module_exit(rstm32g031_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("rstm32g031 module driver");
MODULE_AUTHOR("huawei Technologies Co., Ltd.");
