#
# Makefile for the touchscreen drivers.
#

obj-$(CONFIG_ST_TS_KIT_3_0) += fts.o
obj-$(CONFIG_ST_TS_KIT_3_0) += fts_proc.o
obj-$(CONFIG_ST_TS_KIT_3_0) += fts_lib/

ifeq ($(CONFIG_HUAWEI_DEVKIT_HISI_3_0),y)
EXTRA_CFLAGS += -I$(srctree)/drivers/hisi/tzdriver
EXTRA_CFLAGS += -I$(srctree)/drivers/tzdriver
EXTRA_CFLAGS += -I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/common/include
EXTRA_CFLAGS += -I$(srctree)/drivers/devkit/tpkit/3_0
else
EXTRA_CFLAGS += -Idrivers/hisi/tzdriver
EXTRA_CFLAGS += -Idrivers/tzdriver
EXTRA_CFLAGS += -Idrivers/devkit/lcdkit/lcdkit3.0/kernel/common/include
EXTRA_CFLAGS += -Idrivers/devkit/tpkit/3_0
endif