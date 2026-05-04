/************************************************************
*
* Copyright (C), 1988-1999, Huawei Tech. Co., Ltd.
* FileName: hw_typec.c
* Author: suoandajie(00318894)       Version : 0.1      Date:  2016-5-9
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*  Description:    .c file for type-c core layer which is used to handle
*                  pulic logic management for different chips and to
*                  provide interfaces for exteranl modules.
*  Version:
*  Function List:
*  History:
*  <author>  <time>   <version >   <desc>
***********************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/pm_wakeup.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/usb/role.h>
#include <linux/version.h>
#include <linux/console.h>
#include <securec.h>
#include <huawei_platform/log/hw_log.h>
#include <huawei_platform/usb/hw_pd_dev.h>
#include <ana_hs_kit/ana_hs_extern_ops/ana_hs_extern_ops.h>
#include <linux/power/huawei_charger.h>
#include <chipset_common/hwpower/common_module/power_sysfs.h>
#include <chipset_common/hwpower/hardware_monitor/water_detect.h>
#include <huawei_platform/power/direct_charger/direct_charger.h>
#include <chipset_common/hwpower/common_module/power_icon.h>
#include <chipset_common/hwpower/hardware_channel/vbus_channel.h>
#include <chipset_common/hwusb/hw_usb.h>
#include <chipset_common/hwpower/common_module/power_cmdline.h>
#include <chipset_common/hwpower/common_module/power_interface.h>
#include <chipset_common/hwpower/common_module/power_event_ne.h>
#include <chipset_common/hwpower/common_module/power_dts.h>
#include <chipset_common/hwpower/hardware_ic/boost_5v.h>
#include <chipset_common/hwpower/hardware_channel/vbus_channel.h>
#include <huawei_platform/hwpower/common_module/power_glink.h>

#define CC_SHORT_DEBOUNCE 50 /* ms */

struct pd_dpm_info *g_pd_di;
static bool g_pd_cc_moisture_status;
#if IS_ENABLED(CONFIG_QTI_PMIC_GLINK)
static int g_is_direct_charge_cable = 1;
#endif
static int g_vconn_enable;
static int g_vbus_enable;
static unsigned int cc_dynamic_protect;
struct cc_check_ops* g_cc_check_ops;

#define PD_DPM_MAX_OCP_COUNT 1000
#define OCP_DELAY_TIME 5000
#define DISABLE_INTERVAL 50
#define GET_IBUS_INTERVAL 1000
#define MMI_PD_TIMES 3
#define MMI_PD_IBUS_MIN 300
#define VBUS_STATE_CONNECT                 1
#define WAIT_DEIAY_QUEUES                      1000 /* 1000 ms */
#define WAIT_DEIAY_SECONDARY_IDENTIFICATION    5000 /* 5000 ms */

#define PD_MISC_GET_CC_STATUS                  0xEE
#define PD_MISC_DATA_CUT_OFF                   0xFF
#define PD_MISC_GET_PROPERTY_VALUE             0xFFFFFFFF

#ifndef HWLOG_TAG
#define HWLOG_TAG huawei_pd
HWLOG_REGIST();
#endif

static bool g_pd_cc_orientation;
static bool g_pd_glink_status;
static int g_pd_dpm_typec_state = PD_DPM_USB_TYPEC_DETACHED;
struct pd_ucsi_ops *g_ucsi_ops;

void pd_dpm_ucsi_ops_register(struct pd_ucsi_ops *ops)
{
	if (!ops)
		return;

	g_ucsi_ops = ops;
}

int pd_dpm_data_role_swap(enum pd_dpm_data_role role)
{
	if (!g_ucsi_ops || !g_ucsi_ops->dr_swap_set)
		return -EINVAL;

	return g_ucsi_ops->dr_swap_set(role);
}

int pd_dpm_set_usb_role(int role)
{
	if (!g_ucsi_ops || !g_ucsi_ops->set_usb_con)
		return -EINVAL;

	return g_ucsi_ops->set_usb_con(role);
}

void pd_dpm_get_typec_state(int *typec_state)
{
	hwlog_info("%s  = %d\n", __func__, g_pd_dpm_typec_state);

       *typec_state = g_pd_dpm_typec_state;
}

void pd_dpm_set_typec_state(int typec_state)
{
       hwlog_info("%s pd_dpm_set_typec_state  = %d\n", __func__, typec_state);
       g_pd_dpm_typec_state = typec_state;

       if ((g_pd_dpm_typec_state == PD_DPM_USB_TYPEC_NONE) ||
	       (g_pd_dpm_typec_state == PD_DPM_USB_TYPEC_DETACHED) ||
	       (g_pd_dpm_typec_state == PD_DPM_USB_TYPEC_AUDIO_DETACHED)) {
	       hwlog_info("%s report detach, stop ovp & start res detect\n", __func__);
	       ana_hs_fsa4480_stop_ovp_detect(1);
	       ana_hs_fsa4480_start_res_detect(1);
       } else {
	       hwlog_info("%s report attach, stop res & start ovp detect\n", __func__);
	       ana_hs_fsa4480_stop_res_detect(0);
	       ana_hs_fsa4480_start_ovp_detect(0);
       }
}

void pd_dpm_set_glink_status(void)
{
	struct pd_dpm_info *di = g_pd_di;

	g_pd_glink_status = true;
	if (!di)
		return;

	schedule_work(&di->pd_work);
}

bool pd_dpm_get_ctc_cable_flag(void)
{
	if (g_pd_di)
		return g_pd_di->ctc_cable_flag;

	return false;
}
bool pd_dpm_check_cc_vbus_short(void)
{
#if IS_ENABLED(CONFIG_QTI_PMIC_GLINK)
	unsigned int cc = 0;

	hwlog_info("%s: check cc state begin\n", __func__);
	(void)power_glink_get_property_value(POWER_GLINK_PROP_ID_GET_CC_SHORT_STATUS, &cc, 1);
	hwlog_info("%s: check cc state cc is %d\n", __func__, cc);
	return cc <= 1 ? true : false;
#else
	return false;
#endif
}

bool pd_dpm_get_cc_moisture_status(void)
{
	return g_pd_cc_moisture_status;
}

enum cur_cap pd_dpm_get_cvdo_cur_cap(void)
{
	if (!g_pd_di)
		return PD_DPM_CURR_3A;

	hwlog_info("%s, cur_cap = %d\n", __func__, g_pd_di->cable_vdo);
	return g_pd_di->cable_vdo;
}

void pd_dpm_ignore_vbus_only_event(bool flag)
{
}

int pd_dpm_notify_direct_charge_status(bool dc)
{
	hwlog_info("%s, not thridpart pd", __func__);
	return 0;
}

static int usb_rdy;
void set_usb_rdy(void)
{
	usb_rdy = 1;
}

bool is_usb_rdy(void)
{
	if (usb_rdy)
		return true;
	else
		return false;
}

void pd_dpm_set_cc_orientation(int cc_orientation)
{
	g_pd_cc_orientation = cc_orientation;
}

bool pd_dpm_get_cc_orientation(void)
{
	return g_pd_cc_orientation;
}

void pd_dpm_set_pd_charger_status(void *data)
{
	if (!data || !g_pd_di)
		return;

	g_pd_di->ctc_cable_flag = *(u32 *)data;
}

bool pd_dpm_check_pd_charger(void)
{
	if (!g_pd_di)
		return false;

	return g_pd_di->ctc_cable_flag;
}

static ssize_t pd_dpm_cc_orientation_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", pd_dpm_get_cc_orientation() ? "2" : "1");
}

static ssize_t pd_dpm_pd_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", pd_dpm_check_pd_charger() ? "0" : "1");
}

static DEVICE_ATTR(cc_orientation, S_IRUGO, pd_dpm_cc_orientation_show, NULL);
static DEVICE_ATTR(pd_state, S_IRUGO, pd_dpm_pd_state_show, NULL);

static struct attribute *pd_dpm_ctrl_attributes[] = {
	&dev_attr_cc_orientation.attr,
	&dev_attr_pd_state.attr,
	NULL,
};

static const struct attribute_group pd_dpm_attr_group = {
	.attrs = pd_dpm_ctrl_attributes,
};

static void pd_dpm_parse_orientation_cc(void *data)
{
	if (!data)
		return;

	pd_dpm_set_cc_orientation(*(u32 *)data);
}

void pd_dpm_cc_dynamic_protect(void)
{
	struct pd_dpm_info *di = g_pd_di;

	if (!di)
		return;
	schedule_delayed_work(&di->cc_short_work, msecs_to_jiffies(CC_SHORT_DEBOUNCE));
}

static void pd_dpm_sent_otg_src_type(struct pd_dpm_info *di)
{
	u32 otg_src[PD_OTG_SRC_TYPE_LEN];
	int status = -EINVAL;
	int retry_count = 0;

	otg_src[PD_OTG_SRC_VCONN] = di->src_vconn;
	otg_src[PD_OTG_SRC_VBUS] = di->src_vbus;

	do {
		msleep(500); /* wait 500ms for adsp init done */
		status = power_glink_set_property_value(POWER_GLINK_PROP_ID_SET_OTG, otg_src,
			PD_OTG_SRC_TYPE_LEN);
	} while ((status != 0) && (retry_count++ < 10)); /* max retry count is 10 */
}

static void pd_dpm_ctrl_vconn(void *data)
{
	struct pd_dpm_info *di = g_pd_di;

	if (!di || !data)
		return;

	g_vconn_enable = *(u32 *)data;
	schedule_delayed_work(&di->vconn_src_work, msecs_to_jiffies(PD_OTG_VCONN_DELAY));
}

static void pd_dpm_ctrl_vbus(void *data)
{
	struct pd_dpm_info *di = g_pd_di;

	if (!di || !data)
		return;

	g_vbus_enable = *(u32 *)data;
	schedule_delayed_work(&di->vbus_src_work, msecs_to_jiffies(PD_OTG_VBUS_DELAY));
}

static void pd_dpm_vbus_close(struct pd_dpm_info *di)
{
	unsigned int val;
	bool vbus_flag = true;

	switch (di->src_vbus) {
	case PD_OTG_5VBST:
		if (di->vbus_mode == LOW_POWER)
			vbus_flag = false;

		vbus_ch_close(VBUS_CH_USER_PD, VBUS_CH_TYPE_BOOST_GPIO, vbus_flag, false);
		break;
	case PD_OTG_PMIC:
		val = 0;
		power_glink_set_property_value(POWER_GLINK_PROP_ID_SET_VBUS_CTRL, &val, 1);
		break;
	default:
		break;
	}
}

static void pd_dpm_ctrl_vconn_work(struct work_struct *work)
{
	boost_5v_enable(g_vconn_enable, BOOST_CTRL_PD_VCONN);
}

static void pd_dpm_ctrl_vbus_work(struct work_struct *work)
{
	struct pd_dpm_info *di = container_of(work, struct pd_dpm_info,
		vbus_src_work.work);

	if (!di)
		return;

	if (g_vbus_enable)
		vbus_ch_open(VBUS_CH_USER_PD, VBUS_CH_TYPE_BOOST_GPIO, false);
	else
		pd_dpm_vbus_close(di);
}

static void pd_dpm_quick_charge(void *data)
{
	if (!data)
		return;

	power_icon_notify(ICON_TYPE_QUICK);
}

static void pd_dpm_event_work(struct work_struct *work)
{
	struct pd_dpm_info *di = container_of(work, struct pd_dpm_info,
		pd_work);

	if (!di)
		return;

	pd_dpm_sent_otg_src_type(di);
}

static bool pd_dpm_is_cc_protection(void)
{
	if (!cc_dynamic_protect)
		return false;
	hwlog_info("[CHECK]cc_dynamic_protect is %u\n", cc_dynamic_protect);
#ifdef CONFIG_DIRECT_CHARGER
	if (direct_charge_in_charging_stage() != DC_IN_CHARGING_STAGE)
		return false;
#endif /* CONFIG_DIRECT_CHARGER */
	hwlog_info("cc short\n");
	return true;
}

static void pd_dpm_cc_short_action(void)
{
	unsigned int notifier_type = POWER_ANT_LVC_FAULT;
	int mode = direct_charge_get_working_mode();

	/* cc_dynamic_protect-0: disabled 1: only SC; 2: for SC and LVC */
	if (mode == SC_MODE)
		notifier_type = POWER_ANT_SC_FAULT;
	else if (mode == LVC_MODE && cc_dynamic_protect == 2)
		notifier_type = POWER_ANT_LVC_FAULT;
	hwlog_info("[CHECK]check notifier_type is %u\n", notifier_type);
	power_event_anc_notify(notifier_type, POWER_NE_DC_FAULT_CC_SHORT, NULL);
	hwlog_info("cc_short_action\n");
}

static void pd_dpm_cc_short_work(struct work_struct *work)
{
 	hwlog_info("cc_short_work\n");
	if (!pd_dpm_is_cc_protection())
		return;

	pd_dpm_cc_short_action();
}

void pd_dpm_set_emark_current(int cur)
{
	if (!g_pd_di)
		return;

	g_pd_di->cable_vdo = cur;
	hwlog_info("pd_dpm_get_emark_current cur=%d\n", g_pd_di->cable_vdo);
}

struct otg_ocp_para *pd_dpm_get_otg_ocp_check_para(void)
{
	if (!g_pd_di)
		return NULL;

	if (g_pd_di->otg_ocp_check_enable == 0)
		return NULL;

	return &g_pd_di->otg_ocp_check_para;
}

static int pd_dpm_event_notifier_call(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct pd_dpm_info *di = container_of(nb, struct pd_dpm_info, usb_nb);

	if (!di)
		return NOTIFY_OK;

	switch (event) {
	case POWER_NE_HW_PD_ORIENTATION_CC:
		pd_dpm_parse_orientation_cc(data);
		break;
	case POWER_NE_HW_PD_CHARGER:
		pd_dpm_set_pd_charger_status(data);
		break;
	case POWER_NE_HW_PD_SOURCE_VCONN:
		pd_dpm_ctrl_vconn(data);
		break;
	case POWER_NE_HW_PD_SOURCE_VBUS:
		di->vbus_mode = NORMAL;
		pd_dpm_ctrl_vbus(data);
		break;
	case POWER_NE_HW_PD_LOW_POWER_VBUS:
		di->vbus_mode = LOW_POWER;
		pd_dpm_ctrl_vbus(data);
		break;
	case POWER_NE_HW_PD_QUCIK_CHARGE:
		pd_dpm_quick_charge(data);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static void pd_dpm_parse_dts(struct pd_dpm_info *di)
{
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"src_vconn", &di->src_vconn, PD_OTG_SRC_VCONN_DEFAULT);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"src_vbus", &di->src_vbus, PD_OTG_SRC_VBUS_DEFAULT);

	/* fix a hardware issue, has ocp when open otg */
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"otg_ocp_check_enable", &di->otg_ocp_check_enable, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"otg_ocp_vol_mv", &di->otg_ocp_check_para.vol_mv,
		OTG_OCP_VOL_MV);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"otg_ocp_check_times", &di->otg_ocp_check_para.check_times,
		OTG_OCP_CHECK_TIMES);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"otg_ocp_check_delayed_time", &di->otg_ocp_check_para.delayed_time,
		OTG_OCP_CHECK_DELAYED_TIME);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"cc_dynamic_protect", &cc_dynamic_protect, 0);
	(void)power_dts_read_u32(power_dts_tag(HWLOG_TAG), di->dev->of_node,
		"quirks_fix_flag", &di->quirks_fix_flg, 0);
}

#if IS_ENABLED(CONFIG_QTI_PMIC_GLINK)
void pd_dpm_set_is_direct_charge_cable(int ret)
{
	g_is_direct_charge_cable = ret;
}

static int direct_charge_cable_detect(void)
{
#ifdef CONFIG_HUAWEI_POWER_EMBEDDED_ISOLATION
	unsigned int ret;
	unsigned int cab_flag = 0;

	hwlog_info("direct_charge_cable_detect %d\n", g_is_direct_charge_cable);
	if (g_is_direct_charge_cable) {
		ret = power_glink_get_property_value(POWER_GLINK_PROP_ID_SET_MISC_CMD,
			&cab_flag, 1);
		hwlog_info("%s get cable value is %x", __func__, cab_flag);
		if ((cab_flag & PD_MISC_DATA_CUT_OFF) == PD_MISC_GET_CC_STATUS) {
			hwlog_info("redetect charger cable\n");
			g_is_direct_charge_cable = 0;
	}
}
#endif

	return g_is_direct_charge_cable;
}
#else
void pd_dpm_set_is_direct_charge_cable(int ret)
{
}

static int direct_charge_cable_detect(void)
{
	int ret;

	if (!g_cc_check_ops)
		return -1;

	ret = g_cc_check_ops->is_cable_for_direct_charge();
	if (ret) {
		hwlog_info("%s:cc_check  fail\n", __func__);
		return -1;
	}
	return 0;
}
#endif

static struct dc_cable_ops cable_detect_ops = {
	.detect = direct_charge_cable_detect,
};

static void pd_dpm_chg_retrigger_dmd(void)
{
	int ret;
	char dsm_buff[POWER_DSM_BUF_SIZE_0128] = {0};

	hwlog_info("%s: BC1.2 secondary identification\n", __func__);
	ret = snprintf_s(dsm_buff, POWER_DSM_BUF_SIZE_0128, POWER_DSM_BUF_SIZE_0128 - 1,
		"%s: BC1.2 secondary identification\n", __func__);
	if (ret < 0) {
		hwlog_info("snprintf_s error:%d\n", ret);
		return;
	}
	ret = power_dsm_report_dmd(POWER_DSM_USB, DSM_USB_RETRIGGER_APSD, dsm_buff);
	hwlog_info("%s:power_dsm_report_dmd  ret = %d\n", __func__, ret);
}

static void pd_dpm_chg_retrigger_work(struct work_struct *work)
{
	int ret;
	bool recon = false;
	unsigned int val = 0;

	if (!g_pd_di)
		return;

	hwlog_info("%s: start work\n", __func__);
	if (g_ucsi_ops && g_ucsi_ops->get_usb_con && g_ucsi_ops->set_usb_con) {
		ret = g_ucsi_ops->get_usb_con();
		if (ret == USB_ROLE_DEVICE) {
			ret = g_ucsi_ops->set_usb_con(USB_ROLE_NONE);
			hwlog_info("%s:set_usb_con USB_ROLE_NONE. ret = %d\n", __func__, ret);
			recon = (ret == 0);
			msleep(WAIT_DEIAY_QUEUES);
		}
	}
	if (g_pd_di->vbus_conn == 0) {
		hwlog_info("%s: USB_STATE=DISCONNECTED\n", __func__);
		return;
	}
	ret = power_glink_set_property_value(
		POWER_GLINK_PROP_ID_SET_RETRIGGER_APSD, &val, GLINK_DATA_ONE);
	hwlog_info("%s: power_glink_set_property_value return. ret = %d\n", __func__, ret);
	pd_dpm_chg_retrigger_dmd();
	if (recon) {
		msleep(WAIT_DEIAY_SECONDARY_IDENTIFICATION);
		if (g_pd_di->vbus_conn == 0) {
			hwlog_info("%s: USB_STATE=DISCONNECTED\n", __func__);
			return;
		}
		ret = g_ucsi_ops->set_usb_con(USB_ROLE_DEVICE);
		hwlog_info("%s: set_usb_con USB_ROLE_DEVICE. ret = %d\n", __func__, ret);
	}
}

void pd_dpm_chg_event_notify(int event)
{
	static int vcon = false;

	if (!g_pd_di || !g_pd_di->quirks_fix_flg)
		return;

	hwlog_info("%s: vcon=%d, evt=%d\n", __func__, vcon, event);
	if (delayed_work_pending(&g_pd_di->chg_retrigger_work)) {
		hwlog_info("%s: cancel work\n", __func__);
		cancel_delayed_work(&g_pd_di->chg_retrigger_work);
	}
	if (event == CHG_EVT_VBUS_CONN) {
		g_pd_di->vbus_conn = VBUS_STATE_CONNECT;
		vcon = true;
		return;
	}

	if (event == CHG_EVT_VBUS_DISCONN)
		g_pd_di->vbus_conn = 0;

	if (vcon && (event == CHG_EVT_SDP)) {
		hwlog_info("%s: start work\n", __func__);
		schedule_delayed_work(&g_pd_di->chg_retrigger_work,
			msecs_to_jiffies(WAIT_DEIAY_SECONDARY_IDENTIFICATION));
	}
	vcon = false;
}

int cc_check_ops_register(struct cc_check_ops* ops)
{
	int ret = 0;

	if (g_cc_check_ops) {
		hwlog_err("cc_check ops register fail! g_cc_check_ops busy\n");
		return -EBUSY;
	}

	if (ops) {
		g_cc_check_ops = ops;
	} else {
		hwlog_err("cc_check ops register fail\n");
		ret = -EPERM;
	}
	return ret;
}

static void pd_dpm_otg_restore_work(struct work_struct *work)
{
	struct pd_dpm_info *di = g_pd_di;

	if (!di)
		return;

	pd_dpm_vbus_close(di);
}

static int pd_set_rtb_success(unsigned int val)
{
	struct pd_dpm_info *di = g_pd_di;
	int typec_state = PD_DPM_USB_TYPEC_NONE;

	/* 1:set board running test result success */
	if (!di || (val <= 0))
		return -1;

	if (!power_cmdline_is_factory_mode())
		return 0;

	pd_dpm_get_typec_state(&typec_state);
	if (typec_state == PD_DPM_USB_TYPEC_HOST_ATTACHED ||
		typec_state == PD_DPM_USB_TYPEC_DEVICE_ATTACHED ||
		typec_state == PD_DPM_USB_TYPEC_AUDIO_ATTACHED) {
		hwlog_err("typec port is attached, can not open vbus\n");
		return -1;
	}

	vbus_ch_open(VBUS_CH_USER_PD, VBUS_CH_TYPE_BOOST_GPIO, false);
	cancel_delayed_work_sync(&di->otg_restore_work);
	/* delay val*1000 ms */
	schedule_delayed_work(&di->otg_restore_work,
		msecs_to_jiffies(val * 1000));
	return 0;
}


static struct power_if_ops pd_if_ops = {
	.set_rtb_success = pd_set_rtb_success,
	.type_name = "pd",
};

static int pd_dpm_probe(struct platform_device *pdev)
{
	struct pd_dpm_info *di = NULL;
	int ret;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &pdev->dev;
	g_pd_di = di;
	pd_dpm_parse_dts(di);
	if (g_pd_glink_status)
		pd_dpm_sent_otg_src_type(di);

	g_pd_di->cable_vdo = 0;
	g_pd_di->ctc_cable_flag = false;
	platform_set_drvdata(pdev, di);

	dc_cable_ops_register(&cable_detect_ops);

	INIT_WORK(&di->pd_work, pd_dpm_event_work);
	INIT_DELAYED_WORK(&di->vconn_src_work, pd_dpm_ctrl_vconn_work);
	INIT_DELAYED_WORK(&di->vbus_src_work, pd_dpm_ctrl_vbus_work);
	INIT_DELAYED_WORK(&di->cc_short_work, pd_dpm_cc_short_work);
	INIT_DELAYED_WORK(&di->otg_restore_work, pd_dpm_otg_restore_work);
	INIT_DELAYED_WORK(&di->chg_retrigger_work, pd_dpm_chg_retrigger_work);
	power_if_ops_register(&pd_if_ops);
	di->usb_nb.notifier_call = pd_dpm_event_notifier_call;
	ret = power_event_bnc_register(POWER_BNT_HW_PD, &di->usb_nb);
	if (ret)
		goto notifier_regist_fail;

	power_sysfs_create_group("hw_typec", "typec", &pd_dpm_attr_group);
	return 0;

notifier_regist_fail:
	kfree(di);
	g_pd_di = NULL;
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static const struct of_device_id pd_dpm_callback_match_table[] = {
	{
		.compatible = "huawei,huawei_pd",
		.data = NULL,
	},
	{},
};

static struct platform_driver pd_dpm_callback_driver = {
	.probe = pd_dpm_probe,
	.remove = NULL,
	.driver = {
		.name = "huawei,huawei_pd",
		.owner = THIS_MODULE,
		.of_match_table = pd_dpm_callback_match_table,
	}
};

static int __init pd_dpm_init(void)
{
	return platform_driver_register(&pd_dpm_callback_driver);
}

static void __exit pd_dpm_exit(void)
{
	platform_driver_unregister(&pd_dpm_callback_driver);
}

module_init(pd_dpm_init);
module_exit(pd_dpm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("pd dpm logic driver");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
