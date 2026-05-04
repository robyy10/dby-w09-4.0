obj-$(CONFIG_LCD_KIT_QCOM)	+= lcd_kit_adapt.o
obj-$(CONFIG_LCD_KIT_QCOM)	+= lcd_kit_sysfs_qcom.o
ifneq ($(TARGET_BUILD_VARIANT),user)
obj-$(CONFIG_LCD_KIT_QCOM)	+= lcd_kit_debug_qcom.o
endif
ifneq ($(TARGET_BUILD_VARIANT),user)
EXTRA_CFLAGS += -DLCD_KIT_DEBUG_ENABLE
endif
ifeq ($(TARGET_VERSION_MODE),factory)
EXTRA_CFLAGS += -DLCD_FACTORY_MODE
endif
ifeq ($(TARGET_PRODUCT),bengal)
$(info "TARGET_PRODUCT = bengal_1")
EXTRA_CFLAGS += -DQCOM_PLATFORM_6225
endif
EXTRA_CFLAGS += -I$(srctree)/drivers/staging/android \
		-I$(srctree)/drivers/atfdriver \
		-I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/qcom/common/include \
		-I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/qcom/include \
		-I$(srctree)/include/drm \
		-I$(srctree)/drivers/gpu/drm \
		-I$(srctree)/techpack/display/msm/dsi \
		-I$(srctree)/techpack/display/msm \
		-I$(srctree)/techpack/display/sde \
		-I$(srctree)/techpack/display/include \
		-I$(srctree)/techpack/display/include/uapi/display \
		-I$(srctree)/drivers/clk/qcom \
		-I$(srctree)/drivers/base

ifeq ($(CONFIG_MATTING_ALGO_TASK),y)
EXTRA_CFLAGS += -I$(srctree)/../../vendor/huawei/extra/kernel/drivers/sensor/ext_inputhub/matting_algo_task
endif
