subdir-ccflags-y += -I$(srctree)/drivers/gpu/msm
subdir-ccflags-y += -I$(srctree)/drivers/staging/android
subdir-ccflags-y += -I$(srctree)/drivers/staging/android/ion
subdir-ccflags-y += -I$(srctree)/mm

obj-$(CONFIG_DFX_MEMCHECK) += memcheck_mod.o \
			      memcheck_ioctl.o \
			      memcheck_account.o \
			      memcheck_log_gpumem.o \
			      memcheck_log_ashmem.o \
			      memcheck_log_cma.o \
			      memcheck_log_vmalloc.o

ifneq ($(CONFIG_ARCH_MONACO), y)
obj-$(CONFIG_DFX_MEMCHECK) += memcheck_log_ion.o
endif

obj-$(CONFIG_DFX_MEMCHECK_STACK) += memcheck_stack.o
