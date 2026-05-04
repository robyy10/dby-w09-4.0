#
# Makefile for the FTS touchscreen driver.
#


obj-$(CONFIG_ST_TS_KIT_3_0) += ftsCompensation.o ftsCore.o ftsError.o ftsFrame.o ftsIO.o ftsTest.o ftsTime.o ftsTool.o ftsFlash.o ftsGesture.o

ifeq ($(CONFIG_HUAWEI_DEVKIT_HISI_3_0),y)
EXTRA_CFLAGS += -I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/common/include
EXTRA_CFLAGS += -I$(srctree)/drivers/devkit/tpkit/3_0
else
EXTRA_CFLAGS += -Idrivers/devkit/lcdkit/lcdkit3.0/kernel/common/include
EXTRA_CFLAGS += -Idrivers/devkit/tpkit/3_0
endif