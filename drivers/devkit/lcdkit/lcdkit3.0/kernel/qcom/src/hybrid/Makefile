obj-$(CONFIG_LCD_KIT_HYBRID) += lcd_kit_hybrid_core.o
obj-$(CONFIG_LCD_KIT_HYBRID) += lcd_kit_hybrid_parse.o
obj-$(CONFIG_LCD_KIT_HYBRID) += lcd_kit_hybrid_swctrl.o
obj-$(CONFIG_LCD_KIT_HYBRID) += lcd_kit_hybrid_recover.o
obj-$(CONFIG_LCD_KIT_HYBRID) += lcd_kit_hybrid_alpm.o
obj-$(CONFIG_LCD_KIT_HYBRID) += lcd_kit_hybrid_force.o

EXTRA_CFLAGS += \
	-I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/qcom/common/include \
	-I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/qcom/include \
	-I$(srctree)/drivers/devkit/lcdkit/lcdkit3.0/kernel/qcom/adapt \
	-I$(srctree)/techpack/display/msm \
	-I$(srctree)/techpack/display/msm/dsi \
	-I$(srctree)/techpack/display/include \
	-I$(srctree)/techpack/display/include/uapi/display \
	-I$(srctree)/include/drm \
	-I$(srctree)/drivers/clk/qcom \
	-I$(srctree)/drivers/devkit/tpkit/3_0/hybrid \
	-I$(srctree)/drivers/devkit/tpkit/3_0 \
	-I$(srctree)/drivers/huawei_platform/sensor/ext_inputhub/default \
	-I$(srctree)/drivers/huawei_platform/sensor/ext_inputhub/default/inner_cmd

ifeq ($(CONFIG_MATTING_ALGO_TASK),y)
EXTRA_CFLAGS += -I$(srctree)/../../vendor/huawei/extra/kernel/drivers/sensor/ext_inputhub/matting_algo_task
endif
