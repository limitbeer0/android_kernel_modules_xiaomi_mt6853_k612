/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CANNON_AW87XXX_H
#define CANNON_AW87XXX_H
struct device;
enum cannon_amp_mode {
	CANNON_AMP_OFF,
	CANNON_AMP_SPEAKER,
	CANNON_AMP_RECEIVER,
};
struct device *cannon_aw87xxx_get(struct device *consumer,
				  const char *property);
int cannon_aw87xxx_set_mode(struct device *dev, enum cannon_amp_mode mode);
#endif
