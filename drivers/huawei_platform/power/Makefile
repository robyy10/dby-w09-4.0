ifneq ($(TARGET_PRODUCT),msmnile)
obj-y += huawei_charger_adaptor.o
obj-y += charger/
obj-y += battery/

obj-$(CONFIG_HUAWEI_FEATURE_PRINT_PID_NAME) += pid_socket/
ifeq (y, $(findstring y,$(CONFIG_HUAWEI_DUBAI) $(CONFIG_POWER_DUBAI)))
obj-y += dubai/
endif
endif
obj-$(CONFIG_HUAWEI_KSTATE) += hw_kstate/