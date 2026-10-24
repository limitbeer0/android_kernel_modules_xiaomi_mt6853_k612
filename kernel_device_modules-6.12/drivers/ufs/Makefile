# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_CANNON_BRINGUP),y)
ccflags-y += -I$(srctree)/drivers/ufs/host
ccflags-y += -I$(srctree)/drivers/ufs/core
obj-$(CONFIG_CANNON_UFS_MT6853) += ufs-mt6853.o
else
# SPDX-License-Identifier: GPL-2.0
# UFSHCD makefile
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/blocktag/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/vendor
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/core/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/devfreq/
ccflags-y += -I$(srctree)/kernel/

# obj-$(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK) += vendor/
# obj-$(CONFIG_SCSI_UFS_MEDIATEK_DBG) += ufs-mediatek-dbg.o

# xaga: platform module wiring (alps BUILD.bazel ufs-mediatek-mod; exports
# ufs_mtk_rpmb_get_raw_dev for rpmb-mtk). Module name differs from the main
# source (ufs-mediatek.c) to avoid Kbuild self-aggregation.
obj-m += ufs-mediatek-mod.o
ufs-mediatek-mod-y := ufs-mediatek.o ufs-mediatek-sysfs.o ufs-mediatek-mimic.o \
		ufs-mediatek-mbrain.o
ufs-mediatek-mod-$(CONFIG_MTK_BLOCK_IO_TRACER) += ufs-mediatek-btag.o
ufs-mediatek-mod-$(CONFIG_MTK_RPMB) += ufs-mediatek-rpmb.o ufs-mediatek-ise-dummy.o

# xaga: platform module wiring
obj-m += ufs-mediatek-dbg.o

endif # CONFIG_CANNON_BRINGUP
