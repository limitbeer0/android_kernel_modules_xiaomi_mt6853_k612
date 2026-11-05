# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_MEDIATEK_MT635X_AUXADC) += mt635x-auxadc.o
obj-$(CONFIG_MEDIATEK_MT6360_AUXADC) += mt6360-adc.o
else

obj-$(CONFIG_MEDIATEK_MT6362_ADC) += mt6362-adc.o

# xaga: platform module wiring
obj-m += mt6375-adc.o

# xaga: platform module wiring
obj-m += mt6375-auxadc.o

# xaga: platform module wiring
obj-m += mtk-spmi-pmic-adc.o
endif
