# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_REGULATOR_MT6359P) += mt6359p-regulator.o
else
#
# Makefile for regulator drivers.
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/regulator
CFLAGS_mtk-vmm-isp71-regulator.o := -I$(DEVICE_MODULES_PATH)/drivers/regulator
CFLAGS_mtk-dvfsrc-regulator.o := -I$(DEVICE_MODULES_PATH)/drivers/regulator

obj-$(CONFIG_DEVICE_MODULES_REGULATOR_MT6357) += mt6357-regulator.o
#obj-$(CONFIG_DEVICE_MODULES_REGULATOR_MT6358) += mt6358-regulator.o
obj-$(CONFIG_REGULATOR_MT6362) += mt6362-regulator.o
#obj-$(CONFIG_REGULATOR_MTK_VMM_ISP71) += mtk-vmm-isp71-regulator.o

ccflags-$(CONFIG_REGULATOR_DEBUG) += -DDEBUG

# xaga: platform module wiring
obj-m += mt6315-regulator.o

# xaga: platform module wiring
obj-m += mt6363-regulator.o

# xaga: platform module wiring
obj-m += mt6368-regulator.o

# xaga: platform module wiring
obj-m += mtk-dvfsrc-regulator.o

# xaga: platform module wiring
obj-m += rt5133-regulator.o

# xaga: platform module wiring
obj-m += rt6160-regulator.o
endif # CONFIG_CANNON_BRINGUP
