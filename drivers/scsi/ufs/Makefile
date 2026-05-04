# SPDX-License-Identifier: GPL-2.0
# UFSHCD makefile
obj-$(CONFIG_SCSI_UFS_DWC_TC_PCI) += tc-dwc-g210-pci.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_DWC_TC_PLATFORM) += tc-dwc-g210-pltfrm.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_QCOM) += ufs-qcom.o
obj-$(CONFIG_SCSI_UFS_QCOM_ICE) += ufs-qcom-ice.o
obj-$(CONFIG_SCSI_UFSHCD) += ufshcd-core.o
ufshcd-core-objs := ufshcd.o ufs-sysfs.o
obj-$(CONFIG_SCSI_UFSHCD_PCI) += ufshcd-pci.o
obj-$(CONFIG_SCSI_UFSHCD_PLATFORM) += ufshcd-pltfrm.o
obj-$(CONFIG_SCSI_UFS_TEST) += ufs_test.o
obj-$(CONFIG_DEBUG_FS) += ufs-debugfs.o ufs-qcom-debugfs.o
obj-$(CONFIG_SCSI_UFS_HISI) += ufs-hisi.o
obj-$(CONFIG_HUAWEI_UFS_DSM)  += dsm_ufs.o
obj-$(CONFIG_MAS_BLK)  += mas_ufs.o
obj-$(CONFIG_MAS_UFS_MANUAL_BKOPS) += mas-ufs-bkops.o mas_ufs_bkops_interface.o
obj-$(CONFIG_HUAWEI_UFS_VENDOR_MODE) += ufs-vendor-mode-kona.o ufs-vendor-cmd-kona.o
ifeq ($(TARGET_VERSION_MODE),factory)
    EXTRA_CFLAGS += -DHEALTH_INFO_FACTORY_MODE
endif
obj-$(CONFIG_HUAWEI_UFS_HPB) += ufs-hpb-kona.o
obj-$(CONFIG_HUAWEI_UFS_DYNAMIC_SWITCH_WB)  += ufs_wb_dynamic_switch_kona.o
ufshcd-core-$(CONFIG_SCSI_UFS_CRYPTO) += ufshcd-crypto.o
ufshcd-core-$(CONFIG_SCSI_UFS_CRYPTO_QTI) += ufshcd-crypto-qti.o
