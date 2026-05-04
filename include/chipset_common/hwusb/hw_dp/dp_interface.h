/*
 * dp_interface.h
 *
 * dp interface header file
 *
 * Copyright (c) 2021-2021 Huawei Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __DP_INTERFACE_H__
#define __DP_INTERFACE_H__

#include <linux/kernel.h>
#include <linux/string.h>

#include <huawei_platform/log/imonitor.h>
#include <chipset_common/hwusb/hw_dp/dp_dsm.h>
#include <chipset_common/hwusb/hw_dp/dp_factory.h>
#include <chipset_common/hwusb/hw_dp/dp_hw_debug.h>
#include <chipset_common/hwusb/hw_dp/dp_source_switch.h>

#include <dp/dp_catalog.h>
#include <dp/dp_link.h>
#include <dp/dp_panel.h>

#ifdef CONFIG_HUAWEI_DP_QCOM
#ifdef DP_DSM_ENABLE
void qcom_dp_set_basic_info(struct dp_panel *panel,
	struct dp_link *link, struct dp_catalog *catalog);
void qcom_dp_set_sink_caps(struct dp_panel *dp_panel);
void qcom_dp_set_pd_event(uint8_t cur_hpd_high,
	uint8_t connected, uint8_t dp_data, uint8_t orientation);
#else
static inline void qcom_dp_set_basic_info(struct dp_panel *panel,
	struct dp_link *link, struct dp_catalog *catalog)
{
}

static inline void qcom_dp_set_sink_caps(struct dp_panel *dp_panel)
{
}

static inline void qcom_dp_set_pd_event(uint8_t cur_hpd_high,
	uint8_t connected, uint8_t dp_data, uint8_t orientation)
{
}
#endif /* DP_DSM_ENABLE */
#endif /* CONFIG_HUAWEI_DP_PLATFORM */

#ifdef DP_DSM_ENABLE
/* Interface provided by hw, include data monitor, event report, and etc. */
void huawei_dp_imonitor_set_param(enum dp_imonitor_param param,
	void *data);
void huawei_dp_imonitor_set_param_aux_rw(bool rw, bool i2c,
	uint32_t addr, uint32_t len, int retval);
void huawei_dp_imonitor_set_param_timing(uint16_t h_active,
	uint16_t v_active, uint32_t pixel_clock);
void huawei_dp_imonitor_set_param_vs_pe(int index, uint8_t *vs,
	uint8_t *pe);
bool huawei_dp_factory_mode_is_enable(void);
void huawei_dp_factory_link_cr_or_ch_eq_fail(bool is_cr);
bool huawei_dp_factory_is_4k_60fps(uint8_t rate, uint8_t lanes,
	uint16_t h_active, uint16_t v_active, uint8_t fps);

#ifdef CONFIG_VR_DISPLAY
void huawei_dp_set_dptx_vr_status(bool dptx_vr);
#endif /* CONFIG_VR_DISPLAY */

int huawei_dp_get_current_dp_source_mode(void);
void huawei_dp_imonitor_set_pd_event(uint8_t irq_type,
	uint8_t cur_mode, uint8_t mode_type, uint8_t dev_type, uint8_t typec_orien);
void huawei_dp_save_edid(const uint8_t *edid_buf, uint32_t buf_len);
#else
static inline void huawei_dp_imonitor_set_param(
	enum dp_imonitor_param param, void *data)
{
}

static inline void huawei_dp_imonitor_set_param_aux_rw(bool rw,
	bool i2c, uint32_t addr, uint32_t len, int retval)
{
}

static inline void huawei_dp_imonitor_set_param_timing(
	uint16_t h_active, uint16_t v_active, uint32_t pixel_clock)
{
}

static inline void huawei_dp_imonitor_set_param_vs_pe(int index,
	uint8_t *vs, uint8_t *pe)
{
}

static inline bool huawei_dp_factory_mode_is_enable(void)
{
	return false;
}

static inline void huawei_dp_factory_link_cr_or_ch_eq_fail(
	bool is_cr)
{
}

static inline bool huawei_dp_factory_is_4k_60fps(uint8_t rate, uint8_t lanes,
	uint16_t h_active, uint16_t v_active, uint8_t fps)
{
	return true;
}

#ifdef CONFIG_VR_DISPLAY
static inline void huawei_dp_set_dptx_vr_status(bool dptx_vr) {}
#endif /* CONFIG_VR_DISPLAY */

static inline int huawei_dp_get_current_dp_source_mode(void)
{
	return SAME_SOURCE;
}

static inline void huawei_dp_imonitor_set_pd_event(uint8_t irq_type,
	uint8_t cur_mode, uint8_t mode_type, uint8_t dev_type, uint8_t typec_orien)
{
}

static inline void huawei_dp_save_edid(const uint8_t *edid_buf,
	uint32_t buf_len)
{
}
#endif /* DP_DSM_ENABLE */

#endif /* DP_INTERFACE_H */

