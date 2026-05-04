// SPDX-License-Identifier: GPL-2.0
/*
 * sc8933.c
 *
 * sc8933 boost driver
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

#include "sc8933.h"
#include <chipset_common/hwpower/common_module/power_dts.h>
#include <chipset_common/hwpower/common_module/power_pinctrl.h>
#include <chipset_common/hwpower/common_module/power_gpio.h>
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/common_module/power_i2c.h>

#define HWLOG_TAG sc8933
HWLOG_REGIST();

int sc8933_write_byte(struct sc8933_device_info *di, u8 reg, u8 value)
{
	int ret;

	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	ret = power_i2c_u8_write_byte(di->client, reg, value);

	return ret;
}

int sc8933_read_byte(struct sc8933_device_info *di, u8 reg, u8 *value)
{
	int ret;

	if (!di || (di->chip_already_init == 0)) {
		hwlog_err("chip not init\n");
		return -ENODEV;
	}

	ret = power_i2c_u8_read_byte(di->client, reg, value);

	return ret;
}

int sc8933_write_mask(struct sc8933_device_info *di, u8 reg, u8 mask, u8 shift, u8 value)
{
	int ret;
	u8 val = 0;

	ret = sc8933_read_byte(di, reg, &val);
	if (ret < 0)
		return ret;

	val &= ~mask;
	val |= ((value << shift) & mask);

	return sc8933_write_byte(di, reg, val);
}

static int sc8933_set_vcg_on(void *dev_data, int enable)
{
	struct sc8933_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	sc8933_write_mask(di, SC8933_PWR_PATH_REG, SC8933_VCG_ON_MASK,
		SC8933_VCG_ON_SHIFT, enable);

	hwlog_info("vcg on:%d\n", enable);
	return 0;
}

static int sc8933_set_idle_mode(void *dev_data, int mode)
{
	struct sc8933_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	sc8933_write_mask(di, SC8933_PWR_SET_REG, SC8933_IDLE_MASK,
		SC8933_IDLE_SHIFT, mode);

	hwlog_info("set idle mode:%d\n", mode);
	return 0;
}

int sc8933_set_vbus_mv(void *dev_data, int vbus)
{
	int ret;
	s16 data = 0;
	u8 flag[2] = { 0 }; /* 2: two regs */
	struct sc8933_device_info *di = dev_data;

	if (!di || !vbus)
		return -EINVAL;

	vbus += di->vbus_comp;
	data = (vbus - SC8933_VBUS_BASE) / SC8933_VBUS_LSB;
	if (data < 0)
		return -EINVAL;
	ret = sc8933_write_byte(di, SC8933_VBUS_SET_MSB_REG,
		(data >> SC8933_VBUS_LOW_BITS) & SC8933_VBUS_SET_MSB_MASK);
	ret += sc8933_write_mask(di, SC8933_VBUS_SET_LSB_REG,
		SC8933_VBUS_SET_LSB_MASK,
		SC8933_VBUS_SET_LSB_SHIFT, data);
	ret += sc8933_write_mask(di, SC8933_LOAD_REG,
		SC8933_VBUS_SET_LOAD_MASK,
		SC8933_VBUS_SET_LOAD_SHIFT,
		SC8933_VBUS_SET_LOAD_ENABLE);
	if (ret)
		return -ENODEV;

	(void)sc8933_read_byte(di, SC8933_VBUS_SET_MSB_REG, &flag[0]);
	(void)sc8933_read_byte(di, SC8933_VBUS_SET_LSB_REG, &flag[1]);
	hwlog_info("%s: reg[0x%x]=0x%x reg[0x%x]=0x%x\n", __func__,
		SC8933_VBUS_SET_MSB_REG, flag[0], SC8933_VBUS_SET_LSB_REG, flag[1]);

	return 0;
}

static int sc8933_set_ibus_ma(void *dev_data, int ibus)
{
	int ret;
	u8 flag = 0;
	struct sc8933_device_info *di = dev_data;

	if (!di || !ibus)
		return -EINVAL;
	ibus = ibus * di->sense_r_actual / di->sense_r_typical;
	ret = sc8933_write_byte(di, SC8933_IBUS_LIM_REG,
		(ibus / SC8933_IBUS_LIM_BASE));
	ret += sc8933_write_mask(di, SC8933_LOAD_REG,
		SC8933_IBUS_LIM_LOAD_MASK,
		SC8933_IBUS_LIM_LOAD_SHIFT,
		SC8933_IBUS_LIM_LOAD_ENABLE);
	if (ret)
		return -ENODEV;

	(void)sc8933_read_byte(di, SC8933_IBUS_LIM_REG, &flag);
	hwlog_info("%s: reg[0x%x]=0x%x \n", __func__, SC8933_IBUS_LIM_REG, flag);

	return 0;
}

static int sc8933_ic_enable(void *dev_data, int enable)
{
	int ret;
	struct sc8933_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8933_write_mask(di, SC8933_MODE_REG, SC8933_DIR_MASK,
		SC8933_DIR_SHIFT, enable);
	ret += sc8933_write_mask(di, SC8933_MODE_REG, SC8933_C_DIR_MASK,
		SC8933_C_DIR_SHIFT, enable);

	hwlog_info("ic enable:%d\n", enable);
	return ret;
}

static int sc8933_reg_reset(void *dev_data)
{
	int ret;
	struct sc8933_device_info *di = dev_data;

	if (!di)
		return -ENODEV;

	ret = sc8933_write_mask(di, SC8933_CTRL_REG,
		SC8933_RESET_MASK, SC8933_RESET_SHIFT,
		SC8933_RESET_ENABLE);
	/* i2c return fail, actually execute success */
	if (ret)
		hwlog_info("%s fail\n", __func__);

	hwlog_info("%s done\n", __func__);
	return 0;
}

static int sc8933_reg_init(void *dev_data)
{
	struct sc8933_device_info *di = dev_data;

	if (!di)
		return -EINVAL;

	return sc8933_set_idle_mode(di, SC8933_IDLE_STATE);
}

/* print the register head in charging process */
static int sc8933_register_head(char *buffer, int size, void *dev_data)
{
	char tmp_buff[BUF_LEN] = {0};
	int i;

	if (!buffer)
		return -ENODEV;

	snprintf(tmp_buff, BUF_LEN, "chip      ");
	strncat(buffer, tmp_buff, strlen(tmp_buff));
	for (i = 0; i < SC8933_REG_NUM; i++) {
		snprintf(tmp_buff, BUF_LEN, "R[0x%02x]  ", i + 1);
		strncat(buffer, tmp_buff, strlen(tmp_buff));
	}

	return 0;
}

/* print the register value in charging process */
static int sc8933_dump_reg(char *buffer, int size, void *dev_data)
{
	struct sc8933_device_info *di = dev_data;
	u8 reg[SC8933_REG_NUM] = {0};
	char tmp_buff[BUF_LEN] = {0};
	int i;
	int ret;

	if (!buffer || !di)
		return -ENODEV;

	snprintf(tmp_buff, BUF_LEN, "%-10s", di->name);
	strncat(buffer, tmp_buff, strlen(tmp_buff));
	for (i = 0; i < SC8933_REG_NUM; i++) {
		ret = sc8933_read_byte(di, i + 1, &reg[i]);
		if (ret)
			hwlog_err("dump_register read fail\n");

		snprintf(tmp_buff, BUF_LEN, "0x%-7.2x", reg[i]);
		strncat(buffer, tmp_buff, strlen(tmp_buff));
	}

	return 0;
}

static struct boost_ops g_sc8933_boost_ops = {
	.chip_name = "sc8933",
	.set_vcg_on = sc8933_set_vcg_on,
	.set_vbus = sc8933_set_vbus_mv,
	.set_ibus = sc8933_set_ibus_ma,
	.ic_enable = sc8933_ic_enable,
	.set_idle_mode = sc8933_set_idle_mode,
};

static struct power_log_ops g_sc8933_log_ops = {
	.dev_name = "sc8933",
	.dump_log_head = sc8933_register_head,
	.dump_log_content = sc8933_dump_reg,
};

static void sc8933_init_ops_dev_data(struct sc8933_device_info *di)
{
	memcpy(&di->bst_ops, &g_sc8933_boost_ops, sizeof(struct boost_ops));
	di->bst_ops.dev_data = (void *)di;
	memcpy(&di->log_ops, &g_sc8933_log_ops, sizeof(struct power_log_ops));
	di->log_ops.dev_data = (void *)di;
	snprintf(di->name, CHIP_DEV_NAME_LEN, "sc8933");
}

static void sc8933_ops_register(struct sc8933_device_info *di)
{
	int ret;

	sc8933_init_ops_dev_data(di);

	ret += boost_ops_register(&di->bst_ops);
	ret += power_log_ops_register(&di->log_ops);
	if (ret)
		hwlog_err("sc8933 ops register fail\n");
}

static void sc8933_interrupt_work(struct work_struct *work)
{
	u8 flag[4] = { 0 }; /* 4: four regs */
	struct sc8933_device_info *di = NULL;

	if (!work)
		return;

	di = container_of(work, struct sc8933_device_info, irq_work);
	if (!di || !di->client) {
		hwlog_err("di is null\n");
		return;
	}

	(void)sc8933_read_byte(di, SC8933_LOOP_STA_REG, &flag[0]);
	(void)sc8933_read_byte(di, SC8933_STA1_REG, &flag[1]);
	(void)sc8933_read_byte(di, SC8933_STA2_REG, &flag[2]);
	(void)sc8933_read_byte(di, SC8933_INT_REG, &flag[3]);
	hwlog_info("reg[0x%x]=0x%x reg[0x%x]=0x%x reg[0x%x]=0x%x reg[0x%x]=0x%x\n",
		SC8933_LOOP_STA_REG, flag[0], SC8933_STA1_REG, flag[1],
		SC8933_STA2_REG, flag[2], SC8933_INT_REG, flag[3]);

	enable_irq(di->irq_int);
}

static irqreturn_t sc8933_interrupt(int irq, void *_di)
{
	struct sc8933_device_info *di = _di;

	if (!di)
		return IRQ_HANDLED;

	disable_irq_nosync(di->irq_int);
	queue_work(di->int_wq, &di->irq_work);

	return IRQ_HANDLED;
}

static int sc8933_irq_init(struct sc8933_device_info *di,
	struct device_node *np)
{
	int ret;

	INIT_WORK(&di->irq_work, sc8933_interrupt_work);

	ret = power_gpio_config_interrupt(np,
		"intr_gpio", "sc8933_gpio_int", &di->gpio_int, &di->irq_int);
	if (ret)
		return ret;

	ret = request_irq(di->irq_int, sc8933_interrupt,
		IRQF_TRIGGER_FALLING, "sc8933_int_irq", di);
	if (ret) {
		hwlog_err("gpio irq request fail\n");
		di->irq_int = -1;
		gpio_free(di->gpio_int);
		return ret;
	}

	enable_irq_wake(di->irq_int);
	di->int_wq = create_singlethread_workqueue("sc8933_int_wq");
	return 0;
}

static void sc8933_parse_dts(struct device_node *np,
	struct sc8933_device_info *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "vbus_comp",
		&di->vbus_comp, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "sense_r_typical",
		&di->sense_r_typical, SC8933_SENSE_R_TYPICAL);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "sense_r_actual",
		&di->sense_r_actual, SC8933_SENSE_R_ACTUAL);
}

static int sc8933_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret;
	struct sc8933_device_info *di = NULL;
	struct device_node *np = NULL;

	if (!client || !client->dev.of_node || !id)
		return -ENODEV;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &client->dev;
	np = di->dev->of_node;
	di->client = client;
	di->chip_already_init = 1;

	sc8933_parse_dts(np, di);
	ret = sc8933_reg_reset(di);
	if (ret)
		goto sc8933_fail_0;

	ret = sc8933_reg_init(di);
	if (ret)
		goto sc8933_fail_0;

	ret = sc8933_irq_init(di, np);
	if (ret)
		goto sc8933_fail_0;

	sc8933_ops_register(di);
	i2c_set_clientdata(client, di);
	hwlog_info("%s success\n", __func__);
	return 0;

sc8933_fail_0:
	hwlog_err("sc8933_fail_0\n");
	di->chip_already_init = 0;
	devm_kfree(&client->dev, di);

	return ret;
}

static int sc8933_remove(struct i2c_client *client)
{
	struct sc8933_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return -ENODEV;

	if (di->irq_int)
		free_irq(di->irq_int, di);
	if (di->gpio_int)
		gpio_free(di->gpio_int);

	return 0;
}

static void sc8933_shutdown(struct i2c_client *client)
{
	struct sc8933_device_info *di = i2c_get_clientdata(client);

	if (!di)
		return;

	if (di->irq_int)
		free_irq(di->irq_int, di);
	if (di->gpio_int)
		gpio_free(di->gpio_int);

	sc8933_set_vcg_on(di, 0);
	sc8933_ic_enable(di, 0);
	sc8933_set_idle_mode(di, SC8933_IDLE_STATE);
}

#ifdef CONFIG_PM
static int sc8933_i2c_suspend(struct device *dev)
{
	return 0;
}

static int sc8933_i2c_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops sc8933_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sc8933_i2c_suspend, sc8933_i2c_resume)
};
#define SC8933_PM_OPS (&sc8933_pm_ops)
#else
#define SC8933_PM_OPS (NULL)
#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(i2c, sc8933);
static const struct of_device_id sc8933_of_match[] = {
	{
		.compatible = "sc8933",
		.data = NULL,
	},
	{},
};

static const struct i2c_device_id sc8933_i2c_id[] = {
	{ "sc8933", 0 }, {}
};

static struct i2c_driver sc8933_driver = {
	.probe = sc8933_probe,
	.remove = sc8933_remove,
	.shutdown = sc8933_shutdown,
	.id_table = sc8933_i2c_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sc8933",
		.of_match_table = of_match_ptr(sc8933_of_match),
		.pm = SC8933_PM_OPS,
	},
};

static int __init sc8933_init(void)
{
	return i2c_add_driver(&sc8933_driver);
}

static void __exit sc8933_exit(void)
{
	i2c_del_driver(&sc8933_driver);
}

module_init(sc8933_init);
module_exit(sc8933_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("sc8933 module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
