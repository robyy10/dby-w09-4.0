#include <linux/printk.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#define DTS_COMP_NAME "sensor,underlight"
#define DEV_NAME "sensor_underlight"
#define MMAP_DEVICE_NAME "display_sharemem_map"
#define SHARE_MEMORY_SIZE (128 * 128 * 5)

#define ACTION_READ_READY 1

#define GET_WRITE_READY_FLAG	_IOR('x', 0x01, uint8_t)
#define SET_WRITE_READY_FLAG	_IOW('x', 0x02, uint8_t)
#define GET_READ_READY_FLAG		_IOR('x', 0x03, uint8_t)
#define SET_READ_READY_FLAG		_IOW('x', 0x04, uint8_t)
#define GET_RECT_INFO			_IOR('x', 0x05, struct underlight_rect_info)
#define SET_RECT_INFO			_IOW('x', 0x06, struct underlight_rect_info)
#define GET_RUN_PERIOD			_IOR('x', 0x07, long)
#define SET_RUN_PERIOD			_IOW('x', 0x08, long)

#define UNDERLIGHT_ERR(msg, ...) \
	do { \
		pr_err("[sensors_underlight E]:%s: "msg, __func__, ## __VA_ARGS__); \
	} while (0)

#define UNDERLIGHT_WARNING(msg, ...) \
	do { \
		pr_warning("[sensors_underlight W]:%s: "msg, __func__, ## __VA_ARGS__); \
	} while (0)

#define UNDERLIGHT_NOTICE(msg, ...) \
	do { \
		pr_info("[sensors_underlight N]:%s: "msg, __func__, ## __VA_ARGS__); \
	} while (0)

#define UNDERLIGHT_INFO(msg, ...) \
	do { \
		pr_info("[sensors_underlight I]:%s: "msg, __func__, ## __VA_ARGS__); \
	} while (0)

#define UNDERLIGHT_DEBUG(msg, ...) \
	do { \
		pr_info("[sensors_underlight D]:%s: "msg, __func__, ## __VA_ARGS__); \
	} while (0)

struct underlight_rect_info {
	int top_left_x;
	int top_left_y;
	int rect_h;
	int rect_w;
};

struct underlight_member {
	volatile uint8_t write_ready;
	volatile uint8_t read_ready;
	uint8_t* share_mem_virt;
	phys_addr_t share_mem_phy;
	struct mutex ready_flag_lock;
	long samping_period;
	struct underlight_rect_info rect_info;
	wait_queue_head_t wait_queue;
};