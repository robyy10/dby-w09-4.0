#
# Makefile for DUBAI.
#

ifeq (y, $(findstring y,$(CONFIG_HUAWEI_DUBAI) $(CONFIG_POWER_DUBAI)))
obj-y += \
	dubai.o \
	dubai_wakeup_stats.o
endif

ifeq (y, $(findstring y,$(CONFIG_POWER_DUBAI_GPU_STATS) $(CONFIG_HUAWEI_DUBAI_GPU_STATS)))
obj-y += dubai_gpu_freq_stats.o
endif
obj-$(CONFIG_HUAWEI_DUBAI_DDR_STATS) += dubai_ddr_stats.o
