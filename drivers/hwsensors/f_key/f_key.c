/*
 * Copyright (C) 2022 HUAWEI, Inc.
 * File Name: kernel/drivers/hwsensors/f_key.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "f_key.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/input.h>
#include <securec.h>

static int f_key_parse_dt(struct device *dev, struct f_key_platform_data *pdata)
{
	int ret = 0;
	errno_t err = EOK;
	const char *name = NULL;

	ret = of_property_read_string(dev->of_node, "fkey,name", &name);
	if (ret) {
		pr_err("%s: parse dt name failed\n", __func__);
		return ret;
	} else {
		err = strncpy_s(pdata->name, MAX_NAME_LEN, name, sizeof(pdata->name));
		if (err != EOK) {
			pr_err("%s: strncpy failed\n", __func__);
			return ret;
		}
	}
	ret = of_property_read_u32(dev->of_node, "fkey,input-code", &pdata->input_code);
	if (ret) {
		pr_err("%s: parse input-code failed\n", __func__);
		return ret;
	}
	pdata->gpio = of_get_named_gpio(dev->of_node, "fkey,gpio", 0);
	if (!gpio_is_valid(pdata->gpio)) {
		pr_err("%s: gpio is invalid\n", __func__);
		return -EINVAL;
	}
	pr_info("%s: input-code is %d\n", __func__, pdata->input_code);
	pr_info("%s: gpio is %d\n", __func__, pdata->gpio);

	return ret;
}

irqreturn_t f_key_isr(int irq, void *dev)
{
	int val;
	struct f_key_platform_data *pdata = (struct f_key_platform_data *)dev;

	val = gpio_get_value(pdata->gpio);
	pdata->val = val;
	pr_info("f_key_irq gpio value = %d\n", val);

	schedule_work(&pdata->event_work);
	return IRQ_HANDLED;
}

static void report_event_work(struct work_struct *work)
{
	struct f_key_platform_data *pdata = container_of(work, struct f_key_platform_data, event_work);
	int report_val = PUSH_DOWN;

	if (pdata == NULL) {
		pr_err("%s:Null pointer\n", __func__);
		return;
	}
	if (pdata->val == GPIO_VAL_HIGH)
		report_val = PUSH_UP;

	input_report_key(pdata->input_dev, pdata->input_code, report_val);
	input_sync(pdata->input_dev);
	pr_info("f_key_irq report val = %d\n", report_val);
}

static int input_device_init(struct platform_device *pdev, struct f_key_platform_data *pdata)
{
	int ret = 0;
	struct input_dev *input = NULL;

	if (pdata == NULL || pdev == NULL)
		return -EINVAL;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		pr_err("%s: input allocate failed\n", __func__);
		return -EINVAL;
	}

	input->name = pdata->name;
	input->evbit[0] = BIT_MASK(EV_KEY);
	input_set_capability(input, EV_KEY, pdata->input_code);
	ret = input_register_device(input);
	if (ret < 0) {
		pr_err("%s:input register device failed\n", __func__);
		return ret;
	}
	pdata->input_dev = input;
	return ret;
}

static int f_key_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct f_key_platform_data *pdata = NULL;

	pr_info("f_key probe");
	if (pdev == NULL) {
		pr_err("%s: pdev is NULL!\n", __func__);
		return -EINVAL;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -EINVAL;

	ret = f_key_parse_dt(&pdev->dev, pdata);
	if (ret) {
		pr_err("%s: parse dt failed\n", __func__);
		return -EINVAL;
	}
	mutex_init(&pdata->lock);
	INIT_WORK(&pdata->event_work, report_event_work);

	ret = input_device_init(pdev, pdata);
	if (ret) {
		pr_err("%s: failed to init input device\n", __func__);
		return ret;
	}

	dev_set_drvdata(&pdev->dev, pdata);

	ret = devm_gpio_request(&pdev->dev, pdata->gpio, pdata->name);
	if (ret) {
		pr_err("%s: unable to request gpio\n", __func__);
		return ret;
	}
	pdata->irq = gpio_to_irq(pdata->gpio);
	ret = devm_request_irq(&pdev->dev, pdata->irq, f_key_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, pdata->name,
		pdata);
	if (ret) {
		pr_err("%s: unable to request irq\n", __func__);
		return ret;
	}
	ret = enable_irq_wake(pdata->irq);
	if (ret) {
		pr_err("%s: enable irq failed\n", __func__);
		return ret;
	}

	pr_info("%s: f key probe success\n", __func__);
	return ret;
}

static int f_key_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct f_key_platform_data *pdata = dev_get_drvdata(&pdev->dev);

	if (pdata == NULL) {
		pr_err("%s:remove pdata is null\n", __func__);
		return -EINVAL;
	}
	mutex_destroy(&pdata->lock);
	input_unregister_device(pdata->input_dev);
	return ret;
}

static struct of_device_id hw_f_key_match_table[] = {
	{
		.compatible = "huawei,f_key",
	},
	{ },
};

static struct platform_driver f_key_drv = {
	.probe = f_key_probe,
	.remove = f_key_remove,
	.driver = {
		.name = "huawei_f_key",
		.owner = THIS_MODULE,
		.of_match_table = hw_f_key_match_table,
	},
};

static int __init f_key_init(void)
{
	int err = 0;
	pr_info("f_key init");
	err = platform_driver_register(&f_key_drv);
	if (err) {
		pr_err("f_key_drv register error %d\n", err);
	}
	return err;
}

static void __exit f_key_exit(void)
{
	platform_driver_unregister(&f_key_drv);
}

MODULE_AUTHOR("huawei");
MODULE_DESCRIPTION("f key driver");
MODULE_LICENSE("GPL");

module_init(f_key_init);
module_exit(f_key_exit);