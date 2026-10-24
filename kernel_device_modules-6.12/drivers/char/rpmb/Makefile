ccflags-y += -I$(DEVICE_MODULES_PATH)/include
ifneq ($(findstring $(CONFIG_TRUSTONIC_TEE_SUPPORT), y m),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/char/rpmb/drrpmb_gp/public
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/tee/gud/$(CONFIG_TRUSTONIC_TEE_VERSION)/MobiCoreDriver/public/
endif
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/vendor

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/mmc/host
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/mmc/core
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/mmc/host
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/mmc/core

obj-$(CONFIG_RPMB_SIM) += rpmb_sim.o

# xaga: platform module wiring (alps BUILD.bazel rpmb; exports rpmb_mtk_cmd_req
# for rpmb-mtk)
# cannon: core.c calls rpmb_cdev_* which live in cdev.c, so link both into one
# module (the stock build kept cdev out of the module list by mistake).
obj-m += rpmb-mtk-core.o
rpmb-mtk-core-y := core.o cdev.o
rpmb-mtk-core-$(CONFIG_CANNON_BRINGUP) += cannon-legacy.o

ccflags-y += -D__CHECK_ENDIAN

# cannon-legacy implements Microtrust 400 ioctl 5..8 on /dev/rpmb0 through
# the MT6853 UFS provider. The full newer-platform rpmb-mtk driver stays out.
# obj-m += rpmb-mtk.o
