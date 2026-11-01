/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CANNON_HWC_ABI_H__
#define __CANNON_HWC_ABI_H__

#include <linux/build_bug.h>
#include <linux/types.h>

/* MIUI14's AArch64 HWC uses ioctl 0xc0806445 and 60-byte layer entries. */
#define CANNON_HWC_DISPLAYS 3
#define CANNON_HWC_MAX_LAYERS 64
#define CANNON_LAYERING_LEGACY 2

struct cannon_hwc_layer {
	__u32 ovl_id;
	__u32 src_fmt;
	__s32 dataspace;
	__u32 dst_offset_x, dst_offset_y;
	__u32 dst_width, dst_height;
	__s32 ext_sel_layer;
	__u32 src_offset_x, src_offset_y;
	__u32 src_width, src_height;
	__u32 layer_caps;
	__u32 clip;
	__u8 compress;
	__u8 secure;
};

struct cannon_hwc_layering {
	__u64 input_config[CANNON_HWC_DISPLAYS];
	__s32 disp_mode[CANNON_HWC_DISPLAYS];
	__s32 disp_mode_idx[CANNON_HWC_DISPLAYS];
	__s32 layer_num[CANNON_HWC_DISPLAYS];
	__s32 gles_head[CANNON_HWC_DISPLAYS];
	__s32 gles_tail[CANNON_HWC_DISPLAYS];
	__s32 hrt_num;
	__s32 res_idx;
	__u32 hrt_weight;
	__u32 hrt_idx;
	__u64 mml_cfg[CANNON_HWC_DISPLAYS];
};

static_assert(sizeof(struct cannon_hwc_layer) == 60);
static_assert(sizeof(struct cannon_hwc_layering) == 128);
static_assert(offsetof(struct cannon_hwc_layering, hrt_num) == 84);

struct drm_device;
struct drm_file;
int cannon_layering_rule_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);

#endif
