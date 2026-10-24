/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Cannon's 4.14 handle-based ION ABI. Not the newer fd-only ION ABI. */
#ifndef _UAPI_CANNON_ION_H
#define _UAPI_CANNON_ION_H
#include <linux/types.h>
#include <linux/ioctl.h>

struct cannon_ion_allocation {
	unsigned long len, align;
	__u32 heap_id_mask, flags;
	__s32 handle;
};
struct cannon_ion_allocation32 {
	__u32 len, align, heap_id_mask, flags;
	__s32 handle;
};
struct cannon_ion_fd { __s32 handle, fd; };
struct cannon_ion_handle { __s32 handle; };
struct cannon_ion_custom { __u32 cmd; unsigned long arg; };
struct cannon_ion_custom32 { __u32 cmd, arg; };
struct cannon_ion_heap {
	char name[32];
	__u32 type, heap_id, reserved0, reserved1, reserved2;
};
struct cannon_ion_query {
	__u32 cnt, reserved0;
	__u64 heaps;
	__u32 reserved1, reserved2;
};
#define CION_ALLOC _IOWR('I', 0, struct cannon_ion_allocation)
#define CION_ALLOC32 _IOWR('I', 0, struct cannon_ion_allocation32)
#define CION_FREE _IOWR('I', 1, struct cannon_ion_handle)
#define CION_MAP _IOWR('I', 2, struct cannon_ion_fd)
#define CION_SHARE _IOWR('I', 4, struct cannon_ion_fd)
#define CION_IMPORT _IOWR('I', 5, struct cannon_ion_fd)
#define CION_CUSTOM _IOWR('I', 6, struct cannon_ion_custom)
#define CION_CUSTOM32 _IOWR('I', 6, struct cannon_ion_custom32)
#define CION_SYNC _IOWR('I', 7, struct cannon_ion_fd)
#define CION_QUERY _IOWR('I', 8, struct cannon_ion_query)
#endif
