ifneq ($(TARGET_BUILD_VARIANT),user)
ccflags-y += -DDBG_ULOG_TRACE
endif

obj-$(CONFIG_ULOG_TRACE) += ulog_trace.o
obj-$(CONFIG_ULOG_TRACE) += ulog_file.o
