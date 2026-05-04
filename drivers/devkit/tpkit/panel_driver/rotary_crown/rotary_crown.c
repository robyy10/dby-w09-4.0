/*
 * rotary_crown.c
 * Rotary Crown driver
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#include "rotary_crown.h"

#include <huawei_platform/log/log_jank.h>
#include <linux/pinctrl/pinctrl-state.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/mtd/hw_nve_interface.h>

#include "securec.h"
#ifdef CONFIG_LCD_KIT_HYBRID
#include "huawei_ts_kit_hybrid_core.h"
#endif

static struct timer_list g_crown_check_timer;
static struct work_struct g_crown_check_work;
static struct mutex g_work_lock;
/* global values */
struct rotary_crown_data *g_rotary_crown_pdata;

int rc_i2c_read(uint8_t *reg_addr, uint16_t reg_len, uint8_t *buf, uint16_t len)
{
	int count = 0;
	int ret;
	int msg_len;
	struct i2c_msg *msg_addr = NULL;
	struct i2c_msg xfer[ROTARY_I2C_MSG_LEN];
	struct i2c_client *client = g_rotary_crown_pdata->client;

	if (g_rotary_crown_pdata->is_one_byte) {
		/* Read data */
		xfer[ROTARY_I2C_MSG_INDEX0].addr = client->addr;
		xfer[ROTARY_I2C_MSG_INDEX0].flags = I2C_M_RD;
		xfer[ROTARY_I2C_MSG_INDEX0].len = len;
		xfer[ROTARY_I2C_MSG_INDEX0].buf = buf;
		do {
			ret = i2c_transfer(client->adapter, xfer,
				ROTARY_I2C_MSG_INDEX1);
			if (ret == ROTARY_I2C_MSG_INDEX1)
				return NO_ERR;

			msleep(I2C_WAIT_TIME);
		} while (++count < I2C_RW_TRIES);
	} else {
		/* register addr */
		xfer[ROTARY_I2C_MSG_INDEX0].addr = client->addr;
		xfer[ROTARY_I2C_MSG_INDEX0].flags = 0;
		xfer[ROTARY_I2C_MSG_INDEX0].len = reg_len;
		xfer[ROTARY_I2C_MSG_INDEX0].buf = reg_addr;

		/* Read data */
		xfer[ROTARY_I2C_MSG_INDEX1].addr = client->addr;
		xfer[ROTARY_I2C_MSG_INDEX1].flags = I2C_M_RD;
		xfer[ROTARY_I2C_MSG_INDEX1].len = len;
		xfer[ROTARY_I2C_MSG_INDEX1].buf = buf;

		if (reg_len > 0) {
			msg_len = ROTARY_I2C_MSG_LEN;
			msg_addr = &xfer[ROTARY_I2C_MSG_INDEX0];
		} else {
			msg_len = ROTARY_I2C_MSG_INDEX1;
			msg_addr = &xfer[ROTARY_I2C_MSG_INDEX1];
		}

		do {
			ret = i2c_transfer(client->adapter, msg_addr, msg_len);
			if (ret == msg_len)
				return NO_ERR;

			msleep(I2C_WAIT_TIME);
		} while (++count < I2C_RW_TRIES);
	}

	rc_err("%s: failed\n", __func__);
	return -EIO;
}

int rc_i2c_write(uint8_t *buf, uint16_t length)
{
	int count = 0;
	int ret;

	do {
		ret = i2c_master_send(g_rotary_crown_pdata->client,
			(const char *) buf, length);
		if (ret == length)
			return NO_ERR;

		msleep(I2C_WAIT_TIME);
	} while (++count < I2C_RW_TRIES);

	rc_err("%s: failed", __func__);
	return -EIO;
}

static int rotary_crown_write_reg(uint8_t addr, uint8_t value)
{
	int ret;
	uint8_t cmd[ROTARY_REG_WRITE_LEN] = {0};

	// write reg
	cmd[ROTARY_I2C_MSG_INDEX0] = addr;
	cmd[ROTARY_I2C_MSG_INDEX1] = value;

	// soft reset do not give ack back
	if ((addr == PA_DEVICE_CONTROL) && (value == PA_CHIP_RESET)) {
		ret = i2c_master_send(g_rotary_crown_pdata->client,
			(const char *)cmd, ROTARY_REG_WRITE_LEN);
		rc_info("%s:soft reset send", __func__);
		return 0;
	}

	ret = rc_i2c_write(cmd, ROTARY_REG_WRITE_LEN);
	if (ret != NO_ERR)
		rc_err("set value(0x%02x) to reg(0x%02x) failed", value, addr);

	return ret;
}

static int rotary_crown_parse_dts(void)
{
	struct device_node *np = NULL;
	int ret;
	uint32_t value = 0;
	uint32_t target_x = 0;
	uint32_t nv_number = 0;

	rc_debug("%s enter\n", __func__);
	if (!g_rotary_crown_pdata) {
		rc_err("%s: parameters invalid !\n", __func__);
		return -EINVAL;
	}
	np = g_rotary_crown_pdata->dev->of_node;
	g_rotary_crown_pdata->irq_gpio = of_get_named_gpio(np, "irq_gpio", 0);
	if (!gpio_is_valid(g_rotary_crown_pdata->irq_gpio)) {
		rc_err("%s:irq_gpio is not valid !\n", __func__);
		return -EINVAL;
	}

	rc_debug("%s,irq_gpio=%d\n", __func__, g_rotary_crown_pdata->irq_gpio);

	ret = of_property_read_u32(np, ROTARY_CROWN_TARGET_X, &target_x);
	if (ret) {
		g_rotary_crown_pdata->target_x = ROTARY_STD_DELTA_VAL;
		rc_err("get targe_x failed\n");
	} else {
		g_rotary_crown_pdata->target_x = (uint16_t)target_x;
	}
	rc_debug("%s,target_x=%d\n", __func__, g_rotary_crown_pdata->target_x);

	ret = of_property_read_u32(np, ROTARY_CROWN_NV_NUMBER, &nv_number);
	if (ret) {
		rc_err("get nv_number failed\n");
		return -EINVAL;
	}
	g_rotary_crown_pdata->nv_number = nv_number;
	rc_debug("%s,nv_number=%d\n", __func__, g_rotary_crown_pdata->nv_number);

	// i2c one byte or not
	ret = of_property_read_u32(np, ROTARY_I2C_TYPE, &value);
	if (ret) {
		g_rotary_crown_pdata->is_one_byte = false;
		rc_err("get is_one_btye failed\n");
	} else {
		g_rotary_crown_pdata->is_one_byte = value ? true : false;
	}
	rc_debug("%s,one byte=%d\n", __func__, g_rotary_crown_pdata->is_one_byte);

	return 0;
}

static int rotary_crown_pinctrl_init(void)
{
	int ret;

	if (!g_rotary_crown_pdata) {
		rc_err("%s: parameters invalid !\n", __func__);
		return -EINVAL;
	}

	g_rotary_crown_pdata->rc_pinctrl =
		devm_pinctrl_get(g_rotary_crown_pdata->dev);
	if (IS_ERR(g_rotary_crown_pdata->rc_pinctrl)) {
		rc_err("%s:failed to devm pinctrl get\n", __func__);
		ret = -EINVAL;
		return ret;
	}

	do {
		g_rotary_crown_pdata->pinctrl_state_active =
			pinctrl_lookup_state(g_rotary_crown_pdata->rc_pinctrl,
				PINCTRL_STATE_DEFAULT);
		if (IS_ERR(g_rotary_crown_pdata->pinctrl_state_active)) {
			rc_err("%s:failed to lookup state active\n",
				__func__);
			ret = -EINVAL;
			break;
		}

		g_rotary_crown_pdata->pinctrl_state_suspend =
			pinctrl_lookup_state(g_rotary_crown_pdata->rc_pinctrl,
				PINCTRL_STATE_IDLE);
		if (IS_ERR(g_rotary_crown_pdata->pinctrl_state_suspend)) {
			rc_err("%s:failed to lookup suspend state\n",
				__func__);
			ret = -EINVAL;
			break;
		}

		/*
		 * Pinctrl handle is optional. If pinctrl handle
		 * is found, let pins to be configured in active state.
		 * If not found continue further without error.
		 */
		ret = pinctrl_select_state(g_rotary_crown_pdata->rc_pinctrl,
			g_rotary_crown_pdata->pinctrl_state_active);
		if (ret) {
			rc_err("%s:failed to set pinctr active\n",
				__func__);
			ret = -EINVAL;
			break;
		}

		return 0;
	} while (0);

	devm_pinctrl_put(g_rotary_crown_pdata->rc_pinctrl);
	return ret;
}

static int rotary_crown_check_chip_id(void)
{
	uint8_t addr = PA_PRODUCT_ID1;
	uint8_t value = 0;
	int ret;

	rc_debug("%s:called\n", __func__);
	if (!g_rotary_crown_pdata) {
		rc_err("%s: parameters invalid !\n", __func__);
		return -EINVAL;
	}

	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN,
		&value, ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR) {
		rc_err("%s: call rc_i2c_read failed", __func__);
		return ret;
	}

	rc_debug("%s: chip id(0x%02x)\n", __func__, value);
	if (value != PA_DEFAULT_CHIP_ID1)
		return -EINVAL;

	return NO_ERR;
}

static void rotary_crown_clear_interrupt(void)
{
	int ret;
	uint8_t addr;
	uint8_t value;

	addr = PA_MOTION_STATUS;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN, &value,
		ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR)
		rc_err("%s: read motion status failed", __func__);

	addr = PA_DELTA_X_LOW;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN, &value,
		ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR)
		rc_err("%s: read low delta x failed", __func__);

	addr = PA_DELTA_Y_LOW;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN, &value,
		ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR)
		rc_err("%s: read low delta y failed", __func__);

	addr = PA_DELTA_XY_HIGH;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN, &value,
		ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR)
		rc_err("%s: read delta xy_high failed", __func__);
}

static int rotary_crown_chip_init(uint8_t x_res)
{
	int ret;

	// switch to bank 0
	ret = rotary_crown_write_reg(PA_PAGE_BANK_CONTROL, PA_PAGE_BANK_0);
	if (ret != NO_ERR)
		return ret;

	// software reset
	ret = rotary_crown_write_reg(PA_DEVICE_CONTROL, PA_CHIP_RESET);
	if (ret != NO_ERR)
		rc_err("%s: enter software reset failed", __func__);
	msleep(ROTARY_DELAY_1_MS);

	// exit software reset
	ret = rotary_crown_write_reg(PA_DEVICE_CONTROL, PA_CHIP_EXIT_RESET);
	if (ret != NO_ERR)
		return ret;

	ret = rotary_crown_write_reg(PA_OPERATION_MODE, PA_ENTER_SLEEP1_MODE);
	if (ret != NO_ERR)
		return ret;

	// disable write protect
	ret = rotary_crown_write_reg(PA_WRITE_PROTECT, PA_DISABLE_WR_PROTECT);
	if (ret != NO_ERR)
		return ret;

	// set X-axis resolution
	ret = rotary_crown_write_reg(PA_RESOLUTION_X_CONFIG, x_res);
	if (ret != NO_ERR)
		return ret;

	// set Y-axis resolution
	ret = rotary_crown_write_reg(PA_RESOLUTION_Y_CONFIG, PA_Y_RES_VALUE);
	if (ret != NO_ERR)
		return ret;

	// set 12-bit X/Y data format
	ret = rotary_crown_write_reg(PA_DELTA_DATA_FORMAT, PA_USE_12_BIT_FMT);
	if (ret != NO_ERR)
		return ret;

	// set power saving
	ret = rotary_crown_write_reg(PA_LOW_VOL_CONFIG, PA_SET_POWER_SAVING);
	if (ret != NO_ERR)
		return ret;

	// trace performance config
	ret = rotary_crown_write_reg(PA_TRACE_PERF_CONFIG3, PA_PERF_CFG3_VAL);
	if (ret != NO_ERR)
		return ret;

	ret = rotary_crown_write_reg(PA_TRACE_PERF_CONFIG1, PA_PERF_CFG1_VAL);
	if (ret != NO_ERR)
		return ret;

	ret = rotary_crown_write_reg(PA_TRACE_PERF_CONFIG2, PA_PERF_CFG2_VAL);
	if (ret != NO_ERR)
		return ret;

	// enable write protect
	ret = rotary_crown_write_reg(PA_WRITE_PROTECT, PA_ENABLE_WR_PROTECT);
	if (ret != NO_ERR)
		return ret;

	// clear interrupt
	rotary_crown_clear_interrupt();
	return NO_ERR;
}

static irqreturn_t rotary_crown_irq_handler(int irq, void *dev_id)
{
	struct rotary_crown_data *rc = dev_id;

	rc_debug("%s: enter", __func__);
	if (!rc)
		return IRQ_HANDLED;

	if (!rc->is_init_ok)
		return IRQ_HANDLED;

	disable_irq_nosync(rc->client->irq);
	if (!schedule_work(&rc->work)) {
		rc_err("%s: failed to schedule work\n", __func__);
		enable_irq(rc->client->irq);
	}
	g_rotary_crown_pdata->irq_cnt++;

	return IRQ_HANDLED;
}

static int rotary_crown_read_motion(int16_t *dx)
{
	int ret;
	uint8_t addr;
	int16_t delta_x_low = 0;
	int16_t delta_x_high;
	int16_t delta_xy_high = 0;
	uint8_t value;

	if (dx == NULL)
		return -EINVAL;

	// read delta x low 8-bit
	addr = PA_DELTA_X_LOW;
	value = 0;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN,
		&value, ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR) {
		rc_err("%s: read low delta x failed", __func__);
		return ret;
	}
	rc_debug("%s: low delta x reg 0x%02x", __func__, value);
	delta_x_low |= value;

	// read delta y low 8-bit
	addr = PA_DELTA_Y_LOW;
	value = 0;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN,
		&value, ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR) {
		rc_err("%s: read low delta y failed", __func__);
		return ret;
	}
	rc_debug("%s: low delta y reg 0x%02x", __func__, value);

	// read delta xy high 4-bit
	addr = PA_DELTA_XY_HIGH;
	value = 0;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN,
		&value, ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR) {
		rc_err("%s: read high delta xy failed", __func__);
		return ret;
	}
	rc_debug("%s: high delta reg 0x%02x", __func__, value);
	delta_xy_high |= value;

	delta_x_high = (delta_xy_high << ROTARY_HIGH_DELTA_SHIFT)
		& ROTARY_HIGH_DELTA_MASK;
	if (delta_x_high & ROTARY_HIGH_DELTA_SIGH_MASK)
		delta_x_high |= ROTARY_HIGH_DELTA_SIGH;

	*dx = delta_x_high | delta_x_low;
	return NO_ERR;
}

static int rotary_crown_data_read(int16_t *data)
{
	int ret;
	uint8_t motion_valid = 0;
	uint8_t addr = PA_MOTION_STATUS;

	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN,
		&motion_valid, ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR) {
		rc_err("%s: read motion status failed", __func__);
		return ret;
	}
	motion_valid &= ROTARY_MOTION_VALID_MASK;
	if (motion_valid) {
		ret = rotary_crown_read_motion(data);
		if (ret != NO_ERR) {
			rc_err("%s: read motion failed", __func__);
			return ret;
		}
	}

	return NO_ERR;
}

static void rotary_crown_data_report(struct rotary_crown_data *rc)
{
	int ret;
	uint8_t no_motion_cnt = 0;
	uint8_t valid_cnt = 0;
	int16_t rdval[ROTARY_SMOOTH_DATA_NUM] = {0};
	int16_t report_x;
	int32_t temp_x = 0;
	uint8_t i;

	while (no_motion_cnt <= ROTARY_READ_MOTION_TIMES) {
		if (rc->is_suspend) {
			rc_info("%s:in suspend, break", __func__);
			break;
		}
		if (!gpio_get_value(g_rotary_crown_pdata->irq_gpio)) {
			no_motion_cnt = 0;
			ret = rotary_crown_data_read(&rdval[valid_cnt++]);
			if (ret != NO_ERR) {
				rc_err("%s: read motion data failed", __func__);
				break;
			}
		} else {
			no_motion_cnt++;
			rdval[valid_cnt++] = 0;
		}
		// smooth data process
		for (i = 0; i < ROTARY_SMOOTH_DATA_NUM; i++)
			temp_x += rdval[i];
		report_x = temp_x / ROTARY_SMOOTH_DATA_NUM;
		if (valid_cnt >= ROTARY_SMOOTH_DATA_NUM)
			valid_cnt %= ROTARY_SMOOTH_DATA_NUM;
		temp_x = 0;
		// 0/1/2/3/4 is read data index
		rc_debug("%s: read motion data %d %d %d %d %d report data %d", __func__,
			rdval[0], rdval[1], rdval[2], rdval[3], rdval[4], report_x);
		input_report_rel(rc->input_dev, REL_WHEEL, report_x);
		input_sync(rc->input_dev);
		msleep(ROTARY_READ_DELTA_DELAY);
	}
}

static void rotary_crown_work_func(struct work_struct *work)
{
	struct rotary_crown_data *rc = container_of(work,
		struct rotary_crown_data, work);

	if (rc->is_suspend) {
		rc_info("%s:in suspend, return", __func__);
		goto out;
	}

#ifdef CONFIG_LCD_KIT_HYBRID
	if (!hybrid_i2c_check()) {
		rc_info("%s:i2c is not at AP, exit", __func__);
		goto out;
	}
#endif
	rc_debug("%s: enter", __func__);
	mutex_lock(&g_work_lock);
	rc->is_working = true;
	// report delta x
	rotary_crown_data_report(rc);
	enable_irq(rc->client->irq);
	rc_info("%s:crown irq enable", __func__);
	if (!gpio_get_value(g_rotary_crown_pdata->irq_gpio)) {
		// clear interrupt
		rotary_crown_clear_interrupt();
		rc_info("%s: irq abnormal clear", __func__);
	}
	rc->is_working = false;
	mutex_unlock(&g_work_lock);
	rc_debug("%s: exit", __func__);
	return;
out:
	/* disabled in irq handler */
	enable_irq(rc->client->irq);
}

static int rotary_crown_run_mode_set(bool enable)
{
	int ret;

	rc_debug("%s: enter, enable = %d", __func__, enable);
	if (enable) {
		// exit power down mode
		ret = rotary_crown_write_reg(PA_DEVICE_CONTROL, PA_CHIP_EXIT_RESET);
		if (ret != NO_ERR) {
			rc_err("%s: exit power down mode failed, value = %d",
				__func__, PA_CHIP_EXIT_RESET);
			return ret;
		}

		ret = rotary_crown_write_reg(PA_OPERATION_MODE, PA_ENTER_SLEEP1_MODE);
		if (ret != NO_ERR) {
			rc_err("%s: set operation mode failed, value = %d",
				__func__, PA_ENTER_SLEEP1_MODE);
			return ret;
		}
	} else {
		// enter power down mode
		ret = rotary_crown_write_reg(PA_DEVICE_CONTROL, PA_CHIP_POWER_DOWN);
		if (ret != NO_ERR) {
			rc_err("%s: enter power down mode failed, value = %d",
				__func__, PA_CHIP_POWER_DOWN);
			return ret;
		}
	}

	return NO_ERR;
}

#ifdef CONFIG_LCD_KIT_HYBRID
static int rotary_crown_suspend(struct i2c_client *client)
{
	int ret;
	struct rotary_crown_data *rc = i2c_get_clientdata(client);

	rc_info("%s: enter", __func__);
	if (rc->is_suspend) {
		rc_info("%s:already in suspend, skip", __func__);
		return 0;
	}

	rc->is_suspend = true;
	// disable interrupt
	disable_irq_nosync(rc->client->irq);
	if (!IS_ERR(rc->rc_pinctrl)) {
		ret = pinctrl_select_state(rc->rc_pinctrl,
			rc->pinctrl_state_suspend);
		if (ret < 0)
			rc_err("%s:failed to set suspend state", __func__);
	}

	if (hybrid_i2c_check()) {
		rc_info("%s:i2c is at AP, set mode", __func__);
		ret = rotary_crown_run_mode_set(false);
		if (ret)
			rc_err("%s: enter sleep 2 mode failed", __func__);
	}
	rc_info("%s: exit\n", __func__);

	return 0;
}

static int rotary_crown_resume(struct i2c_client *client)
{
	int ret;
	struct rotary_crown_data *rc = i2c_get_clientdata(client);

	rc_info("%s enter", __func__);
	if (!rc->is_suspend) {
		rc_info("%s:already in resume, skip", __func__);
		return 0;
	}

	if (!hybrid_i2c_check()) {
		rc_info("%s:i2c is not at AP, exit", __func__);
		enable_irq(rc->client->irq);
		return 0;
	}

	if (!IS_ERR(rc->rc_pinctrl)) {
		ret = pinctrl_select_state(rc->rc_pinctrl,
			rc->pinctrl_state_active);
		if (ret < 0)
			rc_err("%s:failed to set active state", __func__);
	}
	// chip write reg
	ret = rotary_crown_chip_init(g_rotary_crown_pdata->res_x_nv);
	if (ret)
		rc_err("%s: chip init failed", __func__);

	// enable irq
	enable_irq(rc->client->irq);
	rc->is_suspend = false;
	rc_info("%s exit", __func__);

	return 0;
}

static int rotary_crown_hybrid_suspend(void)
{
	if (!g_rotary_crown_pdata)
		return -EFAULT;

	return rotary_crown_suspend(g_rotary_crown_pdata->client);
}

static int rotary_crown_hybrid_resume(void)
{
	if (!g_rotary_crown_pdata)
		return -EFAULT;

	return rotary_crown_resume(g_rotary_crown_pdata->client);
}

struct ts_hybrid_chip_ops ts_hybrid_crown_ops = {
	.hybrid_suspend = rotary_crown_hybrid_suspend,
	.hybrid_resume = rotary_crown_hybrid_resume,
	.hybrid_idle = rotary_crown_hybrid_suspend,
};
#endif

static int rotary_crown_irq_init(void)
{
	int ret;

	// interrupt setting
	do {
		ret = gpio_request(g_rotary_crown_pdata->irq_gpio,
			"RC_INT_IRQ");
		if (ret < 0) {
			rc_err("Failed to request GPIO:%d, ERRNO:%d",
				(s32) g_rotary_crown_pdata->irq_gpio, ret);
			ret = -ENODEV;
			break;
		}

		ret = gpio_direction_input(g_rotary_crown_pdata->irq_gpio);
		if (ret < 0) {
			rc_err("Failed to set GPIO direction:%d, ERRNO:%d",
				(s32) g_rotary_crown_pdata->irq_gpio, ret);
			ret = -ENODEV;
			break;
		}

		g_rotary_crown_pdata->client->irq =
			gpio_to_irq(g_rotary_crown_pdata->irq_gpio);
		rc_debug("irq num:%d\n", g_rotary_crown_pdata->client->irq);
		INIT_WORK(&(g_rotary_crown_pdata->work),
			rotary_crown_work_func);

		// IRQF_TRIGGER_NONE 0x00000000
		// IRQF_TRIGGER_RISING   0x00000001
		// IRQF_TRIGGER_FALLING  0x00000002
		// IRQF_TRIGGER_HIGH 0x00000004
		// IRQF_TRIGGER_LOW  0x00000008
		ret = request_irq(g_rotary_crown_pdata->client->irq,
			rotary_crown_irq_handler,
			IRQF_TRIGGER_FALLING,
			g_rotary_crown_pdata->client->name,
			g_rotary_crown_pdata);
		if (ret != 0) {
			rc_err("request_irq failed");
			break;
		}
		disable_irq_nosync(g_rotary_crown_pdata->client->irq);
		rc_info("%s:crown irq disable", __func__);

		return NO_ERR;
	} while (0);

	if (gpio_is_valid(g_rotary_crown_pdata->irq_gpio))
		gpio_free(g_rotary_crown_pdata->irq_gpio);

	return ret;
}

static int rotary_crown_input_init(void)
{
	int ret;

	// interrupt setting
	do {
		// pin control setting
		ret = rotary_crown_pinctrl_init();
		if (ret != NO_ERR) {
			rc_err("failed to init pin control");
			ret = -ENODEV;
			break;
		}

		// input setting
		g_rotary_crown_pdata->input_dev = input_allocate_device();
		if (g_rotary_crown_pdata->input_dev == NULL) {
			rc_err("Can not allocate memory for input device");
			ret = -ENOMEM;
			break;
		}

		g_rotary_crown_pdata->input_dev->name =
			ROTARY_CROWN_INPUT_DEV_NAME;
		g_rotary_crown_pdata->input_dev->dev.parent =
			&(g_rotary_crown_pdata->client->dev);
		g_rotary_crown_pdata->input_dev->id.bustype = BUS_I2C;
		input_set_capability(g_rotary_crown_pdata->input_dev,
			EV_REL, REL_WHEEL);
		ret = input_register_device(g_rotary_crown_pdata->input_dev);
		if (ret < 0) {
			rc_err("Can not register input device");
			ret = -ENODEV;
			break;
		}

		return NO_ERR;
	} while (0);

	if (g_rotary_crown_pdata->input_dev) {
		input_unregister_device(g_rotary_crown_pdata->input_dev);
		input_free_device(g_rotary_crown_pdata->input_dev);
		g_rotary_crown_pdata->input_dev = NULL;
	}

	return ret;
}

static ssize_t rotary_crown_self_test_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

#ifdef CONFIG_LCD_KIT_HYBRID
	if (!hybrid_i2c_check()) {
		rc_info("%s:i2c is not at AP, exit", __func__);
		return -EINVAL;
	}
#endif
	ret = rotary_crown_check_chip_id();
	if (ret != NO_ERR)
		ret = snprintf_s(buf, ROTARY_STR_LEN, ROTARY_STR_LEN - 1,
			"%s\n", "Fail");
	else
		ret = snprintf_s(buf, ROTARY_STR_LEN, ROTARY_STR_LEN - 1,
			"%s\n", "Success");
	if (ret < 0)
		rc_err("%s: snprintf_s err\n", __func__);

	return ret;
}

static int rotary_crown_status_report_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char *irq_gpio_sta = NULL;
	int offset = 0;

	rc_debug("%s: called\n", __func__);
	if (!dev || !buf) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}
	if (!gpio_get_value(g_rotary_crown_pdata->irq_gpio))
		irq_gpio_sta = "low";
	else
		irq_gpio_sta = "high";

	offset += snprintf_s(buf + offset, ROTARY_STR_LEN - offset,
			ROTARY_STR_LEN - offset - 1,
			"irq_gpio:%s, init_status:%u, work_status:%u, power_status:%u\n",
			irq_gpio_sta,
			g_rotary_crown_pdata->is_init_ok,
			g_rotary_crown_pdata->is_working,
			g_rotary_crown_pdata->is_suspend);
	if (offset < 0)
		rc_err("%s: snprintf_s err\n", __func__);

	return offset;
}

static int rotary_crown_read_res_from_nv(uint8_t *res)
{
	int ret;
	struct hw_nve_info_user user_info;

	if (memset_s(&user_info, sizeof(user_info), 0, sizeof(user_info)) != EOK) {
		rc_err("%s: memset error\n", __func__);
		return -EINVAL;
	}
	user_info.nv_operation = NV_READ_OPERATION;
	user_info.nv_number = g_rotary_crown_pdata->nv_number;
	user_info.valid_size = RC_RES_NV_DATA_SIZE;
	if (strncpy_s(user_info.nv_name, sizeof(user_info.nv_name), "RCRES",
		RC_NV_NAME_LEN) != EOK)
		rc_err("%s: strncpy error\n", __func__);
	user_info.nv_name[sizeof(user_info.nv_name) - 1] = '\0';
	ret = hw_nve_direct_access(&user_info);
	if (ret) {
		rc_err("hw_nve_direct_access read error %d\n", ret);
		return -EINVAL;
	}
	*res = user_info.nv_data[0];
	rc_debug("%s: 0x%02x\n", __func__, *res);
	return 0;
}

#ifdef RC_FACTORY_MODE
static int rotary_crown_write_res_to_nv(uint8_t res)
{
	int ret;
	struct hw_nve_info_user user_info;

	if (memset_s(&user_info, sizeof(user_info), 0, sizeof(user_info)) != EOK) {
		rc_err("%s: memset error\n", __func__);
		return -EINVAL;
	}
	user_info.nv_operation = NV_WRITE_OPERATION;
	user_info.nv_number = g_rotary_crown_pdata->nv_number;
	user_info.valid_size = RC_RES_NV_DATA_SIZE;
	if (strncpy_s(user_info.nv_name, sizeof(user_info.nv_name), "RCRES",
		RC_NV_NAME_LEN) != EOK)
		rc_err("%s: strncpy error\n", __func__);
	user_info.nv_name[sizeof(user_info.nv_name) - 1] = '\0';
	user_info.nv_data[0] = res;
	ret = hw_nve_direct_access(&user_info);
	if (ret) {
		rc_err("hw_nve_direct_access write error %d\n", ret);
		return -EINVAL;
	}
	rc_debug("%s: 0x%02x\n", __func__, res);
	return 0;
}

static ssize_t rotary_crown_calibrate_nv_store(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned long val = 0;
	int ret;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

	ret = kstrtoul(buf, 0, &val);
	if (ret) {
		rc_err("%s: call strtoul failed :%d\n", __func__, ret);
		return -EINVAL;
	}

	rc_debug("%s: val(%lu)\n", __func__, val);
	ret = rotary_crown_write_res_to_nv((uint8_t)val);
	if (ret) {
		rc_err("%s: failed to write nv\n", __func__);
		return ret;
	}

	return count;
}

static ssize_t rotary_crown_calibrate_nv_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;
	uint8_t res = 0;
	int offset = 0;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

	ret = rotary_crown_read_res_from_nv(&res);
	if (ret) {
		rc_err("%s: failed to read nv\n", __func__);
		return ret;
	}

	rc_debug("%s: x resoluttion(0x%02x)\n", __func__, res);
	offset += snprintf_s(buf + offset, ROTARY_STR_LEN - offset,
		ROTARY_STR_LEN - offset - 1, "%u\n", res);
	if (offset < 0)
		rc_err("%s: snprintf_s err\n", __func__);

	return offset;
}

static ssize_t rotary_crown_irq_cnt_store(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}
	g_rotary_crown_pdata->irq_cnt = 0;
	// clear interrupt
	rotary_crown_clear_interrupt();

	return count;
}

static ssize_t rotary_crown_irq_cnt_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int offset = 0;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

	rc_debug("%s: irq cnt %d\n", __func__, g_rotary_crown_pdata->irq_cnt);
	offset += snprintf_s(buf + offset, ROTARY_STR_LEN - offset,
		ROTARY_STR_LEN - offset - 1, "%d\n", g_rotary_crown_pdata->irq_cnt);
	if (offset < 0)
		rc_err("%s: snprintf_s err\n", __func__);

	return offset;
}

static int rotary_crown_set_res(uint8_t res)
{
	int ret;

	rc_debug("%s:called\n", __func__);
	// disable write protect
	ret = rotary_crown_write_reg(PA_WRITE_PROTECT, PA_DISABLE_WR_PROTECT);
	if (ret != NO_ERR)
		return ret;

	// set X-axis resolution
	ret = rotary_crown_write_reg(PA_RESOLUTION_X_CONFIG, res);
	if (ret != NO_ERR)
		return ret;

	// set Y-axis resolution
	ret = rotary_crown_write_reg(PA_RESOLUTION_Y_CONFIG, PA_Y_RES_VALUE);
	if (ret != NO_ERR)
		return ret;

	// enable write protect
	ret = rotary_crown_write_reg(PA_WRITE_PROTECT, PA_ENABLE_WR_PROTECT);
	if (ret != NO_ERR)
		return ret;

	return NO_ERR;
}

static int rc_calibrate_enter(void)
{
	int ret;

	rc_debug("%s: enter\n", __func__);
	if (g_rotary_crown_pdata == NULL)
		return -ENOMEM;

	g_rotary_crown_pdata->is_calib_test = true;
	ret = rotary_crown_set_res(PA_X_RES_VALUE);
	if (ret) {
		rc_err("%s: set chip resolution failed", __func__);
		return ret;
	}

	disable_irq_nosync(g_rotary_crown_pdata->client->irq);

	// clear interrupt
	rotary_crown_clear_interrupt();

	return NO_ERR;
}

static int rc_calibrate_test_exit(void)
{
	rc_debug("%s: enter\n", __func__);
	// clear interrupt
	rotary_crown_clear_interrupt();

	enable_irq(g_rotary_crown_pdata->client->irq);
	g_rotary_crown_pdata->is_calib_test = false;

	return NO_ERR;
}

static int rc_test_enter(void)
{
	int ret;
	uint8_t res = 0;
	rc_debug("%s: enter\n", __func__);
	if (g_rotary_crown_pdata == NULL)
		return -ENOMEM;

	g_rotary_crown_pdata->is_calib_test = true;
	disable_irq_nosync(g_rotary_crown_pdata->client->irq);

	// read NV
	ret = rotary_crown_read_res_from_nv(&res);
	if (ret) {
		rc_err("%s: failed to read nv\n", __func__);
		return ret;
	}

	// write to reg
	ret = rotary_crown_set_res(res);
	if (ret) {
		rc_err("%s: set chip resolution failed", __func__);
		return ret;
	}

	// clear interrupt
	rotary_crown_clear_interrupt();

	return NO_ERR;
}

static ssize_t rotary_crown_calibrate_mode_store(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned long val = 0;
	int ret;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

	ret = kstrtoul(buf, 0, &val);
	if (ret) {
		rc_err("%s: call strtoul failed :%d\n", __func__, ret);
		return -EINVAL;
	}

	rc_debug("%s: val(%lu)\n", __func__, val);
	if (val == 0)
		ret = rc_calibrate_enter();
	else
		ret = rc_calibrate_test_exit();

	if (ret) {
		rc_err("%s: calibrate control failed :%d\n", __func__, ret);
		return ret;
	}

	return count;
}

static ssize_t rotary_crown_test_mode_store(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned long val = 0;
	int ret;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

	ret = kstrtoul(buf, 0, &val);
	if (ret) {
		rc_err("%s: call strtoul failed :%d\n", __func__, ret);
		return -EINVAL;
	}

	rc_debug("%s: val(%lu)\n", __func__, val);
	if (val == 0)
		ret = rc_test_enter();
	else
		ret = rc_calibrate_test_exit();

	if (ret) {
		rc_err("%s: nv test control failed :%d\n", __func__, ret);
		return ret;
	}

	return count;
}

static ssize_t rotary_crown_delta_motion_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;
	uint8_t addr;
	uint8_t value = 0;
	int offset = 0;
	int16_t delta_x = 0;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

	addr = PA_MOTION_STATUS;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN, &value,
		ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR) {
		rc_err("%s: read motion status failed", __func__);
		return ret;
	}

	// read delta x
	ret = rotary_crown_read_motion(&delta_x);
	if (ret != NO_ERR) {
		rc_err("%s: read x resolution failed", __func__);
		return ret;
	}
	rc_debug("%s: delta x(0x%04x)\n", __func__, delta_x);
	offset += snprintf_s(buf + offset, ROTARY_STR_LEN - offset,
		ROTARY_STR_LEN - offset - 1, "%d\n", delta_x);
	if (offset < 0)
		rc_err("%s: snprintf_s err\n", __func__);

	return offset;
}

static ssize_t rotary_crown_resolution_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;
	uint8_t addr;
	uint8_t res = 0;
	const uint8_t res_default = PA_X_RES_VALUE;
	int offset = 0;
	uint16_t target_x = g_rotary_crown_pdata->target_x;

	rc_debug("%s: called\n", __func__);
	if ((dev == NULL) || (buf == NULL)) {
		rc_err("%s: parameter is null\n", __func__);
		return -EINVAL;
	}

	addr = PA_RESOLUTION_X_CONFIG;
	ret = rc_i2c_read(&addr, ROTARY_REG_ADDR_LEN, &res, ROTARY_REG_VALUE_LEN);
	if (ret != NO_ERR) {
		rc_err("%s: read x resolution failed", __func__);
		return ret;
	}

	offset += snprintf_s(buf + offset, ROTARY_STR_LEN - offset,
		ROTARY_STR_LEN - offset - 1, "%u,%u,%u\n", res, res_default, target_x);
	if (offset < 0)
		rc_err("%s: snprintf_s err\n", __func__);

	return offset;
}
#endif // RC_FACTORY_MODE

// device file node create
static DEVICE_ATTR(rotary_crown_self_test, 0444, rotary_crown_self_test_show, NULL);
static DEVICE_ATTR(rotary_crown_status_report, 0444, rotary_crown_status_report_show, NULL);
#ifdef RC_FACTORY_MODE
static DEVICE_ATTR(rotary_crown_calibrate_nv, 0664, rotary_crown_calibrate_nv_show,
	rotary_crown_calibrate_nv_store);
static DEVICE_ATTR(rotary_crown_irq_cnt, 0664, rotary_crown_irq_cnt_show,
	rotary_crown_irq_cnt_store);
static DEVICE_ATTR(rotary_crown_calibrate_mode, 0664, NULL,
	rotary_crown_calibrate_mode_store);
static DEVICE_ATTR(rotary_crown_test_mode, 0664, NULL,
	rotary_crown_test_mode_store);
static DEVICE_ATTR(rotary_crown_delta_motion, 0444,
	rotary_crown_delta_motion_show, NULL);
static DEVICE_ATTR(rotary_crown_resolution, 0444,
	rotary_crown_resolution_show, NULL);
#endif // RC_FACTORY_MODE

static struct attribute *rc_attributes[] = {
	&dev_attr_rotary_crown_self_test.attr,
	&dev_attr_rotary_crown_status_report.attr,
#ifdef RC_FACTORY_MODE
	&dev_attr_rotary_crown_calibrate_nv.attr,
	&dev_attr_rotary_crown_irq_cnt.attr,
	&dev_attr_rotary_crown_calibrate_mode.attr,
	&dev_attr_rotary_crown_test_mode.attr,
	&dev_attr_rotary_crown_delta_motion.attr,
	&dev_attr_rotary_crown_resolution.attr,
#endif // RC_FACTORY_MODE
	NULL
};

static struct attribute_group g_rc_attr_group = {
	.attrs = rc_attributes,
};

static int rotary_crown_remove(struct i2c_client *client)
{
	int ret;
	struct rotary_crown_data *rc = i2c_get_clientdata(client);

	if (!g_rotary_crown_pdata)
		return -EFAULT;

	ret = rotary_crown_run_mode_set(false);
	if (ret)
		rc_err("%s: enter sleep 2 mode failed", __func__);
	mutex_destroy(&g_work_lock);
	if (!IS_ERR(g_rotary_crown_pdata->rc_pinctrl)) {
		ret = pinctrl_select_state(g_rotary_crown_pdata->rc_pinctrl,
			g_rotary_crown_pdata->pinctrl_state_suspend);
		if (ret)
			rc_err("%s:failed to set release state", __func__);
		devm_pinctrl_put(g_rotary_crown_pdata->rc_pinctrl);
	}
	g_rotary_crown_pdata->rc_pinctrl = NULL;
	i2c_set_clientdata(client, NULL);
	free_irq(client->irq, rc);
	if (rc->input_dev)
		input_unregister_device(rc->input_dev);

	if (g_rotary_crown_pdata->sys_file_ok)
		sysfs_remove_group(&client->dev.kobj, &g_rc_attr_group);

	if (g_rotary_crown_pdata != NULL) {
		devm_kfree(&client->dev, g_rotary_crown_pdata);
		g_rotary_crown_pdata = NULL;
	}

	return 0;
}

#ifdef CONFIG_LCD_KIT_HYBRID
static void rotary_crown_pm_init(void)
{
	int ret;

	if (!g_rotary_crown_pdata)
		return;

	ret = ts_hybrid_ops_register(&ts_hybrid_crown_ops);
	if (ret)
		rc_err("Unable to register ts hybrid ops: %d\n", ret);
}
#endif

static int rotary_crown_sysfs_init(void)
{
	int ret;

	rc_debug("%s:called\n", __func__);
	do {
		g_rotary_crown_pdata->sys_file_ok = false;
		ret = sysfs_create_group(&g_rotary_crown_pdata->dev->kobj,
			&g_rc_attr_group);
		if (ret) {
			rc_err("%s: failed to create device attr, ret=%d\n",
				__func__, ret);
			return -ENODEV;
		}

		g_rotary_crown_pdata->sys_file_ok = true;
		ret = sysfs_create_link(NULL, &g_rotary_crown_pdata->dev->kobj,
			"rotarycrown");
		if (ret < 0) {
			pr_err("%s: Failed to sysfs_create_link, %d\n",
				__func__, ret);
			return -ENODEV;
		}
	} while (0);

	return NO_ERR;
}

static int rotary_crown_detect(void)
{
	int ret;
#ifdef CONFIG_LCD_KIT_HYBRID
	if (!hybrid_i2c_check()) {
		rc_info("%s:i2c is not at AP, exit", __func__);
		return -1;
	}
#endif
	ret = rotary_crown_check_chip_id();
	if (ret)
		return ret;

	if (g_rotary_crown_pdata->nv_status_count < MAX_READ_NV_TIMES) {
		g_rotary_crown_pdata->nv_status_count++;
		ret = rotary_crown_read_res_from_nv(&g_rotary_crown_pdata->res_x_nv);
		if (ret) {
			g_rotary_crown_pdata->res_x_nv = PA_X_RES_VALUE;
			return ret;
		} else {
			if ((g_rotary_crown_pdata->res_x_nv <= ROTARY_MIN_RES_X) ||
				(g_rotary_crown_pdata->res_x_nv >= ROTARY_MAX_RES_X)) {
				rc_err("nv value is not invalid %d\n", g_rotary_crown_pdata->res_x_nv);
				g_rotary_crown_pdata->res_x_nv = PA_X_RES_VALUE;
			}
		}
	} else {
		rc_err("%s: failed to read nv, use default\n", __func__);
	}
	rc_info("%s: nv value is %d 0x%08x\n", __func__,
		g_rotary_crown_pdata->nv_status_count, g_rotary_crown_pdata->res_x_nv);
	ret = rotary_crown_chip_init(g_rotary_crown_pdata->res_x_nv);
	if (ret)
		return ret;

#ifdef CONFIG_LCD_KIT_HYBRID
	rotary_crown_pm_init();
#endif

	enable_irq(g_rotary_crown_pdata->client->irq);
	rc_info("%s:crown irq enable", __func__);
	g_rotary_crown_pdata->is_init_ok = true;
	return 0;
}

static void crown_check_work_func(struct work_struct *work)
{
	int ret;
	uint check_time;
	unsigned long expires;

	if (g_rotary_crown_pdata->is_init_ok) {
		mutex_lock(&g_work_lock);
		if (!g_rotary_crown_pdata->is_suspend && !g_rotary_crown_pdata->is_working &&
			!g_rotary_crown_pdata->is_calib_test) {
			ret = rotary_crown_check_chip_id();
			if (ret)
				rc_err("%s: failed to check chip id\n", __func__);

			if (!gpio_get_value(g_rotary_crown_pdata->irq_gpio)) {
				// clear interrupt
				rotary_crown_clear_interrupt();
				rc_warn("%s: irq abnormal clear", __func__);
			}
		}
		check_time = CROWN_CHECK_TIME;
		mutex_unlock(&g_work_lock);
	} else {
		ret = rotary_crown_detect();
		if (ret)
			rc_err("%s: failed to recovery\n", __func__);
		check_time = CROWN_RECOVERY_TIME;
	}
	expires = jiffies + (check_time * HZ);
	mod_timer(&g_crown_check_timer, expires);
}

static void crown_check_timer_func(struct timer_list *timer)
{
	if (!schedule_work(&g_crown_check_work))
		rc_err("%s: failed to schedule work\n", __func__);
}

static void rotary_crown_check_timer_init(void)
{
	mutex_init(&g_work_lock);
	INIT_WORK(&g_crown_check_work, crown_check_work_func);
	timer_setup(&g_crown_check_timer, crown_check_timer_func, 0);
}

static void rotary_crown_recovery_start(void)
{
	rc_err("%s: init failed, need recovery", __func__);
	g_crown_check_timer.expires = jiffies + (CROWN_RECOVERY_TIME * HZ);
	add_timer(&g_crown_check_timer);
}

static void rotary_crown_check_start(void)
{
	rc_info("%s: init success, need check", __func__);
	g_crown_check_timer.expires = jiffies + (CROWN_CHECK_TIME * HZ);
	add_timer(&g_crown_check_timer);
}

static int rotary_crown_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	int size;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	g_rotary_crown_pdata = NULL;
	size = sizeof(*g_rotary_crown_pdata);
	g_rotary_crown_pdata = devm_kzalloc(&client->dev, size, GFP_KERNEL);
	if (!g_rotary_crown_pdata)
		return -ENOMEM;

	ret = memset_s(g_rotary_crown_pdata, size, 0, size);
	if (ret)
		goto err_free;

	i2c_set_clientdata(client, g_rotary_crown_pdata);
	g_rotary_crown_pdata->client = client;
	g_rotary_crown_pdata->dev = &client->dev;
	ret = rotary_crown_parse_dts();
	if (ret)
		goto err_free;

	// irq setting
	ret = rotary_crown_irq_init();
	if (ret)
		goto err_free;

	// input and pin control setting
	ret = rotary_crown_input_init();
	if (ret)
		goto err_free;

	// sysfs node init
	ret = rotary_crown_sysfs_init();
	if (ret)
		goto err_free;

	rotary_crown_check_timer_init();

	ret = rotary_crown_detect();
	if (ret)
		rotary_crown_recovery_start();
	else
		rotary_crown_check_start();

	return 0;

err_free:
	devm_kfree(&client->dev, g_rotary_crown_pdata);
	return ret;
}

static const struct i2c_device_id rotary_crown_id[] = {
	{ROTARY_CROWN_DRV_NAME, 0},
	{}
};

static const struct of_device_id rotary_crown_match_table[] = {
	{.compatible = "pixart,pat9126ja", },
	{},
};

static struct i2c_driver rotary_crown_driver = {
	.probe      = rotary_crown_probe,
	.remove     = rotary_crown_remove,
	.id_table   = rotary_crown_id,
	.driver = {
		.name   = ROTARY_CROWN_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = rotary_crown_match_table,
	},
};

static int __init rotary_crown_module_init(void)
{
	rc_debug("%s: enter", __func__);
	return i2c_add_driver(&rotary_crown_driver);
}

static void __exit rotary_crown_module_exit(void)
{
	rc_debug("%s: enter", __func__);
	i2c_del_driver(&rotary_crown_driver);
}

late_initcall(rotary_crown_module_init);
module_exit(rotary_crown_module_exit);
MODULE_AUTHOR("Huawei Device Company");
MODULE_DESCRIPTION("Huawei RotaryCrown Driver");
MODULE_LICENSE("GPL");
