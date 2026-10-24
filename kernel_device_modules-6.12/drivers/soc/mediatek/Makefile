# SPDX-License-Identifier: GPL-2.0-only
ifeq ($(CONFIG_CANNON_BRINGUP),y)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
obj-$(CONFIG_DEVICE_MODULES_MTK_PMIC_WRAP) += mtk-pmic-wrap.o
else
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cmdq/mailbox
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdebug/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmp/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/smi/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/soc/mediatek/
obj-$(CONFIG_MTK_CMDQ) += mtk-cmdq-helper.o
#obj-$(CONFIG_DEVICE_MODULES_MTK_PMIC_WRAP) += mtk-pmic-wrap.o
ifneq ($(wildcard $(src)/mtk-scpsys-mt6991-ivi.c),)
obj-$(CONFIG_DEVICE_MODULES_MTK_SCPSYS_IVI) += mtk-scpsys-mt6991-ivi.o
endif
#ifneq ($(wildcard $(src)/mt8188-pm-domains.h),)
#obj-$(CONFIG_DEVICE_MODULES_MTK_SCPSYS_PM_DOMAINS) += mtk-pm-domains.o
#endif
obj-$(CONFIG_DEVICE_MODULES_MTK_MMSYS_MMSYS) += mtk-mmsys.o
obj-$(CONFIG_DEVICE_MODULES_MTK_DEVAPC) += devapc/
# obj-$(CONFIG_MTK_DEVMPU) += devmpu/
obj-$(CONFIG_MTK_MMDVFS) += mmdvfs/
obj-$(CONFIG_DEVICE_MODULES_MTK_MMSYS_MUTEX) += mtk-mutex.o

# xaga: platform module wiring
obj-m += mtk-dvfsrc.o

# xaga: platform module wiring
obj-m += mtk-mbox.o

# xaga: platform module wiring
obj-m += mtk-mmdvfs.o

# xaga: platform module wiring (alps BUILD.bazel mtk-mmdvfs-v3; exports
# mtk_mmdvfs_camera_notify / mtk_mmdvfs_genpd_notify for mtk-vmm-notifier)
obj-$(CONFIG_MTK_MMDVFS_VCP) += mtk-mmdvfs-v3.o

# xaga: platform module wiring
obj-m += mtk-pm-domain-disable-unused.o

# xaga: platform module wiring
obj-m += mtk-pmic-wrap.o

# xaga: platform module wiring
obj-m += mtk-scpsys-bringup.o

# xaga: platform module wiring
obj-m += mtk-scpsys.o

# xaga: platform module wiring
obj-m += mtk-scpsys-mt6895.o

# xaga: platform module wiring
obj-m += mtk-socinfo.o

# xaga: platform module wiring
obj-m += mtk_tinysys_ipi.o
endif # CONFIG_CANNON_BRINGUP
