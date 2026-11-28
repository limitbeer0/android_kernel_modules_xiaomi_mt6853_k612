/* SPDX-License-Identifier: GPL-2.0-only */
/* Keep real DMA-BUF mappings for the complete legacy DIP buffer lifetime. */
struct cannon_dip_map {
	struct list_head list;
	pid_t owner;
	u32 fd;
	struct dma_buf *buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	dma_addr_t address;
};
static LIST_HEAD(cannon_dip_maps);
static DEFINE_MUTEX(cannon_dip_map_lock);
static unsigned int cannon_dip_map_count;
module_param_named(mapped_buffers, cannon_dip_map_count, uint, 0444);

static void cannon_dip_unmap(struct cannon_dip_map *map)
{
	dma_buf_unmap_attachment_unlocked(map->attachment, map->table, DMA_BIDIRECTIONAL);
	dma_buf_detach(map->buf, map->attachment);
	dma_buf_put(map->buf);
	list_del(&map->list);
	kfree(map);
	cannon_dip_map_count--;
}

static void cannon_dip_release_maps(void)
{
	struct cannon_dip_map *map, *next;

	mutex_lock(&cannon_dip_map_lock);
	list_for_each_entry_safe(map, next, &cannon_dip_maps, list)
		cannon_dip_unmap(map);
	mutex_unlock(&cannon_dip_map_lock);
}

static long cannon_dip_dma_ioctl(unsigned int cmd, void __user *arg)
{
	struct DIP_ION_MEM_INFO info;
	struct cannon_dip_map *map;
	struct dma_buf *buf;
	struct scatterlist *sg;
	dma_addr_t end;
	size_t length;
	u32 fd;
	int i, ret = 0;

	if (cmd == DIP_DET_BUF_FD) {
		if (get_user(fd, (u32 __user *)arg))
			return -EFAULT;
		mutex_lock(&cannon_dip_map_lock);
		list_for_each_entry(map, &cannon_dip_maps, list)
			if (map->owner == task_tgid_nr(current) && map->fd == fd) {
				cannon_dip_unmap(map);
				goto detached;
			}
		ret = -ENOENT;
detached:
		mutex_unlock(&cannon_dip_map_lock);
		return ret;
	}
	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;
	if (info.check_flag > 1 || info.buf_fd > INT_MAX)
		return -EINVAL;
	buf = dma_buf_get(info.buf_fd);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	mutex_lock(&cannon_dip_map_lock);
	list_for_each_entry(map, &cannon_dip_maps, list) {
		if (map->owner != task_tgid_nr(current) || map->fd != info.buf_fd)
			continue;
		if (map->buf != buf) {
			ret = -ESTALE;
			goto put;
		}
		goto found;
	}
	if (info.check_flag) {
		ret = -ENOENT;
		goto put;
	}
	if (cannon_dip_map_count >= 4096) {
		ret = -ENOSPC;
		goto put;
	}
	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map) {
		ret = -ENOMEM;
		goto put;
	}
	map->buf = buf;
	map->attachment = dma_buf_attach(buf, dip_devs->dev);
	if (IS_ERR(map->attachment)) {
		ret = PTR_ERR(map->attachment);
		goto free;
	}
	map->table = dma_buf_map_attachment_unlocked(map->attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(map->table)) {
		ret = PTR_ERR(map->table);
		goto detach;
	}
	map->address = sg_dma_address(map->table->sgl);
	length = 0;
	for_each_sgtable_dma_sg(map->table, sg, i) {
		if (sg_dma_address(sg) != map->address + length) {
			ret = -ERANGE;
			goto unmap;
		}
		length += sg_dma_len(sg);
	}
	/* Hardware carries the fixed CAM high address bits separately. */
	end = map->address + buf->size;
	if (length < buf->size || map->address < 0x200000000ULL ||
	    end > 0x300000000ULL || end <= map->address ||
	    info.buf_offset >= buf->size) {
		ret = -ERANGE;
		goto unmap;
	}
	map->owner = task_tgid_nr(current);
	map->fd = info.buf_fd;
	list_add_tail(&map->list, &cannon_dip_maps);
	cannon_dip_map_count++;
	/* The new entry owns the original reference. */
	get_dma_buf(buf);
found:
	if (info.buf_offset >= buf->size) {
		ret = -EINVAL;
		goto put;
	}
	/* Match the original UAPI: buf_pa is the base, offset remains separate. */
	info.buf_pa = lower_32_bits(map->address);
	ret = copy_to_user(arg, &info, sizeof(info)) ? -EFAULT : 0;
	goto put;
unmap:
	dma_buf_unmap_attachment_unlocked(map->attachment, map->table, DMA_BIDIRECTIONAL);
detach:
	dma_buf_detach(buf, map->attachment);
free:
	kfree(map);
put:
	mutex_unlock(&cannon_dip_map_lock);
	dma_buf_put(buf);
	return ret;
}
