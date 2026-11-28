# SPDX-License-Identifier: GPL-2.0

# xaga: platform module wiring (alps BUILD.bazel mmqos-common; exports
# mtk_mmqos_probe/remove for mmqos-mt6895)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mtk-interconnect
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/qos
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/soc/mediatek
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mm_monitor
ccflags-y += -I$(src)

ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_INTERCONNECT_MTK_EMI) += mtk-emi.o cannon-emi-probe.o
obj-$(CONFIG_INTERCONNECT_MTK_MMQOS_COMMON) += mmqos-common.o
mmqos-common-y := mmqos-mtk.o mmqos-hrt.o mmqos-test.o
obj-$(CONFIG_INTERCONNECT_MTK_MMQOS_MT6853) += mmqos-mt6853.o cannon-mmqos-probe.o
else
obj-m += mmqos-common.o
mmqos-common-y := mmqos-mtk.o mmqos-hrt.o mmqos-test.o
mmqos-common-$(CONFIG_MTK_MMQOS_VCP) += mmqos-vcp.o mmqos-mmup.o

# xaga: platform module wiring
obj-m += mmqos-mt6895.o

# xaga: platform module wiring
obj-m += mtk-emi.o

endif
