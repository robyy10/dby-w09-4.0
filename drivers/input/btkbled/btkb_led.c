// SPDX-License-Identifier: GPL-2.0
/*
 * btkb_led.c
 *
 * btkb_led driver
 *
 * Copyright (c) 2022-2022 Huawei Technologies Co., Ltd.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/input.h>
#include "btkb_led.h"
#include <securec.h>
#ifdef CONFIG_DRM
#include <drm/drm_panel.h>
#endif

struct btkb_led_dev *g_btkb_led_dev;

#ifdef CONFIG_DRM
static struct drm_panel *active_panel;
static int btkb_check_drm_dt(struct device_node *np)
{
	int i;
	int count;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return 0;
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		if (!node)
			continue;
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR_OR_NULL(panel)) {
			active_panel = panel;
			return 0;
		}
	}

	return -EINVAL;
}
#endif

struct btkb_led_dev *input_get_led_dev(void)
{
	return g_btkb_led_dev;
}

static void resume_led_event(struct btkb_led_dev *dev)
{
	struct btkb_led_ops *ops = NULL;

	ops = dev->led_ops;
	if (!ops) {
		pr_err("ops is null, send led resume event fail\n");
		return;
	}

	if (ops->led_toggle)
		ops->led_toggle(dev->dev, true);
}

static void suspend_led_event(struct btkb_led_dev*dev)
{
	struct btkb_led_ops *ops = NULL;

	ops = dev->led_ops;
	if (!ops) {
		pr_err("ops is null, send led suspend event fail\n");
		return;
	}

	if (ops->led_toggle)
		ops->led_toggle(dev->dev, false);
}

#ifdef CONFIG_DRM
static int led_drm_input_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct drm_panel_notifier *evdata = data;
	int *blank = NULL;
	struct btkb_led_dev *led_dev = container_of(self,
		struct btkb_led_dev, led_input_notifier);

	if (!led_dev || !evdata) {
		pr_err("led_dev or evdara is null, btkb init drm notifier callback function fail\n");
		return NOTIFY_DONE;
	}
	if (!evdata->data) {
		pr_err("evdata->dara is null, btkb init drm notifier callback function fail\n");
		return NOTIFY_DONE;
	}
	blank = evdata->data;
	if ((event == DRM_PANEL_EVENT_BLANK) &&
		(*blank == DRM_PANEL_BLANK_UNBLANK))
		resume_led_event(led_dev);
	else if ((event == DRM_PANEL_EVENT_BLANK) &&
		(*blank == DRM_PANEL_BLANK_POWERDOWN))
		suspend_led_event(led_dev);

	return 0;
}
#endif

int btkb_led_register(struct btkb_led_dev *led_dev, struct input_dev *dev)
{
	int ret = 0;

	if (!led_dev) {
		pr_err("led_dev is null, btkb register fail\n");
		return -EINVAL;
	}
	memset_s(&led_dev->led_input_notifier, sizeof(led_dev->led_input_notifier),
		0, sizeof(led_dev->led_input_notifier));
	led_dev->dev = dev;

#ifdef CONFIG_DRM
	if (led_dev->btkb_led_notifier_client == BTKB_LED_NOTIFIER_CLIENT_DRM) {
		led_dev->led_input_notifier.notifier_call = led_drm_input_notifier_callback;
		if (active_panel)
			ret = drm_panel_notifier_register(active_panel, &led_dev->led_input_notifier);
		if (ret)
			pr_err("btkb led register drm fail\n");
	}
#endif

	return ret;
}

int btkb_led_unregister(struct btkb_led_dev *led_dev)
{
	if (!led_dev) {
		pr_err("led_dev id null, btkb led unregister fail\n");
		return -EINVAL;
	}

#ifdef CONFIG_DRM
	if ((led_dev->btkb_led_notifier_client == BTKB_LED_NOTIFIER_CLIENT_DRM) &&
		active_panel)
		drm_panel_notifier_unregister(active_panel, &led_dev->led_input_notifier);
#endif

	return 0;
}

static int btkb_led_parse_dts(struct device_node *np,
	struct btkb_led_dev *led_dev)
{
	int ret;

	ret = of_property_read_u32(np, "btkb_led_notifier",
		&led_dev->btkb_led_notifier);
	if (ret) {
		pr_err("btkb parse parameter btkb_led_notifier fail\n");
		return -EINVAL;
	}
	ret = of_property_read_u32(np, "btkb_led_notifier_client",
		&led_dev->btkb_led_notifier_client);
	if (ret) {
		pr_err("btkb parse parameter  btkb_led_notifier_client fail\n");
		return -EINVAL;
	}

	return 0;
}

static int btkb_led_probe(struct platform_device *pdev)
{
	int ret;
	struct btkb_led_dev *led_dev = NULL;
	struct device_node *np = NULL;

	pr_info("btkb_led_probe enter\n");
	if (!pdev)
		return -ENODEV;
	if (!pdev->dev.of_node)
		return -ENODEV;

	led_dev = kzalloc(sizeof(*led_dev), GFP_KERNEL);
	if (!led_dev)
		return -ENOMEM;

	g_btkb_led_dev = led_dev;
	np = pdev->dev.of_node;

	ret = btkb_led_parse_dts(np, led_dev);
	if (ret) {
		pr_err("btkb_led_parse_dts fail\n");
		goto fail_free_mem;
	}
#ifdef CONFIG_DRM
	ret = btkb_check_drm_dt(np);
	if (ret) {
		pr_err("btkb_check_drm_dt fail\n");
		goto fail_free_mem;
	}
#endif
	pr_info("btkb_led_probe out\n");
	return 0;

fail_free_mem:
	kfree(led_dev);
	led_dev = NULL;
	g_btkb_led_dev = NULL;
	pr_err("btkb_led_probe fail\n");
	return ret;
}

static int btkb_led_remove(struct platform_device *pdev)
{
	struct btkb_led_dev *led_dev = g_btkb_led_dev;

	if (!led_dev)
		return -ENODEV;

	btkb_led_unregister(led_dev);
	platform_set_drvdata(pdev, NULL);
	kfree(led_dev);
	led_dev = NULL;
	g_btkb_led_dev = NULL;

	return 0;
}

static const struct of_device_id btkb_led_match_table[] = {
	{
		.compatible = "huawei,btkb_led",
		.data = NULL,
	},
	{},
};

static struct platform_driver btkb_led_driver = {
	.probe = btkb_led_probe,
	.remove = btkb_led_remove,
	.driver = {
		.name = "huawei,btkb_led",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(btkb_led_match_table),
	},
};

static int __init btkb_led_init(void)
{
	int ret;

	ret =  platform_driver_register(&btkb_led_driver);
	if (ret)
		pr_err("btkb_led_driver register fail\n");
	return ret;
}

static void __exit btkb_led_exit(void)
{
	platform_driver_unregister(&btkb_led_driver);
}

late_initcall(btkb_led_init);
module_exit(btkb_led_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("bluetooth keyboard led control driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");