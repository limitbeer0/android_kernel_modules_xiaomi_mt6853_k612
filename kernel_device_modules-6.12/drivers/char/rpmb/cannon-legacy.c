// SPDX-License-Identifier: GPL-2.0
/* Microtrust 400 userspace ABI, from the running 4.14 rpmb-mtk driver.
 * Its payload is a u32 byte count followed by inline RPMB frames, not the
 * pointer-bearing generic RPMB_IOC_REQ_CMD structure.  MAC verification for
 * these frames remains in secure world, as in the original implementation.
 */
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/rpmb.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "rpmb-cdev.h"

#define SOTER_WRITE_DATA 5
#define SOTER_READ_DATA 6
#define SOTER_GET_COUNTER 7
#define SOTER_GET_WRITE_SIZE 8
#define SOTER_SET_KEY 9
#define SOTER_BUFFER_SIZE (24 * 1024)

static int cannon_rpmb_counter(struct rpmb_dev *rdev, void __user *arg)
{
	struct rpmb_frame *frames;
	struct rpmb_data data = { .req_type = RPMB_GET_WRITE_COUNTER };
	u32 counter;
	int ret;

	frames = kcalloc(2, sizeof(*frames), GFP_KERNEL);
	if (!frames)
		return -ENOMEM;
	frames[0].req_resp = cpu_to_be16(RPMB_GET_WRITE_COUNTER);
	get_random_bytes(frames[0].nonce, sizeof(frames[0].nonce));
	data.icmd.frames = frames;
	data.icmd.nframes = 1;
	data.ocmd.frames = frames + 1;
	data.ocmd.nframes = 1;
	ret = rpmb_mtk_cmd_req(rdev, &data, 0);
	if (ret)
		goto out;
	if (frames[1].req_resp != cpu_to_be16(RPMB_RESP_GET_COUNTER) ||
	    frames[1].result ||
	    memcmp(frames[0].nonce, frames[1].nonce, sizeof(frames[0].nonce))) {
		ret = -EIO;
		goto out;
	}
	counter = be32_to_cpu(frames[1].write_counter);
	if (copy_to_user(arg, &counter, sizeof(counter)))
		ret = -EFAULT;
out:
	kfree_sensitive(frames);
	return ret;
}

static int cannon_rpmb_transfer(struct rpmb_dev *rdev, unsigned int cmd,
				void __user *arg)
{
	struct rpmb_data data = { 0 };
	struct rpmb_frame *frames, *response = NULL;
	u32 bytes, count;
	void __user *payload = (char __user *)arg + sizeof(bytes);
	int ret;

	if (get_user(bytes, (u32 __user *)arg))
		return -EFAULT;
	if (!bytes || bytes > SOTER_BUFFER_SIZE - sizeof(bytes) ||
	    bytes % sizeof(*frames))
		return -EINVAL;
	count = bytes / sizeof(*frames);
	frames = memdup_user(payload, bytes);
	if (IS_ERR(frames))
		return PTR_ERR(frames);

	data.icmd.frames = frames;
	if (cmd == SOTER_READ_DATA) {
		data.req_type = RPMB_READ_DATA;
		data.icmd.nframes = 1;
		/* UFS requires block_count, while the TEE's eMMC-format request
		 * leaves it zero.  This is the same fixup as the 4.14 driver.
		 */
		frames[0].block_count = cpu_to_be16(count);
		data.ocmd.frames = frames;
		data.ocmd.nframes = count;
	} else {
		response = kzalloc(sizeof(*response), GFP_KERNEL);
		if (!response) {
			ret = -ENOMEM;
			goto out;
		}
		data.req_type = RPMB_WRITE_DATA;
		data.icmd.nframes = count;
		data.ocmd.frames = response;
		data.ocmd.nframes = 1;
	}
	ret = rpmb_mtk_cmd_req(rdev, &data, 0);
	if (!ret) {
		/* TEE checks the write response in the first input frame. */
		if (response)
			memcpy(frames, response, sizeof(*frames));
		if (copy_to_user(payload, frames, bytes))
			ret = -EFAULT;
	}
out:
	kfree_sensitive(response);
	kfree_sensitive(frames);
	return ret;
}

long cannon_rpmb_legacy_ioctl(struct rpmb_dev *rdev, unsigned int cmd,
			      void __user *arg)
{
	u32 size;

	switch (cmd) {
	case SOTER_WRITE_DATA:
	case SOTER_READ_DATA:
		return cannon_rpmb_transfer(rdev, cmd, arg);
	case SOTER_GET_COUNTER:
		return cannon_rpmb_counter(rdev, arg);
	case SOTER_GET_WRITE_SIZE:
		size = rdev->ops->reliable_wr_cnt;
		return copy_to_user(arg, &size, sizeof(size)) ? -EFAULT : 0;
	case SOTER_SET_KEY:
		/* This device is provisioned. Bring-up must never program a key. */
		return -EOPNOTSUPP;
	default:
		return -ENOIOCTLCMD;
	}
}
