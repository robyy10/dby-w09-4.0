#obj-y			+= log/
#obj-$(CONFIG_LOGGER_EXTEND) += log/
obj-$(CONFIG_PROC_IO_INFO)   += ioinfo/
obj-y += net/
obj-y += power/
obj-$(CONFIG_RADIO_BUCKBOOST)			+= radiobuckboost/
obj-$(CONFIG_HUAWEI_PLATFORM_MAINTAIN) += mntn/
obj-$(CONFIG_HUAWEI_GPU_OPP_STATS) += gpu_stats/
obj-$(CONFIG_DRG) += hw_drg/

ifneq ($(TARGET_PRODUCT),msmnile)
$(info "TARGET_PRODUCT != msmnile")
obj-y += hwpower/
obj-y += usb/
obj-y += comb_key/
obj-y += hwthermal/
endif
