# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_DEVICE_MODULES_MFD_MT6397) += mt6397.o
mt6397-y := mt6397-core.o mt6397-irq.o mt6358-irq.o
else

ifdef CONFIG_GRT_HYPERVISOR
obj-$(CONFIG_MFD_MT6685)	+= hyp-mt6685-core.o
obj-$(CONFIG_MFD_MTK_SPMI_PMIC)	+= hyp-mtk-spmi-pmic.o
endif

obj-$(CONFIG_REGULATOR_MT6362)	+= mt6362-core.o

# xaga: platform module wiring
obj-m += mt6375.o

# xaga: platform module wiring
obj-m += mt6685-audclk.o

# xaga: platform module wiring
obj-m += mt6685-core.o

# xaga: platform module wiring
obj-m += mtk-spmi-pmic.o
endif # CONFIG_CANNON_BRINGUP
