# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel system_heap; exports
# dma_buf_get_gid for mediatek-drm)
# cannon: module renamed to mtk_system_heap. The common kernel's built-in
# system heap (CONFIG_DMABUF_HEAPS_SYSTEM=y, kept for the ION compat path)
# occupies the "system_heap" module name, and the staging tool then treats
# this vendor module as builtin — leaving ged.ko without
# mtk_dma_buf_set_name at load time (test59, real device).
ccflags-y += -I$(src)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3/
obj-m += mtk_system_heap.o
mtk_system_heap-y := mtk-deferred-free-helper.o mtk_cache_pool.o \
	mtk_dmabuf_monitor.o mtk_page_pool.o mtk_system_heap.o
