/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 XiaoMi, Inc.
 * LM36273 LCD bias/backlight registers, ported from 4.14 cannon leds-lm36273.h.
 * The 4.14 header carried two brightness tables behind CONFIG_LED_MT6853;
 * only the MT6853 exponential table is kept, in leds-lm36273.c.
 */
#ifndef __LEDS_LM36273_H__
#define __LEDS_LM36273_H__

#include <linux/mutex.h>
#include <linux/i2c.h>

#define LP36273_DISP_REV                0x01
#define LP36273_DISP_BC1                0x02
#define LP36273_DISP_BC2                0x03
#define LP36273_DISP_BB_LSB             0x04
#define LP36273_DISP_BB_MSB             0x05
#define LP36273_DISP_BAFLT              0x06
#define LP36273_DISP_BAFHT              0x07
#define LP36273_DISP_BL_ENABLE  0x08
#define LP36273_DISP_BIAS_CONF1 0x09
#define LP36273_DISP_BIAS_CONF2 0x0a
#define LP36273_DISP_BIAS_CONF3 0x0b
#define LP36273_DISP_BIAS_BOOST 0x0c
#define LP36273_DISP_BIAS_VPOS  0x0d
#define LP36273_DISP_BIAS_VNEG  0x0e
#define LP36273_DISP_FLAGS              0x0f
#define LP36273_DISP_OPTION1    0x10
#define LP36273_DISP_OPTION2    0x11
#define LP36273_DISP_PTD_LSB    0x12
#define LP36273_DISP_PTD_MSB    0x13

#define BL_LEVEL_MAX 2047
#define BL_HBM_L1 1945 /* 22mA for exponential mapping*/
#define BL_HBM_L2 1991 /* 25.3mA for exponential mapping*/
#define BL_HBM_L3 2018 /* 27.5mA for exponential mapping*/

struct lm36273_led {
	struct mutex lock;
	struct i2c_client *client;
	int level;
	int hbm_on;
};

struct lm36273_reg {
	uint8_t reg;
	uint8_t value;
};

enum LM36273_REG_RW {
	LM36273_REG_WRITE,
	LM36273_REG_READ,
};

struct LCM_led_i2c_read_write {
	unsigned int read_enable;
	unsigned int read_count;
	unsigned char buffer[64];
};

#endif
