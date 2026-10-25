# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mtk-interconnect/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/slbc/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cmdq/mailbox/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/iommu/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mml
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/dpc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/leds/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tinysys_scmi/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek

ifneq ($(filter y m, $(CONFIG_MTK_MME_SUPPORT)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mme
endif

obj-$(CONFIG_DRM_PANEL_NT37801_MSYNC2_RTE) += panel-nt37801-msync2-rte.o
obj-$(CONFIG_DRM_PANEL_BOE_TV106C9MLL0) += ocp2138_i2c.o
obj-$(CONFIG_DRM_PANEL_MAXIAM_MAX96851) += panel-maxiam-max96851.o
obj-$(CONFIG_DRM_PANEL_AC160_P_3_A0004_CMD_PANEL) += AC160_P_3_A0004_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_AC160_P_7_A0001_CMD_PANEL) += AC160_P_7_A0001_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_AC124_P_3_A0004_CMD_PANEL) += ac124_p_3_a0004_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_AC124_P_B_A0012_CMD_PANEL) += ac124_p_b_a0012_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_L16_42_02_0A_DSC_VDO) += panel-l16-42-02-0a-dsc-vdo.o
obj-$(CONFIG_DRM_PANEL_L16_36_02_0B_DSC_VDO) += panel-l16-36-02-0b-dsc-vdo.o
obj-$(CONFIG_DRM_PANEL_LEDS_KTZ8863A) += leds-ktz8863a.o
obj-$(CONFIG_DRM_PANEL_LEDS_KTZ8863A) += leds-ktz8863a.o

# cannon: j22 panels and LM36273 backlight, ported from 4.14.
obj-$(CONFIG_DRM_PANEL_J22_42_02_0A_VDO) += panel_j22_42_02_0a_vdo.o
obj-$(CONFIG_DRM_PANEL_J22_43_03_0B_VDO) += panel_j22_43_03_0b_vdo.o
obj-$(CONFIG_DRM_PANEL_J22_43_03_0C_VDO) += panel_j22_43_03_0c_vdo.o
obj-$(CONFIG_DRM_PANEL_LEDS_LM36273) += leds-lm36273.o

ccflags-$(CONFIG_DRM_MEDIATEK_PANEL) += -I$(DEVICE_MODULES_PATH)/drivers/leds/

# xaga: provides the __weak mtk_drm_gateic_set_backlight that leds-mtk-disp
# may call (alps ships the file unbuilt; MTK_COMMON_LCM_DRV path).
# Composite module name must DIFFER from the source objects (Kbuild would
# otherwise hit a circular dep mtk_drm_gateic.o <- mtk_drm_gateic.o and never
# compile the main file, leaving mtk_drm_gateic_register /
# mtk_gateic_match_lcm_list as unresolved symbols at insmod). "mediatek-drm-
# gateic" matches the official 5.10 (xagaforge) name and alps mgk_64.bzl.
# cannon: skipped. The j22 panels are direct mipi_dsi/drm_panel drivers and
# need neither the gateic composite nor the LCM panel-drv framework; the
# gateic sources also assume CONFIG_DRM_MEDIATEK_PANEL externs instead of the
# __weak fallbacks.
ifneq ($(CONFIG_CANNON_BRINGUP),y)
obj-m += mediatek-drm-gateic.o
mediatek-drm-gateic-y := mtk_drm_panel_i2c.o \
	mtk_drm_gateic_rt4801h.o \
	mtk_drm_gateic_rt4831a.o \
	mtk_drm_gateic.o

# xaga: MTK LCM framework driver - registers the dsi/dpi/dbi panel drivers
# (mtk_drm_panel_dsi_driver etc.) that panel-* lcm modules hook into. Without
# it the DSI host never finds a panel -> mediatek-drm components never bind ->
# no drm dev -> recovery "timeout wait for graphics" -> hang -> AP_WDT reboot
# loop (real-device 2026-08-12). alps builds it on mt6895 (removed only for
# mt6768/mt6893/mt6765); module name matches alps mgk_64.bzl.
obj-m += mediatek-drm-panel-drv.o
mediatek-drm-panel-drv-y := mtk_drm_panel_drv.o \
	mtk_drm_panel_drv_dsi.o \
	mtk_drm_panel_drv_dpi.o \
	mtk_drm_panel_drv_dbi.o \
	mtk_drm_panel_helper.o \
	mtk_drm_panel_helper_dsi.o \
	mtk_drm_panel_helper_dpi.o \
	mtk_drm_panel_helper_dbi.o \
	mtk_round_corner/mtk_drm_rc.o
endif
