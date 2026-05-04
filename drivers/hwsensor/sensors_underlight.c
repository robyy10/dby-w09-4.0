#include <linux/miscdevice.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>
#include <linux/memory.h>
#include <linux/freezer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <securec.h>

#include "sensors_underlight.h"
#include "apsensor_channel/ap_sensor_route.h"
#include "apsensor_channel/ap_sensor.h"
#include <linux/mtd/hw_nve_interface.h>

static struct underlight_member u_menber = {
	.share_mem_virt = NULL,
	.share_mem_phy = 0,
	.write_ready = 1,
	.read_ready = 0,
	.samping_period = 0,
};

static void read_ready_report()
{
	struct ap_sensor_manager_cmd_t fg_cmd;
	fg_cmd.sensor_type = SENSOR_TYPE_UNDERLIGHT;
	fg_cmd.action = ACTION_READ_READY;
	ap_sensor_route_write((char*)&fg_cmd, sizeof(struct ap_sensor_manager_cmd_t));
}

static int share_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long start = 0;
	unsigned long size = 0;

	if (vma == NULL) {
		UNDERLIGHT_ERR("vma is null!\n");
		return -1;
	}
	if (u_menber.share_mem_virt == NULL || u_menber.share_mem_phy == 0) {
		UNDERLIGHT_ERR("share memory is not alloced!\n");
		return -1;
	}
	start = (unsigned long)vma->vm_start;
	size = (unsigned long)(vma->vm_end - vma->vm_start);
	if (size > SHARE_MEMORY_SIZE)
		return -1;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	UNDERLIGHT_INFO("map size = 0x%lx\n", size);
	if (remap_pfn_range(vma, start, __phys_to_pfn(u_menber.share_mem_phy), size, vma->vm_page_prot)) {
		UNDERLIGHT_ERR("remap_pfn_range error!\n");
		return -1;
	}
	return 0;
}

static int dev_open(struct inode * inode, struct file * file)
{
	return 0;
}

static void ready_flag_switch()
{
	mutex_lock(&u_menber.ready_flag_lock);
	u_menber.write_ready ^= 1;
	u_menber.read_ready ^= 1;
	mutex_unlock(&u_menber.ready_flag_lock);
}

static long ioctl_get_write_ready_flag(unsigned long argp)
{
	long ret = 0;

	ret = copy_to_user((uint8_t *)argp, (const uint8_t *)&u_menber.write_ready, sizeof(uint8_t));
	if (ret)
		UNDERLIGHT_ERR("failed to copy result of ioctl to user space.\n");
	return ret;
}

static long ioctl_set_write_ready_flag(unsigned long argp)
{
	ready_flag_switch();
	return 0;
}

static long ioctl_get_read_ready_flag(unsigned long argp)
{
	long ret = 0;

	ret = copy_to_user((uint8_t *)argp, (const uint8_t *)&u_menber.read_ready, sizeof(uint8_t));
	if (ret)
		UNDERLIGHT_ERR("failed to copy result of ioctl to user space.\n");
	return ret;
}

static long ioctl_set_read_ready_flag(unsigned long argp)
{

	ready_flag_switch();
	read_ready_report();
	return 0;
}

static int read_rect_info_from_nv(int nv_number, int nv_size, char *temp)
{
	int ret;
	struct hw_nve_info_user nv_user_info;

	if (!temp) {
		UNDERLIGHT_ERR("para err\n");
		return -1;
	}
	memset_s(&nv_user_info, sizeof(nv_user_info), 0, sizeof(nv_user_info));
	nv_user_info.nv_operation = 1;
	nv_user_info.nv_number = nv_number;
	nv_user_info.valid_size = nv_size;
	strncpy_s(nv_user_info.nv_name, (sizeof(nv_user_info.nv_name) - 1),
		 "ALSTP1", (sizeof(nv_user_info.nv_name) - 1));
	ret = hw_nve_direct_access(&nv_user_info);
	if (ret != 0) {
		UNDERLIGHT_ERR("color read nv %d error %d\n", nv_number, ret);
		return -1;
	}
	memcpy_s(temp, 4 * sizeof(int), nv_user_info.nv_data, 4 * sizeof(int));
	return 0;
}

#define ALS_TP_CALIDATA_NV1_NUM   403
#define ALS_TP_CALIDATA_NV1_SIZE  104

static long ioctl_get_rect_info(unsigned long argp)
{
	int ret;
	unsigned int rect_info[4] = {0};
	struct device_node *underlight_node = NULL;

	if (read_rect_info_from_nv(ALS_TP_CALIDATA_NV1_NUM, ALS_TP_CALIDATA_NV1_SIZE, (char *)&rect_info[0]) < 0)
		UNDERLIGHT_ERR("read rect info fail, set default val\n");

	if (rect_info[0] <= 0) {
		underlight_node = of_find_compatible_node(NULL, NULL, DTS_COMP_NAME);
		if (underlight_node == NULL)
			UNDERLIGHT_ERR("find node:[%s] fail!\n", DTS_COMP_NAME);
		ret = of_property_read_u32_array(underlight_node, "rect_info", rect_info, 4);
		if (ret)
			UNDERLIGHT_ERR("dts get rect_info fail!\n");
	}
	u_menber.rect_info.top_left_x = (short)rect_info[0];
	u_menber.rect_info.top_left_y = (short)rect_info[1];
	u_menber.rect_info.rect_h = (short)rect_info[2];
	u_menber.rect_info.rect_w = (short)rect_info[3];
	ret = copy_to_user((struct underlight_rect_info *)argp,
			 &u_menber.rect_info, sizeof(struct underlight_rect_info));
	if (ret)
		UNDERLIGHT_ERR("failed to copy result of ioctl to user space.\n");
	return ret;
}

static long ioctl_set_rect_info(unsigned long argp)
{
	int ret;
	ret = copy_from_user(&u_menber.rect_info, (struct underlight_rect_info *)argp,
				 sizeof(struct underlight_rect_info));
	if (ret)
		UNDERLIGHT_ERR("failed to copy result of ioctl from user space.\n");
	return ret;
}

static long ioctl_get_run_period(unsigned long argp)
{
	int ret;
	long user_samping_period;

	ret = copy_from_user(&user_samping_period, (int *)argp, sizeof(long));
	if (ret) {
		UNDERLIGHT_ERR("failed to copy result of ioctl from user space.\n");
	} else {
		wait_event_freezable_timeout(u_menber.wait_queue,
			user_samping_period != u_menber.samping_period,
			msecs_to_jiffies(100000));
		ret = copy_to_user((int *)argp, &u_menber.samping_period, sizeof(long));
		if (ret)
			UNDERLIGHT_ERR("failed to copy result of ioctl to user space.\n");
	}
	return ret;
}

static long ioctl_set_run_period(unsigned long argp)
{
	int ret;
	ret = copy_from_user(&u_menber.samping_period, (int *)argp, sizeof(long));
	if (ret)
		UNDERLIGHT_ERR("failed to copy result of ioctl from user space.\n");
	else
		wake_up_interruptible(&u_menber.wait_queue);
	return ret;
}

static long ioctl_handler(struct file * file, unsigned int cmd, unsigned long argp)
{
	long ret = -EINVAL;

	if ((char *)argp == NULL || file == NULL) {
		UNDERLIGHT_ERR("NULL pointer of arg or cmd\n");
		goto err_out;
	}
	switch (cmd) {
	case GET_WRITE_READY_FLAG:
		ret = ioctl_get_write_ready_flag(argp);
		break;
	case SET_WRITE_READY_FLAG:
		ret = ioctl_set_write_ready_flag(argp);
		break;
	case GET_READ_READY_FLAG:
		ret = ioctl_get_read_ready_flag(argp);
		break;
	case SET_READ_READY_FLAG:
		ret = ioctl_set_read_ready_flag(argp);
		break;
	case GET_RECT_INFO:
		ret = ioctl_get_rect_info(argp);
		break;
	case SET_RECT_INFO:
		ret = ioctl_set_rect_info(argp);
		break;
	case GET_RUN_PERIOD:
		ret = ioctl_get_run_period(argp);
		break;
	case SET_RUN_PERIOD:
		ret = ioctl_set_run_period(argp);
		break;
	default:
		UNDERLIGHT_ERR("unknown cmd\n");
		ret = -ENOSYS;
		break;
	}
	return ret;
err_out:
	return ret;
}

static struct file_operations mmap_dev_fops = {
	.owner = THIS_MODULE,
	.mmap = share_mmap,
	.open = dev_open,
	.unlocked_ioctl = ioctl_handler,
};

static struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MMAP_DEVICE_NAME,
	.fops = &mmap_dev_fops,
};

static int mmap_dev_init(void)
{
	int ret = 0;

	UNDERLIGHT_INFO("\n");
	ret = misc_register(&misc);
	if (ret) {
		UNDERLIGHT_ERR("misc_register ret = %d \n", ret);
		return ret;
	}
	return ret;
}

static int sensors_underlight_probe(struct platform_device *pdev)
{
	UNDERLIGHT_INFO("\n");
	if (pdev == NULL) {
		UNDERLIGHT_ERR("pdev is NULL");
		return -EINVAL;
	}
	u_menber.share_mem_virt = (void *)dma_alloc_coherent(&pdev->dev,
				SHARE_MEMORY_SIZE, &u_menber.share_mem_phy, GFP_KERNEL);
	if (u_menber.share_mem_virt == NULL || u_menber.share_mem_phy == 0) {
		UNDERLIGHT_ERR("dma_alloc_coherent error! ");
		return -EINVAL;
	}
	mutex_init(&u_menber.ready_flag_lock);
	mmap_dev_init();
	return 0;
}

static int sensors_underlight_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id driver_match_table[] = {
	{
		.compatible = DTS_COMP_NAME,
	},
	{},
};

MODULE_DEVICE_TABLE(of, driver_match_table);

static struct platform_driver underlight_driver = {
	.probe = sensors_underlight_probe,
	.remove = sensors_underlight_remove,
	.suspend = NULL,
	.resume = NULL,
	.shutdown = NULL,
	.driver = {
		.name = DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(driver_match_table),
	},
};

static int __init sensors_underlight_init(void)
{
	int ret = 0;
	UNDERLIGHT_INFO("\n");
	ret = platform_driver_register(&underlight_driver);
	if (ret) {
		UNDERLIGHT_ERR("platform_driver_register failed, error=%d!\n", ret);
		return ret;
	}
	init_waitqueue_head(&u_menber.wait_queue);
	return ret;
}

static void __exit sensors_underlight_exit(void) {}

MODULE_AUTHOR("huawei");
MODULE_DESCRIPTION("underlight for sensor");
MODULE_LICENSE("GPL");

module_init(sensors_underlight_init);
module_exit(sensors_underlight_exit);