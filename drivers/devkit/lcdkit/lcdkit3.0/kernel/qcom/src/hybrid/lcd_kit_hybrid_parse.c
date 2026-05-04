// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_kit_hybrid_parse.c
 *
 * dts source file for hybrid control
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
#include "lcd_kit_hybrid_parse.h"
#include "lcd_kit_parse.h"

void hybrid_parse_dt(struct device_node *np, struct lcd_kit_hybrid_info *hybrid_info)
{
	if (!np || !hybrid_info)
		return;

	lcd_kit_parse_dcs_cmds(np, "lcd-kit,te-off-cmds", "lcd-kit,te-off-cmds-state",
			       &hybrid_info->te_off_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,te-on-cmds", "lcd-kit,te-on-cmds-state",
			       &hybrid_info->te_on_cmds);
	/* aod cmd */
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,alpm-on-hl-cmds", "lcd-kit,alpm-on-hl-cmds-state",
			       &hybrid_info->alpm_on_hl_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,alpm-on-ll-cmds", "lcd-kit,alpm-on-ll-cmds-state",
			       &hybrid_info->alpm_on_ll_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,alpm-on-ml-cmds", "lcd-kit,alpm-on-ml-cmds-state",
			       &hybrid_info->alpm_on_ml_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,alpm-on-hbml-cmds", "lcd-kit,alpm-on-hbml-cmds-state",
			       &hybrid_info->alpm_on_hbml_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,alpm-on-nl-cmds", "lcd-kit,alpm-on-nl-cmds-state",
			       &hybrid_info->alpm_on_nl_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,alpm-on-cmds", "lcd-kit,alpm-on-cmds-state",
			       &hybrid_info->alpm_on_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,alpm-exit-cmds", "lcd-kit,alpm-exit-cmds-state",
			       &hybrid_info->alpm_exit_cmds);

	hybrid_info->esd_enabled = of_property_read_bool(np, "qcom,esd-check-enabled");

	LCD_KIT_INFO("parse esd_enabled:%d\n", hybrid_info->esd_enabled);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,status-cmds", "lcd-kit,status-cmds-state",
			       &hybrid_info->status_cmds);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,status-cmds-52", "lcd-kit,status-cmds-state",
			       &hybrid_info->status_cmds_52);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,status-cmds-54", "lcd-kit,status-cmds-state",
			       &hybrid_info->status_cmds_54);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,status-cmds-0d", "lcd-kit,status-cmds-state",
			       &hybrid_info->status_cmds_0d);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,status-cmds-0e", "lcd-kit,status-cmds-state",
			       &hybrid_info->status_cmds_0e);
	lcd_kit_parse_dcs_cmds(np, "lcd-kit,status-cmds-0f", "lcd-kit,status-cmds-state",
			       &hybrid_info->status_cmds_0f);

	lcd_kit_parse_dcs_cmds(np, "lcd-kit,ddic-id-cmds", "lcd-kit,ddic-id-cmds-state",
			       &hybrid_info->ddic_id_cmds);
	of_property_read_u8(np, "lcd-kit,vxn-ddic-id", &hybrid_info->vxn_id);
}
