# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pwm/pwm_v2/include
obj-$(CONFIG_MTK_PWM) += mtk-pwm.o
mtk-pwm-y := mtk_pwm.o pwm_v2/mtk_pwm_hal.o
