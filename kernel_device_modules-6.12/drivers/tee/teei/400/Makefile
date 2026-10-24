# Copyright (c) 2015-2020, MICROTRUST Incorporated
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# version 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

subdir-ccflags-y  += -I$(src)/common/include
ccflags-y += -DMTK_ADAPTED
subdir-ccflags-y  += \
	-I$(src)/tz_dcih \
	-I$(src)/public \
	-I$(src)/tz_driver \
	-I$(src)/tz_driver/include
# Bring-up: silence warnings while the 4.14 driver is adapted to 6.12.
subdir-ccflags-y += -w
# Match the 4.14 build: NQ/shared buffers must live in the DMA zone so the
# secure world can access the physical addresses passed via SMC.
subdir-ccflags-y += -D UT_DMA_ZONE

subdir-ccflags-$(CONFIG_MICROTRUST_DEBUG) += -DDEBUG

# TEEI-400: single isee.ko, no FFA support
obj-m += isee.o
isee-y :=              tee/tee_core.o \
                       tee/tee_client_api.o \
                       tee/tee_shm.o \
                       tee/tee_shm_pool.o \
                       tee/soter/core.o \
                       tee/soter/call.o \
                       ut_keymaster/ut_keymaster.o \
                       tz_driver/teei_fp.o \
                       tz_driver/teei_client_transfer_data.o \
                       tz_driver/teei_smc_call.o \
                       tz_driver/sysfs.o \
                       tz_driver/teei_cancel_cmd.o \
                       tz_driver/fdrv.o \
                       tz_driver/notify_queue.o \
                       tz_driver/switch_queue.o \
                       tz_driver/teei_keymaster.o \
                       tz_driver/teei_task_link.o \
                       tz_driver/tz_log.o \
                       tz_driver/irq_register.o \
                       tz_driver/teei_client_main.o \
                       tz_driver/backward_driver.o \
                       tz_vfs/fp_vendor.o \
                       tz_vfs/vfsFun.o \
                       tz_dcih/tz_dcih.o

isee-y +=              teei_fp/fp_func.o

ifeq ($(filter y m, $(CONFIG_MICROTRUST_TEST_DRIVERS)),y)
isee-y +=              tee/capi_proxy.o \
                       tests/main.o \
                       tests/xtest_isee_1000.o \
                       tests/xtest_isee_3000.o \
                       tests/xtest_main.o

subdir-ccflags-y  += -DTEEI_TEST_DRIVERS
endif

ifneq ($(filter y m, $(CONFIG_MICROTRUST_UNITTEST_SUPPORT)),)
isee-y +=              ut_tester/ut_tester.o
subdir-ccflags-y  += -DTEEI_UNITTEST_SUPPORT
endif
