#ifndef __LINUX_F_KEY_H_INCLUDED
#define __LINUX_F_KEY_H_INCLUDED
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define MAX_NAME_LEN 50
#define GPIO_VAL_HIGH 1
#define GPIO_VAL_LOW 0
#define PUSH_DOWN 1
#define PUSH_UP 0

struct f_key_platform_data {
	char name[MAX_NAME_LEN];
	int gpio;
	int input_code;
	int irq;
	int val;
	struct mutex lock;
	struct work_struct event_work;
	struct input_dev *input_dev;
};

#endif