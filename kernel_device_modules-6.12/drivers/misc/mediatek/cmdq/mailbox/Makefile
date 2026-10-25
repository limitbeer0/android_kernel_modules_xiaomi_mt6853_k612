# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2021 MediaTek Inc.
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/iommu/ \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mach/$(MTK_PLATFORM)/include \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mdp/$(MTK_PLATFORM) \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmp/ \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/smi/ \
             -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3 \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv/ \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include/ \
             -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mminfra/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cmdq/virtio

ifeq ($(CONFIG_VHOST_CMDQ),m)
ccflags-y += -I$(KERNEL_SRC)/include/
endif

ifneq ($(CONFIG_VIRTIO_CMDQ),m)
ccflags-$(CONFIG_DEBUG_FS) += -DCONFIG_CMDQ_MMPROFILE_SUPPORT
endif

ifneq ($(filter y m,$(CONFIG_MICROTRUST_TEE_SUPPORT)),)
include $(DEVICE_MODULES_PATH)/drivers/tee/teei/Makefile.include
endif

ifneq ($(filter y m,$(CONFIG_TRUSTONIC_TEE_SUPPORT)),)
include $(DEVICE_MODULES_PATH)/drivers/tee/gud/Makefile.include
endif

ifneq ($(filter y m,$(CONFIG_MTK_GZ_TZ_SYSTEM)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/geniezone/public
ccflags-y += -DCMDQ_SECURE_MTEE_SUPPORT

ccflags-y += -DCMDQ_SECURE_SUPPORT

ccflags-y += -DCMDQ_SECURE_CPR_SUPPORT
endif

#ccflags-y += -DCMDQ_SKIP_BY_CMDQ_BUILT
ccflags-y += -DCMDQ_DCACHE_INVAL
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv_v2

# xaga: platform module wiring (alps BUILD.bazel mtk-cmdq-drv-ext; exports
# cmdq_util_* for cmdq-platform-mt6895 / cmdq_helper_inf / mtk-mminfra-debug)
ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += mtk-cmdq-drv-ext.o
else
obj-m += mtk-cmdq-drv-ext.o
endif
mtk-cmdq-drv-ext-y := mtk-cmdq-mailbox-ext.o mtk-cmdq-helper-ext.o cmdq-util.o
# alps-verbatim: cmdq_util_mbrain_buf_rec_latest declares nsec but never reads it
$(obj)/cmdq-util.o: ccflags-y += -Wno-unused-but-set-variable

# xaga: platform module wiring
ifeq ($(CONFIG_CANNON_BRINGUP),y)
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += cmdq-platform-mt6853.o
obj-$(CONFIG_MTK_CMDQ_MBOX_EXT) += cannon-cmdq-probe.o
else
obj-m += cmdq-platform-mt6895.o
endif
