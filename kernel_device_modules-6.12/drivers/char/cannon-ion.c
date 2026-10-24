// SPDX-License-Identifier: GPL-2.0-only
/*
 * 4.14 Cannon ION userspace handles backed by the 6.12 system DMA heaps.
 * The exported buffers are real DMA-BUFs. No physical/IOVA addresses or
 * secure allocations are emulated; MTK device mapping remains separate work.
 */
#include <linux/compat.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/cannon_ion.h>

struct cannon_handle {
	struct dma_buf *buffer;
	unsigned int references;
};

struct cannon_client {
	struct mutex lock;
	struct idr handles;
};

/* Normal, non-contiguous page heaps only. Never advertise protected heaps. */
static const struct cannon_ion_heap heaps[] = {
	{ .name = "system", .type = 0, .heap_id = 0 },
	{ .name = "multimedia", .type = 10, .heap_id = 10 },
	{ .name = "multimedia_camera", .type = 10, .heap_id = 12 },
};

static void handle_put(struct cannon_client *client, int id)
{
	struct cannon_handle *handle = idr_find(&client->handles, id);

	if (--handle->references)
		return;
	idr_remove(&client->handles, id);
	dma_buf_put(handle->buffer);
	kfree(handle);
}

/* Takes ownership of buffer only on success. Caller holds client->lock. */
static int handle_add(struct cannon_client *client, struct dma_buf *buffer)
{
	struct cannon_handle *handle;
	int id;

	idr_for_each_entry(&client->handles, handle, id) {
		if (handle->buffer == buffer) {
			if (handle->references == UINT_MAX)
				return -EOVERFLOW;
			handle->references++;
			dma_buf_put(buffer);
			return id;
		}
	}
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;
	handle->buffer = buffer;
	handle->references = 1;
	id = idr_alloc(&client->handles, handle, 1, 0, GFP_KERNEL);
	if (id < 0)
		kfree(handle);
	return id;
}

static long allocate(struct cannon_client *client, void __user *arg, bool compat)
{
	struct cannon_ion_allocation native;
	struct cannon_ion_allocation32 narrow;
	struct dma_heap *heap;
	struct dma_buf *buffer;
	unsigned long len, align;
	u32 mask, flags;
	int id;

	if (compat) {
		if (copy_from_user(&narrow, arg, sizeof(narrow)))
			return -EFAULT;
		len = narrow.len; align = narrow.align;
		mask = narrow.heap_id_mask; flags = narrow.flags;
	} else {
		if (copy_from_user(&native, arg, sizeof(native)))
			return -EFAULT;
		len = native.len; align = native.align;
		mask = native.heap_id_mask; flags = native.flags;
	}
	/* Old MM bit16 means zero-fill; system DMA heaps already zero pages. */
	if (!len || len > ULONG_MAX - (PAGE_SIZE - 1) || align > PAGE_SIZE ||
	    (align && !is_power_of_2(align)) || (flags & ~(7U | BIT(16))))
		return -EINVAL;
	if (!(mask & (BIT(0) | BIT(10) | BIT(12)))) {
		pr_info_ratelimited("cannon-ion: unsupported heap mask %#x\n", mask);
		return -EOPNOTSUPP;
	}
	heap = dma_heap_find(flags & 1 ? "system" : "system-uncached");
	if (!heap)
		return -ENODEV;
	buffer = dma_heap_buffer_alloc(heap, PAGE_ALIGN(len), O_RDWR | O_CLOEXEC, 0);
	dma_heap_put(heap);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);
	id = handle_add(client, buffer);
	if (id < 0) {
		dma_buf_put(buffer);
		return id;
	}
	if (compat) {
		narrow.handle = id;
		if (!copy_to_user(arg, &narrow, sizeof(narrow)))
			return 0;
	} else {
		native.handle = id;
		if (!copy_to_user(arg, &native, sizeof(native)))
			return 0;
	}
	handle_put(client, id);
	return -EFAULT;
}

static long share(struct cannon_client *client, void __user *arg)
{
	struct cannon_ion_fd data;
	struct cannon_handle *handle;
	int fd;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;
	handle = idr_find(&client->handles, data.handle);
	if (!handle)
		return -EINVAL;
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;
	data.fd = fd;
	/* Install only after copying the result: EFAULT must not leak an fd. */
	if (copy_to_user(arg, &data, sizeof(data))) {
		put_unused_fd(fd);
		return -EFAULT;
	}
	get_dma_buf(handle->buffer);
	fd_install(fd, handle->buffer->file);
	return 0;
}

static long import(struct cannon_client *client, void __user *arg)
{
	struct cannon_ion_fd data;
	struct dma_buf *buffer;
	int id;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;
	buffer = dma_buf_get(data.fd);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);
	id = handle_add(client, buffer);
	if (id < 0) {
		dma_buf_put(buffer);
		return id;
	}
	data.handle = id;
	if (!copy_to_user(arg, &data, sizeof(data)))
		return 0;
	handle_put(client, id);
	return -EFAULT;
}

static long query(void __user *arg)
{
	struct cannon_ion_query data;
	u32 count;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;
	if (data.reserved0 || data.reserved1 || data.reserved2)
		return -EINVAL;
	if (data.heaps && !data.cnt)
		return -EINVAL;
	count = min_t(u32, data.cnt, ARRAY_SIZE(heaps));
	if (data.heaps && copy_to_user(u64_to_user_ptr(data.heaps), heaps,
				     count * sizeof(heaps[0])))
		return -EFAULT;
	data.cnt = data.heaps ? count : ARRAY_SIZE(heaps);
	return copy_to_user(arg, &data, sizeof(data)) ? -EFAULT : 0;
}

static long custom(void __user *arg, bool compat)
{
	struct cannon_ion_custom native;
	struct cannon_ion_custom32 narrow;
	void __user *payload;
	u32 command, subcommand;

	if (compat) {
		if (copy_from_user(&narrow, arg, sizeof(narrow)))
			return -EFAULT;
		command = narrow.cmd;
		payload = compat_ptr(narrow.arg);
	} else {
		if (copy_from_user(&native, arg, sizeof(native)))
			return -EFAULT;
		command = native.cmd;
		payload = (void __user *)native.arg;
	}
	if (get_user(subcommand, (u32 __user *)payload))
		return -EFAULT;
	pr_info_ratelimited("cannon-ion: unsupported MTK custom %u/%u comm=%s\n",
			    command, subcommand, current->comm);
	return -EOPNOTSUPP;
}

static long ion_ioctl(struct file *file, unsigned int cmd, unsigned long address)
{
	struct cannon_client *client = file->private_data;
	void __user *arg = in_compat_syscall() ? compat_ptr(address) : (void __user *)address;
	struct cannon_ion_handle handle;
	struct cannon_ion_fd data;
	struct dma_buf *buffer;
	long result;

	mutex_lock(&client->lock);
	switch (cmd) {
	case CION_ALLOC:
		result = in_compat_syscall() ? -ENOTTY : allocate(client, arg, false);
		break;
	case CION_ALLOC32:
		result = in_compat_syscall() ? allocate(client, arg, true) : -ENOTTY;
		break;
	case CION_FREE:
		if (copy_from_user(&handle, arg, sizeof(handle))) {
			result = -EFAULT;
			break;
		}
		result = -EINVAL;
		if (idr_find(&client->handles, handle.handle)) {
			handle_put(client, handle.handle);
			result = 0;
		}
		break;
	case CION_SHARE:
	case CION_MAP:
		result = share(client, arg);
		break;
	case CION_IMPORT:
		result = import(client, arg);
		break;
	case CION_SYNC:
		result = -EFAULT;
		if (copy_from_user(&data, arg, sizeof(data)))
			break;
		buffer = dma_buf_get(data.fd);
		if (IS_ERR(buffer)) {
			result = PTR_ERR(buffer);
			break;
		}
		result = dma_buf_end_cpu_access(buffer, DMA_BIDIRECTIONAL);
		dma_buf_put(buffer);
		break;
	case CION_QUERY:
		result = query(arg);
		break;
	case CION_CUSTOM:
		result = in_compat_syscall() ? -ENOTTY : custom(arg, false);
		break;
	case CION_CUSTOM32:
		result = in_compat_syscall() ? custom(arg, true) : -ENOTTY;
		break;
	default:
		result = -ENOTTY;
	}
	mutex_unlock(&client->lock);
	return result;
}

static int ion_open(struct inode *inode, struct file *file)
{
	struct cannon_client *client = kzalloc(sizeof(*client), GFP_KERNEL);

	if (!client)
		return -ENOMEM;
	mutex_init(&client->lock);
	idr_init(&client->handles);
	file->private_data = client;
	return nonseekable_open(inode, file);
}

static int ion_release(struct inode *inode, struct file *file)
{
	struct cannon_client *client = file->private_data;
	struct cannon_handle *handle;
	int id;

	idr_for_each_entry(&client->handles, handle, id) {
		dma_buf_put(handle->buffer);
		kfree(handle);
	}
	idr_destroy(&client->handles);
	kfree(client);
	return 0;
}

static const struct file_operations ion_fops = {
	.owner = THIS_MODULE,
	.open = ion_open,
	.release = ion_release,
	.unlocked_ioctl = ion_ioctl,
	.compat_ioctl = ion_ioctl,
};

static struct miscdevice ion_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ion",
	.fops = &ion_fops,
	.mode = 0600,
};

static int __init cannon_ion_init(void)
{
	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	pr_info("cannon-ion: legacy handles over DMA heaps; MTK IOVA/secure heaps unavailable\n");
	return misc_register(&ion_device);
}
module_init(cannon_ion_init);

static void __exit cannon_ion_exit(void)
{
	misc_deregister(&ion_device);
}
module_exit(cannon_ion_exit);
MODULE_DESCRIPTION("Cannon legacy ION handle compatibility over DMA heaps");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(DMA_BUF);
