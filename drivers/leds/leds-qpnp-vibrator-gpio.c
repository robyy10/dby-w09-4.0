// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved. */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>

/*
 * Define vibration periods: default(5sec), min(50ms), max(15sec) and
 * overdrive(30ms).
 */
#define QPNP_VIB_MIN_PLAY_MS		50
#define QPNP_VIB_PLAY_MS		5000
#define QPNP_VIB_MAX_PLAY_MS		15000
#define QPNP_VIB_OVERDRIVE_PLAY_MS	30
#define HISI_VIBRATOR_TIMEOUT_MIN 30

struct vib_ldo_chip {
	struct led_classdev	cdev;
	struct mutex		lock;
	struct hrtimer		stop_timer;
	struct work_struct	vib_work;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;
	struct pinctrl_state *pin_active;

	int			state;
	u64			vib_play_ms;
	bool		vib_enabled;
};

static void qpnp_vib_work(struct work_struct *work)
{
	struct vib_ldo_chip *chip = container_of(work, struct vib_ldo_chip,
						vib_work);

	if (chip->state) {
		pinctrl_select_state(chip->pinctrl, chip->pin_active);

		hrtimer_start(&chip->stop_timer,
			      ms_to_ktime(chip->vib_play_ms),
			      HRTIMER_MODE_REL);
	} else {
		pinctrl_select_state(chip->pinctrl, chip->pin_default);
	}
}

static enum hrtimer_restart vib_stop_timer(struct hrtimer *timer)
{
	struct vib_ldo_chip *chip = container_of(timer, struct vib_ldo_chip,
					     stop_timer);

	chip->state = 0;
	schedule_work(&chip->vib_work);
	return HRTIMER_NORESTART;
}

static ssize_t qpnp_vib_show_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", chip->vib_enabled);
}

static ssize_t qpnp_vib_store_state(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	/* At present, nothing to do with setting state */
	return count;
}

static ssize_t qpnp_vib_show_duration(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);
	ktime_t time_rem;
	s64 time_ms = 0;
	if (hrtimer_active(&chip->stop_timer)) {
		time_rem = hrtimer_get_remaining(&chip->stop_timer);
		time_ms = ktime_to_ms(time_rem);
	}

	return snprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t qpnp_vib_store_duration(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);

	u32 val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret < 0)
		return ret;

	/* setting 0 on duration is NOP for now */
	if (val <= 0)
		return count;

	if (val < QPNP_VIB_MIN_PLAY_MS)
		val = QPNP_VIB_MIN_PLAY_MS;

	if (val > QPNP_VIB_MAX_PLAY_MS)
		val = QPNP_VIB_MAX_PLAY_MS;

	mutex_lock(&chip->lock);
	chip->vib_play_ms = val;
	mutex_unlock(&chip->lock);

	return count;
}

static ssize_t qpnp_vib_show_activate(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* For now nothing to show */
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t qpnp_vib_store_activate(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,
						cdev);
	u32 val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val != 0 && val != 1)
		return count;

	mutex_lock(&chip->lock);
	hrtimer_cancel(&chip->stop_timer);
	chip->state = val;
	pr_debug("state = %d, time = %llums\n", chip->state, chip->vib_play_ms);
	pr_err("state = %d, time = %llums\n", chip->state, chip->vib_play_ms);
	mutex_unlock(&chip->lock);
	schedule_work(&chip->vib_work);

	return count;
}

static struct device_attribute qpnp_vib_attrs[] = {
	__ATTR(state, 0664, qpnp_vib_show_state, qpnp_vib_store_state),
	__ATTR(duration, 0664, qpnp_vib_show_duration, qpnp_vib_store_duration),
	__ATTR(activate, 0664, qpnp_vib_show_activate, qpnp_vib_store_activate),
};

/* Dummy functions for brightness */
static enum led_brightness qpnp_vib_brightness_get(struct led_classdev *cdev)
{

	return 0;
}

static void qpnp_vib_brightness_set(struct led_classdev *cdev,
			enum led_brightness level)
{
	struct vib_ldo_chip *chip = container_of(cdev, struct vib_ldo_chip,	cdev);

	if (chip == NULL) {
		pr_err("%s: vibrator data is NULL", __func__);
		return;
	}
	/* no blocking, the lock should not be used */
	if (level) {
		pinctrl_select_state(chip->pinctrl, chip->pin_active);
		chip->vib_enabled = 1;
	} else {
		pinctrl_select_state(chip->pinctrl, chip->pin_default);
		chip->vib_enabled = 0;
	}
}

static int pinctrl_init(struct device *dev, struct vib_ldo_chip *chip)
{
	int ret;

	if (dev == NULL) {
		pr_err("dev is NULL\n");
		return -1;
	}
	chip->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		pr_err("vibrator error:devm_pinctrl_get wrong\n");
		return -1;
	}
	chip->pin_default = pinctrl_lookup_state(chip->pinctrl, "default");
	if (IS_ERR_OR_NULL(chip->pin_default)) {
		pr_err("vibrator default error:pinctrl_lookup_state wrong\n");
		return -1;
	}
	chip->pin_active = pinctrl_lookup_state(chip->pinctrl, "active");
	if (IS_ERR_OR_NULL(chip->pin_active)) {
		pr_err("vibrator active error:pinctrl_lookup_state wrong\n");
		return -1;
	}
	ret = pinctrl_select_state(chip->pinctrl, chip->pin_default);
	if (ret)
		pr_err("vibrator error:pinctrl_select_state wrong\n");
	return ret;
}

static int qpnp_vibrator_ldo_probe(struct platform_device *pdev)
{
	//struct device_node *of_node = pdev->dev.of_node;
	struct vib_ldo_chip *chip;
	int i, ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	ret = pinctrl_init(&pdev->dev, chip);
	if (ret) {
		pr_err("pinctrl init error %d\n", ret);
		return ret;
    }

	chip->vib_play_ms = QPNP_VIB_PLAY_MS;
	mutex_init(&chip->lock);
	INIT_WORK(&chip->vib_work, qpnp_vib_work);

	hrtimer_init(&chip->stop_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	chip->stop_timer.function = vib_stop_timer;
	dev_set_drvdata(&pdev->dev, chip);

	chip->cdev.name = "vibrator";
	chip->cdev.brightness_get = qpnp_vib_brightness_get;
	chip->cdev.brightness_set = qpnp_vib_brightness_set;
	chip->cdev.max_brightness = 100;
	ret = devm_led_classdev_register(&pdev->dev, &chip->cdev);
	if (ret < 0) {
		pr_err("Error in registering led class device, ret=%d\n", ret);
		goto fail;
	}

	for (i = 0; i < ARRAY_SIZE(qpnp_vib_attrs); i++) {
		ret = sysfs_create_file(&chip->cdev.dev->kobj,
				&qpnp_vib_attrs[i].attr);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error in creating sysfs file, ret=%d\n",
				ret);
			goto sysfs_fail;
		}
	}

	pr_info("Vibrator LDO successfully registered\n");

	return 0;

sysfs_fail:
	for (--i; i >= 0; i--)
		sysfs_remove_file(&chip->cdev.dev->kobj,
				&qpnp_vib_attrs[i].attr);
fail:
	mutex_destroy(&chip->lock);
	dev_set_drvdata(&pdev->dev, NULL);
	return ret;
}

static int qpnp_vibrator_ldo_remove(struct platform_device *pdev)
{
	struct vib_ldo_chip *chip = dev_get_drvdata(&pdev->dev);

	hrtimer_cancel(&chip->stop_timer);
	cancel_work_sync(&chip->vib_work);
	mutex_destroy(&chip->lock);
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

static const struct of_device_id vibrator_ldo_match_table[] = {
	{ .compatible = "qcom,qpnp-vibrator-gpio" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, vibrator_ldo_match_table);

static struct platform_driver qpnp_vibrator_ldo_driver = {
	.driver	= {
		.name		= "qcom,qpnp-vibrator-gpio",
		.owner = THIS_MODULE,
		.of_match_table	= vibrator_ldo_match_table,
	},
	.probe	= qpnp_vibrator_ldo_probe,
	.remove	= qpnp_vibrator_ldo_remove,
};
module_platform_driver(qpnp_vibrator_ldo_driver);

MODULE_DESCRIPTION("QPNP Vibrator-GPIO driver");
MODULE_LICENSE("GPL v2");
