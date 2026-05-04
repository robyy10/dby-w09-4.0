ifdef CONFIG_HUAWEI_ANA_HS_KIT_AUDIO
	ccflags-y += -Idrivers/devkit/audiokit/
endif

obj-$(CONFIG_ANA_HS) += ana_hs.o
obj-$(CONFIG_ANA_HS_CORE) += ana_hs_core/
obj-$(CONFIG_INVERT_HS_CORE) += invert_hs_core/
ifeq ($(CONFIG_ANA_HS_EXTERN_OPS_KIT), y)
obj-y += ana_hs_extern_ops/
else ifeq ($(CONFIG_ANA_HS_EXTERN_OPS), y)
obj-y += ana_hs_extern_ops/
endif
