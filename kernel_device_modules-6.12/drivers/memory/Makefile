# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_HAVE_MTK_MC)	+= mediatek/

# xaga: platform module wiring
ifeq ($(CONFIG_CANNON_BRINGUP),y)
ccflags-y += -I$(src) -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mminfra
obj-$(CONFIG_DEVICE_MODULES_MTK_SMI) += mtk-smi.o
else
obj-m += mtk-smi.o
endif
