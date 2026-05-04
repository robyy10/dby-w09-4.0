#goodix gtx8
obj-$(CONFIG_GOODIX_GTX8_TS_KIT) += gtx8_ts.o
gtx8_ts-objs += gtx8.o gtx8_update.o gtx8_tools.o gtx8_debug.o

ifeq ($(CONFIG_HUAWEI_DEVKIT_HISI_3_0),y)
EXTRA_CFLAGS += -I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/common/include
EXTRA_CFLAGS += -I$(srctree)/drivers/hisi/tzdriver
EXTRA_CFLAGS += -I$(srctree)/drivers/tzdriver
EXTRA_CFLAGS += -I$(srctree)/drivers/devkit/tpkit/3_0
else
EXTRA_CFLAGS += -Idrivers/devkit/lcdkit/lcdkit3.0/kernel/common/include
EXTRA_CFLAGS += -Idrivers/hisi/tzdriver
EXTRA_CFLAGS += -Idrivers/tzdriver
EXTRA_CFLAGS += -Idrivers/devkit/tpkit/3_0
endif