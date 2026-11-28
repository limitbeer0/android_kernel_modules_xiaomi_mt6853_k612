/* SPDX-License-Identifier: GPL-2.0-only */
/* Legacy ISP ION import is a DMA-BUF lifetime operation, not a fake mapping. */
#include <linux/dma-buf.h>
#include <linux/overflow.h>

struct cannon_isp_ion_ref {
	struct list_head list;
	struct dma_buf *buffer;
	pid_t owner;
	u32 node, port, references;
	int fd;
};

static LIST_HEAD(cannon_isp_ion_refs);
static DEFINE_MUTEX(cannon_isp_ion_lock);
static unsigned int cannon_isp_ion_count;
module_param_named(ion_kept_buffers, cannon_isp_ion_count, uint, 0444);

static int cannon_isp_ion_node(u32 *node)
{
	/* The 4.14 ABI omits CAMSV0/1, while the native driver retains slots. */
	if (*node >= ISP_CAMSV_START_IDX)
		*node += ISP_CAMSV2_IDX - ISP_CAMSV_START_IDX;
	if (*node != ISP_CAM_A_IDX && *node != ISP_CAM_B_IDX &&
	    (*node < ISP_CAMSV2_IDX || *node > ISP_CAMSV7_IDX))
		return -EINVAL;
	return 0;
}

static void cannon_isp_ion_drop(struct cannon_isp_ion_ref *entry)
{
	list_del(&entry->list);
	cannon_isp_ion_count--;
	dma_buf_put(entry->buffer);
	kfree(entry);
}

/* Last ISP close occurs only after all streams/IRQs have been stopped. */
static void cannon_isp_ion_release_all(void)
{
	struct cannon_isp_ion_ref *entry, *next;

	mutex_lock(&cannon_isp_ion_lock);
	list_for_each_entry_safe(entry, next, &cannon_isp_ion_refs, list)
		cannon_isp_ion_drop(entry);
	mutex_unlock(&cannon_isp_ion_lock);
}

static long cannon_isp_ion_ioctl(unsigned int cmd, void __user *arg)
{
	struct ISP_DEV_ION_NODE_STRUCT data;
	struct cannon_isp_ion_ref *entry, *next;
	struct dma_buf *buffer = NULL;
	pid_t owner = task_tgid_nr(current);
	int ret = 0;

	if (cmd == ISP_ION_FREE_BY_HWMODULE) {
		memset(&data, 0, sizeof(data));
		if (copy_from_user(&data.devNode, arg, sizeof(data.devNode)))
			return -EFAULT;
	} else {
		if (copy_from_user(&data, arg, sizeof(data)))
			return -EFAULT;
		if ((unsigned int)data.dmaPort >= _dma_max_wr_ || data.memID < 0)
			return -EINVAL;
	}
	ret = cannon_isp_ion_node(&data.devNode);
	if (ret)
		return ret;
	if (cmd == ISP_ION_IMPORT) {
		buffer = dma_buf_get(data.memID);
		if (IS_ERR(buffer))
			return PTR_ERR(buffer);
	}
	mutex_lock(&cannon_isp_ion_lock);
	list_for_each_entry_safe(entry, next, &cannon_isp_ion_refs, list) {
		if (entry->owner != owner || entry->node != data.devNode)
			continue;
		if (cmd == ISP_ION_FREE_BY_HWMODULE) {
			cannon_isp_ion_drop(entry);
			continue;
		}
		if (entry->port != data.dmaPort || entry->fd != data.memID)
			continue;
		if (cmd == ISP_ION_IMPORT) {
			/* An fd number reused for a different buffer must not alias. */
			if (entry->buffer != buffer)
				ret = -ESTALE;
			else if (entry->references == UINT_MAX)
				ret = -EOVERFLOW;
			else
				entry->references++;
		} else if (!--entry->references) {
			cannon_isp_ion_drop(entry);
		}
		goto out;
	}
	if (cmd == ISP_ION_FREE_BY_HWMODULE)
		goto out;
	if (cmd == ISP_ION_FREE) {
		ret = -ENOENT;
		goto out;
	}
	if (cannon_isp_ion_count >= 4096) {
		ret = -ENOSPC;
		goto out;
	}
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto out;
	}
	entry->buffer = buffer;
	entry->owner = owner;
	entry->node = data.devNode;
	entry->port = data.dmaPort;
	entry->fd = data.memID;
	entry->references = 1;
	buffer = NULL;
	list_add_tail(&entry->list, &cannon_isp_ion_refs);
	cannon_isp_ion_count++;
out:
	mutex_unlock(&cannon_isp_ion_lock);
	if (buffer)
		dma_buf_put(buffer);
	return ret;
}
