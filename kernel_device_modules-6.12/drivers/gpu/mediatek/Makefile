#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

# cannon: default to mt6853 (build-cannon.sh passes MTK_PLATFORM=mt6853,
# which overrides this fallback).
export MTK_PLATFORM ?= mt6853

ifneq ($(filter-out "" '',$(CONFIG_MTK_GPU_VERSION)),)
MTK_GPU_VERSION = $(subst ",,$(CONFIG_MTK_GPU_VERSION))

$(info [MTK GPU] MTK_GPU_VERSION = $(MTK_GPU_VERSION))

ifneq ($(CONFIG_MTK_PLATFORM),)
export MTK_PLATFORM = $(subst ",,$(CONFIG_MTK_PLATFORM))
else ifneq ($(word 4,$(MTK_GPU_VERSION)),)
export MTK_PLATFORM = $(word 4,$(MTK_GPU_VERSION))
endif
endif

ifeq ($(MTK_PLATFORM),mt6873)
subdir-ccflags-y += -DCONFIG_GPU_MT6873
else ifeq ($(MTK_PLATFORM),mt6893)
subdir-ccflags-y += -DCONFIG_GPU_MT6893
else ifeq ($(MTK_PLATFORM),mt6853)
subdir-ccflags-y += -DCONFIG_GPU_MT6853
else ifeq ($(MTK_PLATFORM),mt6768)
subdir-ccflags-y += -DCONFIG_GPU_MT6768
else ifeq ($(MTK_PLATFORM),mt6877)
subdir-ccflags-y += -DCONFIG_GPU_MT6877
else ifeq ($(MTK_PLATFORM),mt6761)
subdir-ccflags-y += -DCONFIG_GPU_MT6761
else ifeq ($(MTK_PLATFORM),mt6765)
subdir-ccflags-y += -DCONFIG_GPU_MT6765
else ifeq ($(MTK_PLATFORM),mt6781)
subdir-ccflags-y += -DCONFIG_GPU_MT6781
else ifeq ($(MTK_PLATFORM),mt6853)
subdir-ccflags-y += -DCONFIG_GPU_MT6853
else ifeq ($(MTK_PLATFORM),mt6833)
subdir-ccflags-y += -DCONFIG_GPU_MT6833
else ifeq ($(MTK_PLATFORM),mt6789)
subdir-ccflags-y += -DCONFIG_GPU_MT6789
endif

ifneq ($(CONFIG_MTK_GPU_SUPPORT),)
# cannon: the xaga build gets this via DEVCIE_MODULES_INCLUDE/LINUXINCLUDE;
# the CANNON Kbuild branch keeps a minimal include set, so provide the
# mt-plat header dir (mtk_gpu_utility.h) for ged/gpu_bm/gpufreq here.
    subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/mt-plat
    obj-y += ged/
    obj-y += gpufreq/
    obj-y += gpu_bm/
    obj-y += gpu_iommu/
    obj-y += gpueb/
    obj-y += gpu_pdma/
    obj-y += hal/
    obj-y += mali-valhall/

ifneq ($(filter-out "" '',$(CONFIG_MTK_GPU_VERSION)),)
# GPU R5P0
ifeq ($(CONFIG_MTK_GPU_R5P0),y)
    obj-y += $(subst ",,$(CONFIG_MTK_PLATFORM))/
else ifneq ($(word 1,$(MTK_GPU_VERSION)),)
    obj-y += gpu_$(word 1,$(MTK_GPU_VERSION))/
else ifneq ($(CONFIG_MTK_PLATFORM),)
ifneq ($(wildcard $(srctree)/$(src)/$(subst ",,$(CONFIG_MTK_PLATFORM))/Makefile),)
    obj-y += $(subst ",,$(CONFIG_MTK_PLATFORM))/
endif
endif
endif

endif
