#
# Copyright (C) 2019 MediaTek Inc.
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

# cannon: the upstream Makefile was fully commented out (OPPO drives this
# directory through BUILD.bazel). Restore the make wiring for the two modules
# alps defines there, composition copied verbatim from BUILD.bazel:
#   mtk_gpufreq_wrapper_legacy = gpufreq_v2_legacy + gpufreq_history_legacy +
#                                gpufreq_debug_legacy
#   mtk_gpufreq_mt6853         = gpufreq_mt6853 + gpuppm_legacy +
#                                gpufreq_history_mt6853
# The unused per-platform blocks stay commented as upstream left them.
# Include paths mirror the BUILD.bazel copts (missing dirs are harmless).

ccflags-y += \
	-Wno-unused-variable \
	-Wno-unused-function \
	-Wno-unused-but-set-variable
ccflags-y += -I$(DEVICE_MODULES_PATH)/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpufreq/v2_legacy/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/ged/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpueb/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/mt-plat/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/base/power/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pbm/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/aee/aed/

mtk_gpufreq_wrapper_legacy-y := \
	gpufreq_v2_legacy.o \
	gpufreq_history_legacy.o \
	gpufreq_debug_legacy.o

mtk_gpufreq_mt6853-y := \
	gpufreq_history_mt6853.o \
	gpuppm_legacy.o \
	gpufreq_mt6853.o

obj-$(CONFIG_MTK_GPU_SUPPORT) += mtk_gpufreq_wrapper_legacy.o
obj-$(CONFIG_MTK_GPU_MT6853_SUPPORT) += mtk_gpufreq_mt6853.o
