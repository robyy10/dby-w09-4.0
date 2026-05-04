// SPDX-License-Identifier: GPL-2.0
/*
 * reverse_charge.c
 *
 * reverse charge with boost and protocol driver
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
#include <chipset_common/hwpower/reverse_charge/reverse_charge.h>
#include <chipset_common/hwpower/reverse_charge/reverse_charge_boost.h>
#include <chipset_common/hwpower/reverse_charge/reverse_charge_protocol.h>
#include <chipset_common/hwpower/common_module/power_common_macro.h>
#include <chipset_common/hwpower/common_module/power_interface.h>
#include <chipset_common/hwpower/common_module/power_printk.h>
#include <chipset_common/hwpower/common_module/power_sysfs.h>
#include <chipset_common/hwpower/common_module/power_dts.h>
#include <chipset_common/hwpower/common_module/power_wakeup.h>
#include <chipset_common/hwpower/common_module/power_supply_application.h>
#include <chipset_common/hwpower/hardware_channel/vbus_channel.h>
#include <chipset_common/hwpower/common_module/power_delay.h>
#include <chipset_common/hwpower/common_module/power_event_ne.h>
#include <chipset_common/hwpower/charger/charger_common_interface.h>
#include <chipset_common/hwpower/common_module/power_ui_ne.h>
#include <chipset_common/hwpower/common_module/power_icon.h>
#include <linux/math64.h>
#include <linux/power/huawei_charger.h>
#include <huawei_platform/usb/hw_pd_dev.h>

#define HWLOG_TAG reverse_charge
HWLOG_REGIST();

static struct reverse_charge_device *g_rchg_di;

static const struct rchg_boost_device_data g_boost_dev_data[] = {
	{ BOOST_DEVICE_ID_SC8933, "sc8933" },
};

static int boost_get_device_id(const char *str)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(g_boost_dev_data); i++) {
		if (!strncmp(str, g_boost_dev_data[i].name,
			strlen(str)))
			return g_boost_dev_data[i].id;
	}

	return -EPERM;
}

int boost_ops_register(struct boost_ops *ops)
{
	int dev_id;

	if (!g_rchg_di || !ops || !ops->chip_name) {
		hwlog_err("g_rchg_di or ops or chip_name is null\n");
		return -EPERM;
	}

	dev_id = boost_get_device_id(ops->chip_name);
	if (dev_id < 0) {
		hwlog_err("%s ops register fail\n", ops->chip_name);
		return -EPERM;
	}

	g_rchg_di->bst_ops = ops;
	g_rchg_di->bst_dev_id = dev_id;

	hwlog_info("%d:%s ops register ok\n", dev_id, ops->chip_name);
	return 0;
}

struct boost_ops *boost_get_ops(void)
{
	if (!g_rchg_di || !g_rchg_di->bst_ops) {
		hwlog_err("g_rchg_di or bst_ops is null\n");
		return NULL;
	}

	return g_rchg_di->bst_ops;
}

static const struct rchg_rprotocol_device_data g_rprotocol_dev_data[] = {
	{ RPROTOCOL_DEVICE_ID_STM32G031, "rstm32g031" },
};

static int rprotocol_get_device_id(const char *str)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(g_rprotocol_dev_data); i++) {
		if (!strncmp(str, g_rprotocol_dev_data[i].name,
			strlen(str)))
			return g_rprotocol_dev_data[i].id;
	}

	return -EPERM;
}

int rprotocol_ops_register(struct rprotocol_ops *ops)
{
	int dev_id;

	if (!g_rchg_di || !ops || !ops->chip_name) {
		hwlog_err("g_rchg_di or ops or chip_name is null\n");
		return -EPERM;
	}

	dev_id = rprotocol_get_device_id(ops->chip_name);
	if (dev_id < 0) {
		hwlog_err("%s ops register fail\n", ops->chip_name);
		return -EPERM;
	}

	g_rchg_di->rprot_ops = ops;
	g_rchg_di->rprot_dev_id = dev_id;

	hwlog_info("%d:%s ops register ok\n", dev_id, ops->chip_name);
	return 0;
}

struct rprotocol_ops *rprotocol_get_ops(void)
{
	if (!g_rchg_di || !g_rchg_di->rprot_ops) {
		hwlog_err("g_rchg_di or rprot_ops is null\n");
		return NULL;
	}

	return g_rchg_di->rprot_ops;
}

static void rchg_parse_thre_para(struct device_node *np,
	struct rchg_thre_para *data, const char *name)
{
	int row, col, len;
	int idata[RCHG_THRE_PARA_LEVEL * RCHG_THRE_TOTAL] = { 0 };

	len = power_dts_read_string_array(power_dts_tag(HWLOG_TAG), np,
		name, idata, RCHG_THRE_PARA_LEVEL, RCHG_THRE_TOTAL);
	if (len < 0)
		return;

	for (row = 0; row < len / RCHG_THRE_TOTAL; row++) {
		col = row * RCHG_THRE_TOTAL + RCHG_THRE_HIGH;
		data[row].thre_high = idata[col];
		col = row * RCHG_THRE_TOTAL + RCHG_THRE_LOW;
		data[row].thre_low = idata[col];
		col = row * RCHG_THRE_TOTAL + RCHG_THRE_IBUS;
		data[row].ibus_limit = idata[col];
	}

	for (row = 0; row < len / RCHG_THRE_TOTAL; row++)
		hwlog_info("thre_para[%d]: %d %d %d\n",
		row, data[row].thre_high, data[row].thre_low, data[row].ibus_limit);
}

static int rchg_parse_dts(struct device_node *np, struct reverse_charge_device *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "soc_protect_th",
		&di->soc_protect_th, RCHG_SOC_PROTECT_TH);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "temp_h_protect_th",
		&di->temp_h_protect_th, RCHG_TEMP_H_PROTECT_TH);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "temp_l_protect_th",
		&di->temp_l_protect_th, RCHG_TEMP_L_PROTECT_TH);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "vbat_protect_th",
		&di->vbat_protect_th, RCHG_VBAT_PROTECT_TH);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "vbus_init",
		&di->vbus_init, RCHG_VBUS_OUTPUT);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "ibus_init",
		&di->ibus_init, RCHG_IBUS_OUTPUT);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "ibus_h_temp",
		&di->ibus_h_temp, RCHG_IBUS_H_TEMP);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "rchg_use_boost",
		&di->rchg_use_boost, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "mmi_pass_vbus",
		&di->mmi_pass_vbus, RCHG_MMI_PASS_VBUS);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "mmi_pass_ibat",
		&di->mmi_pass_ibat, RCHG_MMI_PASS_IBAT);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "rchg_hv_support",
		&di->rchg_hv_support, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), np, "pwr_consum_ratio",
		&di->pwr_consum_ratio, RCHG_PWR_CONSUM_RATIO);

	rchg_parse_thre_para(np, di->soc_para, "soc_para");
	rchg_parse_thre_para(np, di->temp_para, "temp_para");
	rchg_parse_thre_para(np, di->pwr_consum_para, "pwr_consum_para");
	return 0;
}

static void psy_tst_work(struct work_struct *work)
{
	struct reverse_charge_device *di = NULL;

	di = container_of(work, struct reverse_charge_device,
		psy_tst_work.work);
	if (!di) {
		hwlog_err("%s: di is null\n", __func__);
		return;
	}

	(void)power_msleep(DT_MSLEEP_200MS, 0, NULL); /* wait equipment usb off */
	hwlog_info("dbc test, supply 5v vbus\n");
	(void)boost_set_idle_mode(0);
	(void)boost_set_vcg_on(1);
	(void)boost_set_vbus(di->vbus_init);
	(void)boost_ic_enable(1);

	(void)power_msleep(DT_MSLEEP_2S, 0, NULL); /* supply power for 2 sec */
	(void)boost_set_vcg_on(0);
	(void)boost_ic_enable(0);
	(void)boost_set_idle_mode(1);
}

#ifdef CONFIG_SYSFS
static ssize_t rchg_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t rchg_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static struct power_sysfs_attr_info rchg_sysfs_field_tbl[] = {
	power_sysfs_attr_rw(rchg, 0644, RCHG_SYSFS_RCHG_ENABLE, rchg_enable), /* for dbc test */
	power_sysfs_attr_rw(rchg, 0644, RCHG_SYSFS_RCHG_SUCCESS, rchg_success), /* for mmi test */
	power_sysfs_attr_rw(rchg, 0644, RCHG_SYSFS_RCHG_HV_SUPPORT, rchg_hv_support),
};

#define RCHG_SYSFS_ATTRS_SIZE  ARRAY_SIZE(rchg_sysfs_field_tbl)

static struct attribute *rchg_sysfs_attrs[RCHG_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group rchg_sysfs_attr_group = {
	.attrs = rchg_sysfs_attrs,
};

static ssize_t rchg_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct power_sysfs_attr_info *info = NULL;
	struct reverse_charge_device *di = dev_get_drvdata(dev);
	int len = 0;
	int rchg_success;

	info = power_sysfs_lookup_attr(attr->attr.name,
		rchg_sysfs_field_tbl, RCHG_SYSFS_ATTRS_SIZE);
	if (!info || !di)
		return -EINVAL;

	switch (info->name) {
	case RCHG_SYSFS_RCHG_SUCCESS:
		rchg_success = (int)((rprot_get_request_vbus() >= di->mmi_pass_vbus) &&
			(abs(power_supply_app_get_bat_current_now()) >= di->mmi_pass_ibat));
		hwlog_info("mmi visit rchg success: %d\n", rchg_success);
		return snprintf(buf, MAX_SIZE, "%d\n", rchg_success);
	case RCHG_SYSFS_RCHG_HV_SUPPORT:
		hwlog_info("rchg_hv_support: %d\n", di->rchg_hv_support);
		return snprintf(buf, MAX_SIZE, "%d\n", di->rchg_hv_support);
	default:
		break;
	}

	return len;
}

static ssize_t rchg_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct power_sysfs_attr_info *info = NULL;
	struct reverse_charge_device *di = dev_get_drvdata(dev);

	info = power_sysfs_lookup_attr(attr->attr.name,
		rchg_sysfs_field_tbl, RCHG_SYSFS_ATTRS_SIZE);
	if (!info || !di)
		return -EINVAL;

	switch (info->name) {
	case RCHG_SYSFS_RCHG_ENABLE:
		mod_delayed_work(system_power_efficient_wq,
			&di->psy_tst_work, 0);
		break;
	default:
		break;
	}

	return count;
}

static void rchg_sysfs_create_group(struct device *dev)
{
	power_sysfs_init_attrs(rchg_sysfs_attrs,
		rchg_sysfs_field_tbl, RCHG_SYSFS_ATTRS_SIZE);
	power_sysfs_create_link_group("hw_power", "charger", "reverse_charge",
		dev, &rchg_sysfs_attr_group);
}

static void rchg_sysfs_remove_group(struct device *dev)
{
	power_sysfs_remove_link_group("hw_power", "charger", "reverse_charge",
		dev, &rchg_sysfs_attr_group);
}
#else
static inline void rchg_sysfs_create_group(struct device *dev)
{
}

static inline void rchg_sysfs_remove_group(struct device *dev)
{
}
#endif /* CONFIG_SYSFS */

static int rchg_get_para_thre(int vol, struct rchg_thre_para *para)
{
	int i = 0;

	for (; i < RCHG_THRE_PARA_LEVEL; i++) {
		if ((vol <= para[i].thre_high) &&
			(vol > para[i].thre_low))
			return para[i].ibus_limit;
	}

	return -1;
}

static int rchg_check_battery_status(struct reverse_charge_device *di,
	int *vbus_limit, int *ibus_limit)
{
	int bat_temp = power_supply_app_get_bat_temp();
	int soc = power_supply_app_get_bat_capacity();
	int vbat = power_supply_app_get_bat_voltage_now();

	if (bat_temp >= di->temp_h_protect_th) {
		*vbus_limit = di->vbus_init;
		*ibus_limit = di->ibus_h_temp;
		return 0;
	}
	if ((bat_temp <= di->temp_l_protect_th) ||
		(vbat <= di->vbat_protect_th) ||
		(soc <= di->soc_protect_th)) {
		*vbus_limit = di->vbus_init;
		*ibus_limit = di->ibus_init;
		return 0;
	}

	return 1;
}

static void rchg_send_icon_uevent(int icon_type)
{
	struct reverse_charge_device *di = g_rchg_di;
	char rchg_buf[POWER_DSM_BUF_SIZE_0128] = { 0 };

	if (!di)
		return;

	hwlog_info("%s enter,icon_type=%d, last_icon_type=%d\n",
		__func__, icon_type, di->last_icon_type);
	if (icon_type == di->last_icon_type)
		return;

	/* report rchg info */
	snprintf(rchg_buf, POWER_DSM_BUF_SIZE_0128 - 1,
		"dmd_icon_type=%d\n", icon_type);

	if (icon_type == ICON_TYPE_RCHG_SUPER)
		power_dsm_report_dmd(POWER_DSM_BATTERY,
			POWER_DSM_RCHG_TYPE_SUPER, rchg_buf);
	else if (icon_type == ICON_TYPE_RCHG_NORMAL)
		power_dsm_report_dmd(POWER_DSM_BATTERY,
			POWER_DSM_RCHG_TYPE_NORMAL, rchg_buf);

	di->last_icon_type = icon_type;
	power_icon_notify(icon_type);
}

static void rchg_calculate_charge_param(struct reverse_charge_device *di,
	int *vbus_limit, int *ibus_limit, int *drop_ibus)
{
	int soc;
	int bat_temp;
	int vbat;
	int ibat;
	int pwr_consume;
	int temp_ibus;

	if (!vbus_limit || !ibus_limit || !drop_ibus)
		return;

	vbat = power_supply_app_get_bat_voltage_now();
	soc = power_supply_app_get_bat_capacity();
	bat_temp = power_supply_app_get_bat_temp();
	ibat = power_supply_app_get_bat_current_now();

	/* step-1: get requested vbus/ibus */
	*vbus_limit = rprot_get_request_vbus();
	*ibus_limit = rprot_get_request_ibus();

	/* step-2: update ibus limit by soc */
	temp_ibus = rchg_get_para_thre(soc, di->soc_para);
	*drop_ibus = temp_ibus;
	hwlog_info("soc:%d, temp_ibus:%d\n", soc, temp_ibus);
	if (temp_ibus > 0)
		*ibus_limit = min(*ibus_limit, temp_ibus);

	/* step-3: update ibus limit by temp */
	temp_ibus = rchg_get_para_thre(bat_temp, di->temp_para);
	hwlog_info("bat_temp:%d, temp_ibus:%d\n", bat_temp, temp_ibus);
	if (temp_ibus > 0) {
		*ibus_limit = min(*ibus_limit, temp_ibus);
		*drop_ibus = min(*drop_ibus, temp_ibus);
	}

	/* step-4: update ibus limit by power consume */
	pwr_consume = abs(ibat) - rprot_get_rt_ibus() *
		di->pwr_consum_ratio / POWER_BASE_DEC;
	temp_ibus = rchg_get_para_thre(pwr_consume, di->pwr_consum_para);
	hwlog_info("ibat:%d, pwr_consume:%d, temp_ibus:%d\n",
		ibat, pwr_consume, temp_ibus);
	if (temp_ibus > 0) {
		*ibus_limit = min(*ibus_limit, temp_ibus);
		*drop_ibus = min(*drop_ibus, temp_ibus);
	}
}

static void rchg_control_work(struct work_struct *work)
{
	int vbus_limit = 0;
	int ibus_limit = 0;
	int drop_ibus = 0;
	struct reverse_charge_device *di = NULL;

	di = container_of(work, struct reverse_charge_device,
		rchg_control_work.work);
	if (!di) {
		hwlog_err("%s: di is null\n", __func__);
		return;
	}

	if (!rchg_check_battery_status(di, &vbus_limit, &ibus_limit)) {
		hwlog_err("%s: battery status bad, not do super charge\n", __func__);
		goto next_loop;
	}
	if (!rprot_check_protocol_state()) {
		hwlog_err("%s: scp control mode fail, not do super charge\n", __func__);
		vbus_limit = di->vbus_init;
		ibus_limit = di->ibus_init;
		goto next_loop;
	}

	rchg_send_icon_uevent(ICON_TYPE_RCHG_SUPER);
	rchg_calculate_charge_param(di, &vbus_limit, &ibus_limit, &drop_ibus);

next_loop:
	boost_set_ibus(ibus_limit);
	boost_set_vbus(vbus_limit);
	rprot_update_vbus(vbus_limit);
	rprot_update_drop_cur(drop_ibus);
	schedule_delayed_work(&g_rchg_di->rchg_control_work,
		msecs_to_jiffies(RCHG_CONTROL_WORK_DELAY_TIME));
}

static int reverse_charge_stop(struct reverse_charge_device *di)
{
	int ret;

	hwlog_info("%s enter\n", __func__);
	ret = charge_set_hiz_enable(HIZ_MODE_DISABLE);

	ret += boost_set_idle_mode(BST_IDLE_STATE);
	ret += boost_set_vcg_on(BST_VCG_OFF);
	ret += boost_ic_enable(0);

	ret += rprot_enable_sleep(1);
	cancel_delayed_work_sync(&di->rchg_control_work);
	di->last_icon_type = ICON_TYPE_INVALID;
	power_wakeup_unlock(di->rchg_lock, false);

	return ret;
}

static int reverse_charge_start(struct reverse_charge_device *di)
{
	int ret = 0;
	int role_swap_flag;

	role_swap_flag = get_role_swap_flag();
	hwlog_info("%s enter, role_swap_flag = %d\n", __func__, role_swap_flag);
	power_wakeup_lock(di->rchg_lock, false);

	ret += boost_set_idle_mode(BST_NORMAL_STATE);
	ret += boost_set_vcg_on(BST_VCG_ON);
	ret += boost_set_ibus(di->ibus_init);
	ret += boost_set_vbus(di->vbus_init);
	ret += boost_ic_enable(1);
	(void)power_msleep(DT_MSLEEP_10MS, 0, NULL);
	ret += charge_set_hiz_enable(HIZ_MODE_ENABLE);

	if (role_swap_flag)
		return ret;

	ret += rprot_ic_reset();
	ret += rprot_enable_rscp(1);
	ret += rprot_update_vbus(di->vbus_init);
	if (ret)
		hwlog_err("rchg start err\n");

	mod_delayed_work(system_power_efficient_wq,
		&di->rchg_control_work, 0);

	return ret;
}

int reverse_charge_enable(int enable)
{
	if (!g_rchg_di) {
		hwlog_err("%s: g_rchg_di is null, enable = %d\n", __func__, enable);
		return -EINVAL;
	}

	if (!g_rchg_di->rchg_use_boost) {
		hwlog_info("use buck charger to supply otg power\n");
		return charge_otg_mode_enable(enable, VBUS_CH_USER_WIRED_OTG);
	}

	if (enable)
		 return reverse_charge_start(g_rchg_di);
	else
		return reverse_charge_stop(g_rchg_di);
}

static int rchg_notifier_call(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct reverse_charge_device *di = g_rchg_di;

	if (!di)
		return NOTIFY_OK;

	switch (event) {
	case POWER_NE_RVS_CHG_RESET_PROTOCOL:
		hwlog_info("%s rchg reset protocol\n", __func__);
		rprot_ic_reset();
		break;
	case POWER_NE_RVS_CHG_PROTOCOL_FAIL:
		hwlog_info("%s rchg protocol fail\n", __func__);
		rchg_send_icon_uevent(ICON_TYPE_RCHG_NORMAL);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int reverse_charge_probe(struct platform_device *pdev)
{
	int ret;
	struct reverse_charge_device *di = NULL;
	struct device_node *np = NULL;

	if (!pdev || !pdev->dev.of_node)
		return -ENODEV;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &pdev->dev;
	np = di->dev->of_node;

	ret = rchg_parse_dts(np, di);
	if (ret)
		goto fail_free_mem;

	rchg_sysfs_create_group(di->dev);
	INIT_DELAYED_WORK(&di->rchg_control_work, rchg_control_work);
	INIT_DELAYED_WORK(&di->psy_tst_work, psy_tst_work);
	di->rchg_nb.notifier_call = rchg_notifier_call;
	ret = power_event_bnc_register(POWER_BNT_REVERSE_CHG, &di->rchg_nb);
	if (ret)
		goto fail_free_mem;
	di->rchg_lock = power_wakeup_source_register(di->dev, "reverse_charge_wakelock");

	g_rchg_di = di;
	platform_set_drvdata(pdev, di);
	hwlog_info("%s success\n", __func__);
	return 0;

fail_free_mem:
	devm_kfree(&pdev->dev, di);
	g_rchg_di = NULL;

	return ret;
}

static int reverse_charge_remove(struct platform_device *pdev)
{
	struct reverse_charge_device *di = platform_get_drvdata(pdev);

	if (!di)
		return -ENODEV;

	rchg_sysfs_remove_group(di->dev);
	cancel_delayed_work(&di->rchg_control_work);
	devm_kfree(&pdev->dev, di);
	g_rchg_di = NULL;

	return 0;
}

static const struct of_device_id reverse_charge_match_table[] = {
	{
		.compatible = "huawei,reverse_charge",
		.data = NULL,
	},
	{},
};

static struct platform_driver reverse_charge_driver = {
	.probe = reverse_charge_probe,
	.remove = reverse_charge_remove,
	.driver = {
		.name = "reverse_charge",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(reverse_charge_match_table),
	},
};

static int __init reverse_charge_init(void)
{
	return platform_driver_register(&reverse_charge_driver);
}

static void __exit reverse_charge_exit(void)
{
	platform_driver_unregister(&reverse_charge_driver);
}

subsys_initcall_sync(reverse_charge_init);
module_exit(reverse_charge_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("reverse charge with boost and protocol module driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
