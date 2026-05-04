/*
* ufs_wb_dynamic_switch.c
* ufs write boster feature
*
* copyright (c) huawei technologies co., ltd. 2021. All rights reserved.
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
*/
#include "ufs_wb_dynamic_switch_kona.h"
#include <linux/platform_device.h>
#include <asm/unaligned.h>
#include "ufshcd.h"
#include "ufs_quirks.h"

#define ufs_attribute(_name, _uname)                                           \
	static ssize_t _name##_show(struct device *dev,                \
			struct device_attribute *attr, char *buf)      \
	{                                                              \
		struct Scsi_Host *shost = class_to_shost(dev);         \
		struct ufs_hba *hba = shost_priv(shost);               \
		u32 value;                                             \
		pm_runtime_get_sync(hba->dev);			       \
		if (ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR,\
				QUERY_ATTR_IDN##_uname, 0, 0, &value)) \
			return -EINVAL;				       \
		pm_runtime_put_sync(hba->dev);			       \
		return sprintf(buf, "0x%08X\n", value);		       \
	}                                                              \
	static DEVICE_ATTR_RO(_name)

ufs_attribute(wb_flush_status, _WB_FLUSH_STATUS);
ufs_attribute(wb_available_buffer_size, _AVAIL_WB_BUFF_SIZE);
ufs_attribute(wb_buffer_lifetime_est, _WB_BUFF_LIFE_TIME_EST);
ufs_attribute(wb_current_buffer_size, _CURR_WB_BUFF_SIZE);

#define ufs_flag(_name, _uname)                                                \
	static ssize_t _name##_show(struct device *dev,                \
		struct device_attribute *attr, char *buf)	       \
	{                                                              \
		bool flag;                                             \
		struct Scsi_Host *shost = class_to_shost(dev);         \
		struct ufs_hba *hba = shost_priv(shost);               \
		pm_runtime_get_sync(hba->dev);			       \
		if (ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG,\
			QUERY_FLAG_IDN##_uname, &flag))                \
			return -EINVAL;				       \
		pm_runtime_put_sync(hba->dev);			       \
		return sprintf(buf, "%s\n",                            \
			flag ? "true" : "false");		       \
	}                                                              \
	static DEVICE_ATTR_RO(_name)

ufs_flag(wb_en, _WB_EN);
ufs_flag(wb_flush_en, _WB_BUFF_FLUSH_EN);
ufs_flag(wb_flush_during_hibern8, _WB_BUFF_FLUSH_DURING_HIBERN8);

static ssize_t wb_permanent_disable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len);
static ssize_t wb_permanent_disable_show(struct device *dev,
	struct device_attribute *attr, char *buf);
DEVICE_ATTR_RW(wb_permanent_disable);

struct device_attribute *ufs_host_attrs[] = {
	&dev_attr_wb_en,
	&dev_attr_wb_flush_en,
	&dev_attr_wb_flush_during_hibern8,
	&dev_attr_wb_flush_status,
	&dev_attr_wb_available_buffer_size,
	&dev_attr_wb_buffer_lifetime_est,
	&dev_attr_wb_current_buffer_size,
	&dev_attr_wb_permanent_disable,
	NULL
};

static int ufshcd_wb_exception_event_ctrl(struct ufs_hba *hba, bool enable)
{
	int ret;
	unsigned long flags;
	u32 val;

	/*
	 * Some device can alloc a new write buffer by toggle write booster.
	 * We apply a exception handler func for and only for those devices.
	 *
	 * If new strategy was to implement, the hba->wb_work would be the good
	 * place to put new function to, ufshcd_exception_event_handler would
	 * handle them the same way.
	 */
	if (hba->dev_info.w_manufacturer_id != UFS_VENDOR_TOSHIBA)
		return 0;

	if (enable)
		val = hba->ee_ctrl_mask | MASK_EE_WRITEBOOSTER_EVENT;
	else
		val = hba->ee_ctrl_mask & ~MASK_EE_WRITEBOOSTER_EVENT;
	val &= MASK_EE_STATUS;
	ret = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
			QUERY_ATTR_IDN_EE_CONTROL, 0, 0, &val);
	if (ret) {
		dev_err(hba->dev,
			"%s: failed to enable wb exception event %d\n",
			__func__, ret);
		return ret;
	}

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->ee_ctrl_mask &= ~MASK_EE_WRITEBOOSTER_EVENT;
	hba->ufs_wb->wb_exception_enabled = enable;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	dev_info(hba->dev, "%s: wb exception event %s\n",
			__func__, enable ? "enable" : "disable");

	return ret;
}

static inline struct ufs_hba *ufshcd_dev_to_hba(struct device *dev)
{
	if (!dev)
		return NULL;

	return shost_priv(class_to_shost(dev));
}

static inline int ufshcd_disable_wb_exception_event(struct ufs_hba *hba)
{
	return ufshcd_wb_exception_event_ctrl(hba, false);
}

int ufshcd_enable_wb_exception_event(struct ufs_hba *hba)
{
	return ufshcd_wb_exception_event_ctrl(hba, true);
}

bool ufshcd_wb_is_permanent_disabled(struct ufs_hba *hba)
{
	unsigned long flags;
	bool wb_permanent_disabled = false;

	spin_lock_irqsave(hba->host->host_lock, flags);
	wb_permanent_disabled = hba->ufs_wb->wb_permanent_disabled;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return wb_permanent_disabled;
}

static void ufshcd_set_wb_permanent_disable(struct ufs_hba *hba)
{
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->ufs_wb->wb_permanent_disabled = true;
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

static bool ufshcd_get_wb_work_sched(struct ufs_hba *hba)
{
	unsigned long flags;
	bool wb_work_sched = false;

	spin_lock_irqsave(hba->host->host_lock, flags);
	wb_work_sched = hba->ufs_wb->wb_work_sched;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return wb_work_sched;
}

static ssize_t wb_permanent_disable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	struct ufs_hba *hba = ufshcd_dev_to_hba(dev);

	if (!hba)
		return -EFAULT;

	pm_runtime_get_sync(hba->dev);
	if (!ufshcd_wb_sup(hba)) {
		dev_info(hba->dev, "%s wb not support\n", __func__);
		goto out;
	}

	ret = ufshcd_disable_wb_exception_event(hba);
	if (ret) {
		dev_err(hba->dev, "%s wb disable exception fail\n", __func__);
		goto out;
	}

	/* when WRITEBOOSTER_EVENT is processing, caller will retry. */
	if (ufshcd_get_wb_work_sched(hba)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = ufshcd_wb_ctrl(hba, false);
	if (ret) {
		dev_err(hba->dev, "%s wb disable fail\n", __func__);

		goto out;
	}
	ufshcd_set_wb_permanent_disable(hba);
	dev_info(hba->dev, "%s wb permanent disable succ\n", __func__);
out:
	pm_runtime_put_sync(hba->dev);

	return !ret ? len : ret;
}

static ssize_t wb_permanent_disable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	bool flag = false;
	struct ufs_hba *hba = ufshcd_dev_to_hba(dev);

	if (!hba)
		return snprintf(buf, PAGE_SIZE,
			"wb permanent disable hba is null\n");

	flag = ufshcd_wb_is_permanent_disabled(hba);

	return snprintf(buf, PAGE_SIZE, "wb permanent disable : %s\n",
		flag ? "true" : "false");
}


static void ufshcd_wb_toggle_fn(struct work_struct *work)
{
	int ret;
	u32 current_buffer;
	u32 available;
	unsigned long flags;
	struct ufs_hba *hba;
	struct delayed_work *dwork = to_delayed_work(work);

	hba = ((struct ufs_wb_switch_info *)(container_of(dwork,
		struct ufs_wb_switch_info, wb_toggle_work)))->hba;

	dev_info(hba->dev, "%s: WB for Txxx toggle func", __func__);

	if (hba->dev_info.w_manufacturer_id != UFS_VENDOR_TOSHIBA)
		return;

	pm_runtime_get_sync(hba->dev);

	if (ufshcd_wb_ctrl(hba, false) || ufshcd_wb_ctrl(hba, true))
		goto out;
	/*
	 * Error handler doesn't need here. If we failed to check buffer
	 * size, following io would cause a new exception handler which
	 * let us try here again.
	 */
	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
				      QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE, 0,
				      0, &current_buffer);
	if (ret)
		goto out;

	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
				      QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE, 0,
				      0, &available);
	if (ret)
		goto out;

	/*
	 * Maybe, there are not enough space to alloc a new write buffer, if so,
	 * disable exception to avoid en endless notification.
	 */
	if (current_buffer < hba->ufs_wb->wb_shared_alloc_units ||
		available <= WB_T_BUFFER_THRESHOLD) {
		ret = ufshcd_wb_exception_event_ctrl(hba, false);
		if (!ret) {
			spin_lock_irqsave(hba->host->host_lock, flags);
			hba->ufs_wb->wb_exception_enabled = false;
			spin_unlock_irqrestore(hba->host->host_lock, flags);
		}
	}

out:
	pm_runtime_put_sync(hba->dev);
	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->ufs_wb->wb_work_sched = false;
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

void ufshcd_wb_toggle_cancel(struct ufs_hba *hba)
{
	if (!hba)
		return;
	if (!hba->ufs_wb)
		return;

	flush_delayed_work(&hba->ufs_wb->wb_toggle_work);
}

void ufshcd_wb_exception_event_handler(struct ufs_hba *hba)
{
	unsigned long flags;

	if (!hba)
		return;

	spin_lock_irqsave(hba->host->host_lock, flags);

	if (!hba->ufs_wb->wb_exception_enabled)
		goto out;

	if (hba->ufs_wb->wb_work_sched)
		goto out;
	hba->ufs_wb->wb_work_sched = true;

	schedule_delayed_work(&hba->ufs_wb->wb_toggle_work,
			msecs_to_jiffies(0));

out:
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

static int ufs_get_wb_device_desc(struct ufs_hba *hba)
{
	int err;
	u8 *desc_buf = NULL;
	size_t buff_len;

	buff_len = max_t(size_t, hba->desc_size.dev_desc,
			QUERY_DESC_MAX_SIZE + 1);
	desc_buf = kmalloc(buff_len, GFP_KERNEL);
	if (!desc_buf)
		return -ENOMEM;

	err = ufshcd_read_device_desc(hba, desc_buf, hba->desc_size.dev_desc);
	if (err) {
		dev_err(hba->dev, "%s: Failed reading Device Desc. err = %d\n",
			__func__, err);
		goto out;
	}

	hba->ufs_wb->wb_shared_alloc_units = get_unaligned_be32(
		desc_buf + DEVICE_DESC_PARAM_WB_SHARED_ALLOC_UNITS);

out:
	kfree(desc_buf);
	return err;
}

int ufs_dynamic_switching_wb_config(struct ufs_hba *hba)
{
	int ret;
	if (!hba)
		return -1;

	if (!ufshcd_wb_sup(hba)) {
		dev_info(hba->dev, "%s wb not support\n", __func__);
		return -1;
	}
	if (hba->ufs_wb == NULL) {
		hba->ufs_wb = kzalloc(sizeof(struct ufs_wb_switch_info),
						GFP_KERNEL);
		if (!hba->ufs_wb) {
			dev_err(hba->dev, "%s: alloc ufs_wb_switch_info failed!\n",
					__func__);
			return -ENOMEM;
			}
	}

	hba->ufs_wb->hba = hba;
	if (ufshcd_wb_is_permanent_disabled(hba))
		goto out;
	INIT_DELAYED_WORK(&hba->ufs_wb->wb_toggle_work, ufshcd_wb_toggle_fn);
	ret = ufs_get_wb_device_desc(hba);
	if (ret) {
		dev_err(hba->dev, "%s: alloc ufs_wb_switch_info failed!\n", __func__);
		goto out;
	}
	return ret;
out:
	kfree(hba->ufs_wb);
	hba->ufs_wb = NULL;
	return ret;
}

void ufs_dynamic_switching_wb_remove(struct ufs_hba *hba)
{
	if (!hba)
		return;
	if (!hba->ufs_wb)
		return;

	kfree(hba->ufs_wb);
	hba->ufs_wb = NULL;
}
