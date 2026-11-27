// SPDX-License-Identifier: GPL-2.0-only
/*
 * 4.14 Cannon ION userspace handles backed by the 6.12 system DMA heaps.
 * The exported buffers are real DMA-BUFs. No physical/IOVA addresses or
 * secure allocations are emulated. Normal CAM ports use a native DMA domain.
 */
#include <linux/compat.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-heap.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/workqueue.h>
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

#define CANNON_ION_NAME_LEN 48

struct cannon_client {
	struct mutex lock;
	struct idr handles;
	char name[CANNON_ION_NAME_LEN];
};

/*
 * Debug data belongs to a DMA-BUF, not an ION handle: clients may free all
 * handles while keeping a shared fd, then import it again. A metadata entry
 * holds one buffer reference until the last external reference disappears.
 * Collect it within 200 ms. This avoids changing the DMA-BUF/KMI layout or
 * touching exporter-private data, and prevents pointer reuse during lookup.
 */
struct cannon_debug {
	struct list_head list;
	struct dma_buf *buffer;
	char name[CANNON_ION_NAME_LEN];
	u32 values[4];
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	dma_addr_t iova;
};

static LIST_HEAD(cannon_debug_list);
static DEFINE_MUTEX(cannon_debug_lock);
static struct delayed_work cannon_debug_work;
static bool cannon_debug_stopping;
static struct device *cannon_dma_devs[18];

static void cannon_debug_free(struct cannon_debug *entry)
{
	bool pinned = !!entry->table;

	if (entry->table)
		dma_buf_unmap_attachment(entry->attachment, entry->table, DMA_BIDIRECTIONAL);
	if (entry->attachment)
		dma_buf_detach(entry->buffer, entry->attachment);
	dma_buf_put(entry->buffer);
	kfree(entry);
	if (pinned)
		module_put(THIS_MODULE);
}

static void cannon_debug_collect(struct work_struct *work)
{
	struct cannon_debug *entry, *next;

	mutex_lock(&cannon_debug_lock);
	list_for_each_entry_safe(entry, next, &cannon_debug_list, list) {
		if (file_count(entry->buffer->file) != 1)
			continue;
		list_del(&entry->list);
		cannon_debug_free(entry);
	}
	if (!list_empty(&cannon_debug_list) && !READ_ONCE(cannon_debug_stopping))
		schedule_delayed_work(&cannon_debug_work, msecs_to_jiffies(200));
	mutex_unlock(&cannon_debug_lock);
}

static struct cannon_debug *cannon_debug_find(struct dma_buf *buffer)
{
	struct cannon_debug *entry;

	list_for_each_entry(entry, &cannon_debug_list, list)
		if (entry->buffer == buffer)
			return entry;
	return NULL;
}

static struct cannon_debug *cannon_debug_get(struct dma_buf *buffer)
{
	struct cannon_debug *entry = cannon_debug_find(buffer);

	if (entry)
		return entry;
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);
	get_dma_buf(buffer);
	entry->buffer = buffer;
	list_add_tail(&entry->list, &cannon_debug_list);
	return entry;
}

/* Legacy user port IDs use larb << 5 | port. CCU has separate domains and
 * must not be mapped through the normal camera DMA client.
 */
static bool cannon_camera_port(u32 id)
{
	u32 larb = id >> 5, port = id & 31;

	return (larb == 13 && port < 9) ||
	       ((larb == 16 || larb == 17) && port < 17);
}

static int cannon_dma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	unsigned int i, larb;
	int ret;

	if (!domain || !fwspec || !fwspec->num_ids)
		return -EPROBE_DEFER;
	if ((domain->type != IOMMU_DOMAIN_DMA && domain->type != IOMMU_DOMAIN_DMA_FQ) ||
	    domain->geometry.aperture_start != 0x200000000ULL ||
	    domain->geometry.aperture_end != 0x2ffffffffULL)
		return dev_err_probe(dev, -EINVAL, "Expected translated CAM 8-12 GiB domain\n");
	larb = (fwspec->ids[0] & 0xffff) >> 5;
	for (i = 0; i < fwspec->num_ids; i++)
		if ((fwspec->ids[i] >> 16) != 2 ||
		    !cannon_camera_port(fwspec->ids[i] & 0xffff) ||
		    ((fwspec->ids[i] & 0xffff) >> 5) != larb)
			return dev_err_probe(dev, -EINVAL, "Unexpected camera IOMMU port\n");
	if (fwspec->num_ids != (larb == 13 ? 9 : 17))
		return dev_err_probe(dev, -EINVAL, "Incomplete camera IOMMU ports\n");
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(34));
	if (ret)
		return ret;
	if (!dev->dma_parms)
		return dev_err_probe(dev, -EINVAL, "Platform DMA parameters are missing\n");
	dma_set_max_seg_size(dev, UINT_MAX);
	mutex_lock(&cannon_debug_lock);
	if (cannon_dma_devs[larb]) {
		mutex_unlock(&cannon_debug_lock);
		return -EBUSY;
	}
	cannon_dma_devs[larb] = dev;
	mutex_unlock(&cannon_debug_lock);
	dev_info(dev, "cannon-ion: real CAM DMA domain ready, %u ports\n", fwspec->num_ids);
	return 0;
}

static void cannon_dma_remove(struct platform_device *pdev)
{
	unsigned int i;

	mutex_lock(&cannon_debug_lock);
	for (i = 0; i < ARRAY_SIZE(cannon_dma_devs); i++)
		if (cannon_dma_devs[i] == &pdev->dev)
			cannon_dma_devs[i] = NULL;
	mutex_unlock(&cannon_debug_lock);
}

static ssize_t mapping_state_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cannon_debug *entry;
	unsigned int mapped = 0, buffers = 0;
	u64 bytes = 0;

	mutex_lock(&cannon_debug_lock);
	list_for_each_entry(entry, &cannon_debug_list, list) {
		buffers++;
		if (entry->table) {
			mapped++;
			bytes += entry->buffer->size;
		}
	}
	mutex_unlock(&cannon_debug_lock);
	return sysfs_emit(buf, "mapped=%u bytes=%llu metadata=%u\n", mapped, bytes, buffers);
}
static DEVICE_ATTR_RO(mapping_state);
static struct attribute *cannon_dma_attrs[] = { &dev_attr_mapping_state.attr, NULL };
ATTRIBUTE_GROUPS(cannon_dma);

static const struct of_device_id cannon_dma_match[] = {
	{ .compatible = "xiaomi,cannon-camera-dma" }, {}
};
MODULE_DEVICE_TABLE(of, cannon_dma_match);
static struct platform_driver cannon_dma_driver = {
	.probe = cannon_dma_probe,
	.remove = cannon_dma_remove,
	.driver = {
		.name = "cannon-camera-dma",
		.of_match_table = cannon_dma_match,
		.suppress_bind_attrs = true,
		.dev_groups = cannon_dma_groups,
	},
};

/* Caller holds the metadata mutex. This retains the actual DMA attachment
 * until all handles, fds, mappings and kernel buffer references are gone.
 */
static int cannon_map_camera(struct cannon_debug *entry, u32 port)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	struct scatterlist *sg, *last;
	struct iommu_domain *domain;
	struct device *dma_dev = cannon_dma_devs[port >> 5];
	dma_addr_t base;
	u64 length = 0;
	unsigned int i;
	int ret = -EOPNOTSUPP;

	if (!dma_dev)
		return -ENODEV;
	if (entry->table)
		return iommu_get_domain_for_dev(entry->attachment->dev) ==
		       iommu_get_domain_for_dev(dma_dev) ? 0 : -EXDEV;
	attachment = dma_buf_attach(entry->buffer, dma_dev);
	if (IS_ERR(attachment))
		return PTR_ERR(attachment);
	table = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(table)) {
		ret = PTR_ERR(table);
		goto detach;
	}
	base = sg_dma_address(table->sgl);
	for_each_sg(table->sgl, sg, table->nents, i) {
		if (sg_dma_address(sg) != base + length)
			goto unmap;
		length += sg_dma_len(sg);
	}
	if (length < entry->buffer->size || base < 0x200000000ULL ||
	    base > 0x2ffffffffULL || length > 0x300000000ULL - base)
		goto unmap;
	/* Verify the endpoints against the real translation table before
	 * exposing any address to the old HAL. No physical address fallback.
	 */
	domain = iommu_get_domain_for_dev(dma_dev);
	last = sg_last(table->sgl, table->orig_nents);
	if (!domain ||
	    iommu_iova_to_phys(domain, base) != sg_phys(table->sgl) ||
	    iommu_iova_to_phys(domain, base + entry->buffer->size - 1) !=
		sg_phys(last) + last->length - 1) {
		ret = -EIO;
		goto unmap;
	}
	/* A DMA-BUF fd may outlive every /dev/ion fd. Keep this module's
	 * mapper alive until the last external DMA-BUF reference is gone.
	 * Module exit synchronizes the collector before freeing its text.
	 */
	__module_get(THIS_MODULE);
	entry->attachment = attachment;
	entry->table = table;
	entry->iova = base;
	dev_info_ratelimited(dma_dev, "mapped CAM port=%#x iova=%pad size=%zu\n",
			     port, &entry->iova, entry->buffer->size);
	return 0;
unmap:
	dma_buf_unmap_attachment(attachment, table, DMA_BIDIRECTIONAL);
detach:
	dma_buf_detach(entry->buffer, attachment);
	return ret;
}

static long custom_iova(struct cannon_client *client, void __user *payload, bool compat)
{
	struct cannon_handle *handle;
	struct cannon_debug *entry;
	u8 __user *input = (u8 __user *)payload + 8;
	u8 __user *output = (u8 __user *)payload + (compat ? 32 : 40);
	/* Nested handle union: native 8 bytes, compat 4 bytes. */
	u32 params[5];
	s32 id;
	u64 iova, length;
	long ret;

	if (get_user(id, (s32 __user *)input) ||
	    copy_from_user(params, input + (compat ? 4 : 8), sizeof(params)))
		return -EFAULT;
	if (!cannon_camera_port(params[0]) || params[1] || params[2] ||
	    params[3] || params[4]) {
		pr_info_ratelimited("cannon-ion: unsupported IOVA port=%#x security=%u coherent=%u range=%#x..%#x\n",
				    params[0], params[1], params[2], params[3], params[4]);
		return -EOPNOTSUPP;
	}
	handle = idr_find(&client->handles, id);
	if (!handle)
		return -EINVAL;
	if (compat && handle->buffer->size > U32_MAX)
		return -EOVERFLOW;
	mutex_lock(&cannon_debug_lock);
	entry = cannon_debug_get(handle->buffer);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto out;
	}
	ret = cannon_map_camera(entry, params[0]);
	if (ret)
		goto out;
	iova = entry->iova;
	length = entry->buffer->size;
out:
	if (!list_empty(&cannon_debug_list))
		schedule_delayed_work(&cannon_debug_work, msecs_to_jiffies(200));
	mutex_unlock(&cannon_debug_lock);
	if (ret)
		return ret;
	if (copy_to_user(output, &iova, sizeof(iova)))
		return -EFAULT;
	if (compat) {
		u32 narrow_len = length;

		return copy_to_user(output + 8, &narrow_len, sizeof(narrow_len)) ? -EFAULT : 0;
	}
	return copy_to_user(output + 8, &length, sizeof(length)) ? -EFAULT : 0;
}

/* Both unions align to 8 because their other members contain a u64.
 * Only the nested handle union shrinks to 4 bytes in the compat payload.
 */
static long custom_debug(struct cannon_client *client, void __user *payload,
			 bool compat, bool set)
{
	struct cannon_handle *handle;
	struct cannon_debug *entry;
	struct {
		char name[CANNON_ION_NAME_LEN];
		u32 values[4];
	} data = {};
	u8 __user *handle_arg = (u8 __user *)payload + 8;
	u8 __user *debug_arg = handle_arg + (compat ? 4 : 8);
	s32 id;
	long ret = 0;

	if (get_user(id, (s32 __user *)handle_arg))
		return -EFAULT;
	handle = idr_find(&client->handles, id);
	if (!handle)
		return -EINVAL;
	if (set && copy_from_user(&data, debug_arg, sizeof(data)))
		return -EFAULT;
	data.name[sizeof(data.name) - 1] = '\0';

	mutex_lock(&cannon_debug_lock);
	entry = cannon_debug_find(handle->buffer);
	if (set) {
		entry = cannon_debug_get(handle->buffer);
		if (IS_ERR(entry)) {
			ret = PTR_ERR(entry);
			goto out;
		}
		ret = dma_buf_set_name(handle->buffer, data.name);
		if (ret)
			goto out;
		memcpy(entry->name, data.name, sizeof(entry->name));
		memcpy(entry->values, data.values, sizeof(entry->values));
	} else if (entry) {
		memcpy(data.name, entry->name, sizeof(data.name));
		memcpy(data.values, entry->values, sizeof(data.values));
	}
out:
	if (!list_empty(&cannon_debug_list))
		schedule_delayed_work(&cannon_debug_work, msecs_to_jiffies(200));
	mutex_unlock(&cannon_debug_lock);
	if (!ret && !set && copy_to_user(debug_arg, &data, sizeof(data)))
		ret = -EFAULT;
	return ret;
}

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
	if (client->name[0])
		dma_buf_set_name(buffer, client->name);
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

static long custom(struct cannon_client *client, void __user *arg, bool compat)
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
	if (command == 0 && subcommand == 4) { /* ION_SYS_SET_CLIENT_NAME */
		char name[CANNON_ION_NAME_LEN];

		if (copy_from_user(name, (u8 __user *)payload + 8,
				   sizeof(name)))
			return -EFAULT;
		name[sizeof(name) - 1] = '\0';
		memcpy(client->name, name, sizeof(client->name));
		return 0;
	}
	if (command == 1 && subcommand == 8)
		return custom_iova(client, payload, compat);
	if (command == 1 && (subcommand == 1 || subcommand == 2))
		return custom_debug(client, payload, compat, subcommand == 1);
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
		result = in_compat_syscall() ? -ENOTTY : custom(client, arg, false);
		break;
	case CION_CUSTOM32:
		result = in_compat_syscall() ? custom(client, arg, true) : -ENOTTY;
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
	int ret;

	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	pr_info("cannon-ion: legacy DMA heaps; CAM IOVA requires native DMA provider; no secure heaps\n");
	INIT_DELAYED_WORK(&cannon_debug_work, cannon_debug_collect);
	ret = misc_register(&ion_device);
	if (ret)
		return ret;
	ret = platform_driver_register(&cannon_dma_driver);
	if (ret)
		misc_deregister(&ion_device);
	return ret;
}
module_init(cannon_ion_init);

static void __exit cannon_ion_exit(void)
{
	struct cannon_debug *entry, *next;

	misc_deregister(&ion_device);
	WRITE_ONCE(cannon_debug_stopping, true);
	cancel_delayed_work_sync(&cannon_debug_work);
	list_for_each_entry_safe(entry, next, &cannon_debug_list, list) {
		list_del(&entry->list);
		cannon_debug_free(entry);
	}
	platform_driver_unregister(&cannon_dma_driver);
}
module_exit(cannon_ion_exit);
MODULE_DESCRIPTION("Cannon legacy ION handle compatibility over DMA heaps");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(DMA_BUF);
