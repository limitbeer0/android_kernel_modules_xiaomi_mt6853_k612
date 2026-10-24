ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_DEVICE_MODULES_KEYBOARD_MTK_PMIC) += mtk-pmic-keys.o
else
# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the input core drivers.
#

obj-$(CONFIG_KEYBOARD_MTK_KPD)		+= mtk-kpd.o
obj-$(CONFIG_DEVICE_MODULES_KEYBOARD_MTK_PMIC)		+= mtk-pmic-keys.o

# xaga: platform module wiring
obj-m += mtk-kpd.o

# xaga: platform module wiring
obj-m += mtk-pmic-keys.o

endif # CONFIG_CANNON_BRINGUP
