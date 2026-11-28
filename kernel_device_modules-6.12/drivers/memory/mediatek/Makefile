# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/memory/mediatek/


ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_MTK_DRAMC) += mtk_dramc.o
mtk_dramc-y := dramc.o dramc_bin.o
else
# xaga: platform module wiring
obj-m += emimpu.o

# xaga: platform module wiring (alps BUILD.bazel emi; exports mtk_emidbg_dump
# for mtk-smi-dbg)
obj-m += emi.o
emi-y := emi-cen.o emi-isu.o emi-dbg.o

# xaga: platform module wiring (alps BUILD.bazel mtk_dramc; exports
# mtk_dramc_get_ddr_type for mmqos-common)
obj-m += mtk_dramc.o
mtk_dramc-y := dramc.o dramc_bin.o

endif
