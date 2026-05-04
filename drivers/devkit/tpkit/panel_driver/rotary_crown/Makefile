obj-$(CONFIG_HUAWEI_ROTARY_CROWN) += rotary_crown.o
ifeq ($(TARGET_VERSION_MODE),factory)
EXTRA_CFLAGS += -DRC_FACTORY_MODE
endif

ccflags-y += \
	-I$(srctree)/drivers/devkit/tpkit/3_0/hybrid \
	-I$(srctree)/drivers/devkit/tpkit/3_0 \
	-I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/qcom/common/include
