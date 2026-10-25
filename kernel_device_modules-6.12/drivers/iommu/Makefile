# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_DEVICE_MODULES_MTK_IOMMU) += mtk_iommu.o
obj-$(CONFIG_DEVICE_MODULES_MTK_IOMMU) += cannon-iommu-probe.o
else
obj-m += mtk_iommu.o
endif
