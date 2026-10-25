// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include "mali_kbase_config_platform.h"
#include "platform/mtk_platform_common.h"
#include <ged_dvfs.h>
#include <mtk_gpufreq.h>
#include <mtk_gpu_utility.h>
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
#include <mboot_params.h>
#endif
#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)
#include <mtk_gpu_power_sspm_ipi.h>
#endif

#ifndef POWER_ON
#define POWER_ON GPU_PWR_ON
#endif
#ifndef POWER_OFF
#define POWER_OFF GPU_PWR_OFF
#endif

static inline void gpufreq_set_timestamp(void)
{
	/* Valhall CSF on MT6895 handles timestamps via internal hardware CSF timers */
}

static inline void gpufreq_check_bus_idle(void)
{
	/* Bus idle checking on MT6895 is handled by MTCMOS/SPM inside gpufreq_power_control */
}

DEFINE_MUTEX(g_mfg_lock);
static int g_cur_opp_idx;
static bool mfg_qchannel_enable;
module_param(mfg_qchannel_enable, bool, 0644);
MODULE_PARM_DESC(mfg_qchannel_enable, "Enable MFG_ACTIVE_SEL bit0 in MFG_QCHANNEL_CON (0x13FBF0B4)");

static bool mali_enable_pdca = true;
module_param(mali_enable_pdca, bool, 0644);
MODULE_PARM_DESC(mali_enable_pdca, "Enable MediaTek PDCv2 active power control in MFG_TOP_CONFIG (0x13FBF000)");

static bool mali_enable_pll_clocks = true;
module_param(mali_enable_pll_clocks, bool, 0644);
MODULE_PARM_DESC(mali_enable_pll_clocks, "Enable MFGPLL and MFGSCPLL and switch CLK_CFG_30 muxes (Run 78)");

bool mali_skip_mcu_on_clk_fail = true;
module_param(mali_skip_mcu_on_clk_fail, bool, 0644);
MODULE_PARM_DESC(mali_skip_mcu_on_clk_fail, "Refuse to boot MCU if PLLs or muxes are not verified ready");

static uint mali_hw_seq_mask = 7;
module_param(mali_hw_seq_mask, uint, 0644);
MODULE_PARM_DESC(mali_hw_seq_mask, "Bitmask for stock GPU power-on sequences: bit0=I2M, bit1=DCM, bit2=ACP (default 7)");

static bool g_mfg_clocks_enabled = false;

struct mt6895_gpu_clocks {
	struct clk *bg3d;          /* mfgcfg_bg3d */
	struct clk *pll_main;      /* mfg_ao_mfgpll */
	struct clk *pll_sc;        /* mfgsc_ao_mfgscpll */
	struct clk *mux_0;         /* mfg_sel_0 */
	struct clk *mux_1;         /* mfg_sel_1 */
	struct clk *parent_main;   /* mfgpll_ck */
	struct clk *parent_sc;     /* mfgscpll_ck */
	struct clk *parent_sub;    /* mfg_ref_ck */
	struct clk *parent_sc_sub; /* mfgsc_ref_ck */
};
static struct mt6895_gpu_clocks g_gpu_clks;
static bool g_gpu_clks_acquired = false;

static void mt6895_acquire_clocks(struct kbase_device *kbdev)
{
	if (g_gpu_clks_acquired)
		return;

	g_gpu_clks.bg3d = devm_clk_get_optional(kbdev->dev, "mfgcfg_bg3d");
	g_gpu_clks.pll_main = devm_clk_get_optional(kbdev->dev, "mfg_ao_mfgpll");
	if (IS_ERR_OR_NULL(g_gpu_clks.pll_main))
		g_gpu_clks.pll_main = clk_get(NULL, "mfg_ao_mfgpll");

	g_gpu_clks.pll_sc = devm_clk_get_optional(kbdev->dev, "mfgsc_ao_mfgscpll");
	if (IS_ERR_OR_NULL(g_gpu_clks.pll_sc))
		g_gpu_clks.pll_sc = clk_get(NULL, "mfgsc_ao_mfgscpll");

	g_gpu_clks.mux_0 = devm_clk_get_optional(kbdev->dev, "mfg_sel_0");
	g_gpu_clks.mux_1 = devm_clk_get_optional(kbdev->dev, "mfg_sel_1");

	g_gpu_clks.parent_main = devm_clk_get_optional(kbdev->dev, "mfgpll_ck");
	if (IS_ERR_OR_NULL(g_gpu_clks.parent_main))
		g_gpu_clks.parent_main = clk_get(NULL, "mfgpll_ck");

	g_gpu_clks.parent_sc = devm_clk_get_optional(kbdev->dev, "mfgscpll_ck");
	if (IS_ERR_OR_NULL(g_gpu_clks.parent_sc))
		g_gpu_clks.parent_sc = clk_get(NULL, "mfgscpll_ck");

	g_gpu_clks.parent_sub = devm_clk_get_optional(kbdev->dev, "mfg_ref_ck");
	if (IS_ERR_OR_NULL(g_gpu_clks.parent_sub))
		g_gpu_clks.parent_sub = clk_get(NULL, "mfg_ref_ck");

	g_gpu_clks.parent_sc_sub = devm_clk_get_optional(kbdev->dev, "mfgsc_ref_ck");
	if (IS_ERR_OR_NULL(g_gpu_clks.parent_sc_sub))
		g_gpu_clks.parent_sc_sub = clk_get(NULL, "mfgsc_ref_ck");

	dev_info(kbdev->dev,
		"[CSF CLK ACQUIRE] bg3d=%p pll_main=%p pll_sc=%p mux0=%p mux1=%p parent_main=%p parent_sc=%p sub0=%p sub1=%p\n",
		g_gpu_clks.bg3d, g_gpu_clks.pll_main, g_gpu_clks.pll_sc,
		g_gpu_clks.mux_0, g_gpu_clks.mux_1,
		g_gpu_clks.parent_main, g_gpu_clks.parent_sc,
		g_gpu_clks.parent_sub, g_gpu_clks.parent_sc_sub);

	g_gpu_clks_acquired = true;
}

static unsigned int mt6895_get_fmeter(void __iomem *con0_addr, void __iomem *con1_addr)
{
	u32 val = 0, ckgen_load_cnt = 0, ckgen_k1 = 0;
	int i = 0;
	unsigned int freq = 0;

	writel(0x00FF0000, con1_addr);
	val = readl(con0_addr);
	writel((val & 0x00FFFFFF), con0_addr);
	writel(0x00009000, con0_addr);

	ckgen_load_cnt = readl(con1_addr) >> 16;
	ckgen_k1 = readl(con0_addr) >> 24;

	val = readl(con0_addr);
	writel((val | 0x1010), con0_addr);

	/* wait fmeter finish */
	while (readl(con0_addr) & 0x10) {
		udelay(10);
		i++;
		if (i > 1000) {
			pr_err("[CSF CLK FMETER] wait Fmeter timeout\n");
			break;
		}
	}

	val = readl(con1_addr) & 0xFFFF;
	/* KHz */
	freq = (val * 26000 * (ckgen_k1 + 1)) / (ckgen_load_cnt + 1);
	return freq;
}

static unsigned int mt6895_calc_pll_rate(void __iomem *con1_addr)
{
	u32 mfgpll = readl(con1_addr);
	u32 pcw = mfgpll & 0x3FFFFF;
	u32 posdiv_power = (mfgpll & (0x7 << 24)) >> 24;

	return (((pcw * 10 + 5) * 26) >> 14) / (1 << posdiv_power) * 100;
}

static int mt6895_power_on_clocks(struct kbase_device *kbdev)
{
	int ret = 0;
	void __iomem *mfg_pll_base;
	void __iomem *topck;
	u32 pll0_con0, pll0_con3, pll1_con0, pll1_con3, clk_cfg_30;
	int i;

	if (g_mfg_clocks_enabled)
		return 0;

	mt6895_acquire_clocks(kbdev);

	if (!mali_enable_pll_clocks) {
		dev_info(kbdev->dev, "[CSF CLK] PLL activation skipped (mali_enable_pll_clocks=0)\n");
		return 0;
	}

	dev_info(kbdev->dev, "[CSF CLK] Starting stock-ordered GPU clock power-on sequence...\n");

	/* Stock Step 1: Enable BG3D clock gate */
	if (!IS_ERR_OR_NULL(g_gpu_clks.bg3d)) {
		ret = clk_prepare_enable(g_gpu_clks.bg3d);
		dev_info(kbdev->dev, "[CSF CLK] clk_prepare_enable(bg3d) = %d\n", ret);
	}

	/* Stock Step 2: Enable MFGPLL via CCF */
	if (!IS_ERR_OR_NULL(g_gpu_clks.pll_main)) {
		ret = clk_prepare_enable(g_gpu_clks.pll_main);
		dev_info(kbdev->dev, "[CSF CLK] clk_prepare_enable(pll_main) = %d\n", ret);
	}

	/* Stock Step 3: Enable MFGSCPLL via CCF */
	if (!IS_ERR_OR_NULL(g_gpu_clks.pll_sc)) {
		ret = clk_prepare_enable(g_gpu_clks.pll_sc);
		dev_info(kbdev->dev, "[CSF CLK] clk_prepare_enable(pll_sc) = %d\n", ret);
	}

	/* Stock Step 4: Hardware verification & assert fallback for PLLs */
	mfg_pll_base = ioremap(0x13fa0000, 0x1000);
	if (mfg_pll_base) {
		/* MFGPLL_CON0 at 0x8, MFGPLL_CON3 at 0x14 */
		pll0_con0 = readl(mfg_pll_base + 0x8);
		pll0_con3 = readl(mfg_pll_base + 0x14);
		/* MFGSCPLL_CON0 at 0xc08, MFGSCPLL_CON3 at 0xc14 */
		pll1_con0 = readl(mfg_pll_base + 0xc08);
		pll1_con3 = readl(mfg_pll_base + 0xc14);

		dev_info(kbdev->dev,
			"[CSF CLK RAW] PRE: MFGPLL CON0=0x%08x CON3=0x%08x | MFGSC CON0=0x%08x CON3=0x%08x\n",
			pll0_con0, pll0_con3, pll1_con0, pll1_con3);

		/* If bit 0 of CON0 is not set, assert hardware enable directly */
		if (!(pll0_con0 & BIT(0))) {
			dev_info(kbdev->dev, "[CSF CLK HW] Asserting MFGPLL hardware enable\n");
			writel(readl(mfg_pll_base + 0x14) | BIT(0), mfg_pll_base + 0x14); /* PWR_ON */
			udelay(1);
			writel(readl(mfg_pll_base + 0x14) & ~BIT(1), mfg_pll_base + 0x14); /* Clear ISO */
			udelay(1);
			writel(readl(mfg_pll_base + 0x8) | BIT(0), mfg_pll_base + 0x8); /* EN */
		}

		if (!(pll1_con0 & BIT(0))) {
			dev_info(kbdev->dev, "[CSF CLK HW] Asserting MFGSCPLL hardware enable\n");
			writel(readl(mfg_pll_base + 0xc14) | BIT(0), mfg_pll_base + 0xc14); /* PWR_ON */
			udelay(1);
			writel(readl(mfg_pll_base + 0xc14) & ~BIT(1), mfg_pll_base + 0xc14); /* Clear ISO */
			udelay(1);
			writel(readl(mfg_pll_base + 0xc08) | BIT(0), mfg_pll_base + 0xc08); /* EN */
		}

		/* Poll for lock / analog settle (up to 1 ms) */
		for (i = 0; i < 20; i++) {
			udelay(50);
			pll0_con0 = readl(mfg_pll_base + 0x8);
			pll1_con0 = readl(mfg_pll_base + 0xc08);
			if ((pll0_con0 & BIT(0)) && (pll1_con0 & BIT(0)))
				break;
		}

		pll0_con3 = readl(mfg_pll_base + 0x14);
		pll1_con3 = readl(mfg_pll_base + 0xc14);
		dev_info(kbdev->dev,
			"[CSF CLK RAW] POST: MFGPLL CON0=0x%08x CON3=0x%08x | MFGSC CON0=0x%08x CON3=0x%08x (loops=%d)\n",
			pll0_con0, pll0_con3, pll1_con0, pll1_con3, i);

		iounmap(mfg_pll_base);
	}

	/* Stock Step 5: Switch Muxes via CCF clk_set_parent natively */
	if (!IS_ERR_OR_NULL(g_gpu_clks.mux_0) && !IS_ERR_OR_NULL(g_gpu_clks.parent_main)) {
		ret = clk_set_parent(g_gpu_clks.mux_0, g_gpu_clks.parent_main);
		dev_info(kbdev->dev, "[CSF CLK] clk_set_parent(mux_0 -> mfgpll_ck) = %d\n", ret);
		if (ret) {
			dev_err(kbdev->dev, "[CSF CLK ERROR] clk_set_parent(mux_0) failed: %d\n", ret);
			if (mali_skip_mcu_on_clk_fail)
				return ret;
		}
	}
	if (!IS_ERR_OR_NULL(g_gpu_clks.mux_1) && !IS_ERR_OR_NULL(g_gpu_clks.parent_sc)) {
		ret = clk_set_parent(g_gpu_clks.mux_1, g_gpu_clks.parent_sc);
		dev_info(kbdev->dev, "[CSF CLK] clk_set_parent(mux_1 -> mfgscpll_ck) = %d\n", ret);
		if (ret) {
			dev_err(kbdev->dev, "[CSF CLK ERROR] clk_set_parent(mux_1) failed: %d\n", ret);
			if (mali_skip_mcu_on_clk_fail)
				return ret;
		}
	}

	/* Stock Step 6: Verify CLK_CFG_30 (Read-only verification of CCF latch, no raw SET writes!) */
	topck = ioremap(0x10000000, 0x1000);
	if (topck) {
		clk_cfg_30 = readl(topck + 0x1f0);
		dev_info(kbdev->dev,
			"[CSF CLK] CLK_CFG_30 read-back: 0x%08x (bits 16:17 = %u:%u)\n",
			clk_cfg_30, (clk_cfg_30 >> 16) & 1, (clk_cfg_30 >> 17) & 1);

		if (!((clk_cfg_30 >> 16) & 1) || !((clk_cfg_30 >> 17) & 1)) {
			dev_err(kbdev->dev, "[CSF CLK ERROR] CLK_CFG_30 bits 16:17 != 1:1 after CCF parent switch!\n");
			if (mali_skip_mcu_on_clk_fail) {
				iounmap(topck);
				return -EIO;
			}
		}
		iounmap(topck);
	}

	/* Log CCF post-switch rates */
	if (!IS_ERR_OR_NULL(g_gpu_clks.mux_0) && !IS_ERR_OR_NULL(g_gpu_clks.mux_1)) {
		dev_info(kbdev->dev,
			"[CSF CLK CCF] mux_0: rate=%lu Hz | mux_1: rate=%lu Hz\n",
			clk_get_rate(g_gpu_clks.mux_0),
			clk_get_rate(g_gpu_clks.mux_1));
	}

	/* Stock Step 7: Measure Physical Hardware Frequencies via Fmeter & CON1 */
	mfg_pll_base = ioremap(0x13fa0000, 0x1000);
	if (mfg_pll_base) {
		u32 fgpu_fmeter = mt6895_get_fmeter(mfg_pll_base + 0x040, mfg_pll_base + 0x044);
		u32 fstack_fmeter = mt6895_get_fmeter(mfg_pll_base + 0xc40, mfg_pll_base + 0xc44);
		u32 fgpu_calc = mt6895_calc_pll_rate(mfg_pll_base + 0x00c);
		u32 fstack_calc = mt6895_calc_pll_rate(mfg_pll_base + 0xc0c);

		dev_info(kbdev->dev,
			"[CSF CLK FMETER] MFGPLL: calc=%u kHz, fmeter=%u kHz | MFGSCPLL: calc=%u kHz, fmeter=%u kHz\n",
			fgpu_calc, fgpu_fmeter, fstack_calc, fstack_fmeter);
		iounmap(mfg_pll_base);
	}

	udelay(50);
	g_mfg_clocks_enabled = true;
	dev_info(kbdev->dev, "[CSF CLK] GPU clock power-on sequence completed successfully!\n");
	return 0;
}

static void mt6895_power_off_clocks(struct kbase_device *kbdev)
{
	void __iomem *topck;
	u32 clk_cfg_30;

	if (!g_mfg_clocks_enabled)
		return;

	dev_info(kbdev->dev, "[CSF CLK] Starting symmetric GPU clock power-off sequence...\n");

	/* Step 1: Switch CCF parents back to ref first */
	if (!IS_ERR_OR_NULL(g_gpu_clks.mux_0) && !IS_ERR_OR_NULL(g_gpu_clks.parent_sub)) {
		int ret = clk_set_parent(g_gpu_clks.mux_0, g_gpu_clks.parent_sub);
		if (ret)
			dev_warn(kbdev->dev, "[CSF CLK] clk_set_parent(mux_0 -> ref) returned %d\n", ret);
	}
	if (!IS_ERR_OR_NULL(g_gpu_clks.mux_1) && !IS_ERR_OR_NULL(g_gpu_clks.parent_sc_sub)) {
		int ret = clk_set_parent(g_gpu_clks.mux_1, g_gpu_clks.parent_sc_sub);
		if (ret)
			dev_warn(kbdev->dev, "[CSF CLK] clk_set_parent(mux_1 -> ref) returned %d\n", ret);
	}

	/* Step 2: Read-back CLK_CFG_30 to verify switch to ref */
	topck = ioremap(0x10000000, 0x1000);
	if (topck) {
		clk_cfg_30 = readl(topck + 0x1f0);
		dev_info(kbdev->dev,
			"[CSF CLK] CLK_CFG_30 after switch to ref: 0x%08x (bits 16:17 = %u:%u)\n",
			clk_cfg_30, (clk_cfg_30 >> 16) & 1, (clk_cfg_30 >> 17) & 1);
		iounmap(topck);
	}

	udelay(20);

	/* Step 3: Disable PLLs */
	if (!IS_ERR_OR_NULL(g_gpu_clks.pll_sc))
		clk_disable_unprepare(g_gpu_clks.pll_sc);
	if (!IS_ERR_OR_NULL(g_gpu_clks.pll_main))
		clk_disable_unprepare(g_gpu_clks.pll_main);

	/* Step 4: Disable BG3D clock gate */
	if (!IS_ERR_OR_NULL(g_gpu_clks.bg3d))
		clk_disable_unprepare(g_gpu_clks.bg3d);

	g_mfg_clocks_enabled = false;
	dev_info(kbdev->dev, "[CSF CLK] Symmetric GPU clock power-off completed.\n");
}

static void mt6895_pdca_config(struct kbase_device *kbdev, bool power_on)
{
	void __iomem *mfg_top;
	int i;

	if (!mali_enable_pdca) {
		dev_info(kbdev->dev, "[CSF PDC] PDCv2 config skipped (mali_enable_pdca=0)\n");
		return;
	}

	mfg_top = ioremap(0x13fbf000, 0x1000);
	if (!mfg_top) {
		dev_err(kbdev->dev, "[CSF PDC] Failed to ioremap MFG_TOP_CONFIG (0x13fbf000)\n");
		return;
	}

	if (power_on) {
		/* Step 1: Read-before-write logging (physical stacks 0, 1, 4, 5 on MT6895) */
		dev_info(kbdev->dev,
			"[CSF PDC BEFORE] CG=0x%08x ST0=0x%08x ST1=0x%08x ST4=0x%08x ST5=0x%08x\n",
			readl(mfg_top + 0x100), readl(mfg_top + 0x120), readl(mfg_top + 0x140),
			readl(mfg_top + 0xC0),  readl(mfg_top + 0x98));

		dev_info(kbdev->dev,
			"[CSF PDC BEFORE SC0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x400), readl(mfg_top + 0x418), readl(mfg_top + 0x430),
			readl(mfg_top + 0x448), readl(mfg_top + 0x460), readl(mfg_top + 0x478));

		dev_info(kbdev->dev,
			"[CSF PDC BEFORE RSV0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x404), readl(mfg_top + 0x41C), readl(mfg_top + 0x434),
			readl(mfg_top + 0x44C), readl(mfg_top + 0x464), readl(mfg_top + 0x47C));

		/* Step 2: Vendor sequence for PDCv2 power-on (__gpufreq_pdca_config) */
		/* Shader cores 0..5 active_pwrctl_en (bit 0) - Mali-G610 MC6 has 6 cores */
		for (i = 0; i < 6; i++) {
			u32 ofs = 0x400 + i * 0x18;
			writel(readl(mfg_top + ofs) | BIT(0), mfg_top + ofs);
		}

		/* Clock gating active_pwrctl_en (bit 0) */
		writel(readl(mfg_top + 0x100) | BIT(0), mfg_top + 0x100);

		/* Physical shader stacks st0, st1, st4, st5 active_pwrctl_en (bit 0) */
		writel(readl(mfg_top + 0x120) | BIT(0), mfg_top + 0x120);
		writel(readl(mfg_top + 0x140) | BIT(0), mfg_top + 0x140);
		writel(readl(mfg_top + 0xC0)  | BIT(0), mfg_top + 0xC0);
		writel(readl(mfg_top + 0x98)  | BIT(0), mfg_top + 0x98);

		/* Shader cores 0..5 active_pwrctl_rsv (bit 31) */
		for (i = 0; i < 6; i++) {
			u32 ofs = 0x404 + i * 0x18;
			writel(readl(mfg_top + ofs) | BIT(31), mfg_top + ofs);
		}

		/* Step 3: Read-after-write logging */
		dev_info(kbdev->dev,
			"[CSF PDC AFTER] CG=0x%08x ST0=0x%08x ST1=0x%08x ST4=0x%08x ST5=0x%08x\n",
			readl(mfg_top + 0x100), readl(mfg_top + 0x120), readl(mfg_top + 0x140),
			readl(mfg_top + 0xC0),  readl(mfg_top + 0x98));

		dev_info(kbdev->dev,
			"[CSF PDC AFTER SC0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x400), readl(mfg_top + 0x418), readl(mfg_top + 0x430),
			readl(mfg_top + 0x448), readl(mfg_top + 0x460), readl(mfg_top + 0x478));

		dev_info(kbdev->dev,
			"[CSF PDC AFTER RSV0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x404), readl(mfg_top + 0x41C), readl(mfg_top + 0x434),
			readl(mfg_top + 0x44C), readl(mfg_top + 0x464), readl(mfg_top + 0x47C));
	} else {
		dev_info(kbdev->dev, "[CSF PDC] Preserving PDCv2 active_pwrctl on power off\n");
	}

	iounmap(mfg_top);
}

static void mt6895_apply_stock_hw_sequences(struct kbase_device *kbdev)
{
	void __iomem *mfg_top;
	void __iomem *mfg_rpc;
	u32 val;

	if (!mali_hw_seq_mask) {
		dev_info(kbdev->dev, "[CSF HW-SEQ] Skipping stock hardware sequences (mali_hw_seq_mask=0)\n");
		return;
	}

	mfg_top = ioremap(0x13fbf000, 0x1000);
	if (!mfg_top) {
		dev_err(kbdev->dev, "[CSF HW-SEQ] Failed to ioremap MFG_TOP_CONFIG (0x13fbf000)\n");
		return;
	}

	dev_info(kbdev->dev, "[CSF HW-SEQ] Applying stock sequences with mask=0x%x\n", mali_hw_seq_mask);

	/* Bit 0: I2M Protector sequence (MFG_I2M_PROTECTOR_CFG_00 GPM_ENABLE) */
	if (mali_hw_seq_mask & BIT(0)) {
		dev_info(kbdev->dev, "[CSF HW-SEQ] Executing I2M Protector sequence (bit 0)\n");
		writel(0x20300316, mfg_top + 0xF60);
		writel(0x1800000C, mfg_top + 0xF64);
		writel(0x01010802, mfg_top + 0xF68);
		writel(0x000227F3, mfg_top + 0xFA8);
		udelay(10);
		writel(0x20300317, mfg_top + 0xF60); /* bit 0 = GPM_ENABLE */
		dev_info(kbdev->dev, "[CSF HW-SEQ] I2M 0xF60=0x%08x 0xF64=0x%08x 0xF68=0x%08x 0xFA8=0x%08x\n",
			readl(mfg_top + 0xF60), readl(mfg_top + 0xF64),
			readl(mfg_top + 0xF68), readl(mfg_top + 0xFA8));
	}

	/* Bit 1: DCM sequence (MFG_DCM_CON_0, MFG_GLOBAL_CON, MFG_QCHANNEL_CON, MFG_RPC_AO_CLK_CFG) */
	if (mali_hw_seq_mask & BIT(1)) {
		dev_info(kbdev->dev, "[CSF HW-SEQ] Executing DCM sequence (bit 1)\n");
		/* 0x13FBF010: MFG_DCM_CON_0 (mask 0x3FF7F -> 0xC03F) */
		val = readl(mfg_top + 0x010);
		val = (val & ~0x3FF7F) | 0xC03F;
		writel(val, mfg_top + 0x010);

		/* 0x13FBF024: MFG_ASYNC_CON_1 (bit 0 set) */
		val = readl(mfg_top + 0x024);
		val |= BIT(0);
		writel(val, mfg_top + 0x024);

		/* 0x13FBF0B0: MFG_GLOBAL_CON (clear bits 8, 9, 10, 19) */
		val = readl(mfg_top + 0x0B0);
		val &= ~0x80700;
		writel(val, mfg_top + 0x0B0);

		/* 0x13FBF0B4: MFG_QCHANNEL_CON (set bit 4) */
		val = readl(mfg_top + 0x0B4);
		val |= BIT(4);
		writel(val, mfg_top + 0x0B4);

		/* 0x13F91034: MFG_RPC_AO_CLK_CFG in RPC block (clear bit 0) */
		mfg_rpc = ioremap(0x13f90000, 0x2000);
		if (mfg_rpc) {
			val = readl(mfg_rpc + 0x1034);
			val &= ~BIT(0);
			writel(val, mfg_rpc + 0x1034);
			dev_info(kbdev->dev, "[CSF HW-SEQ] DCM: MFG_RPC_AO_CLK_CFG(0x13F91034)=0x%08x\n",
				readl(mfg_rpc + 0x1034));

			/* Clear MFGRPC AXI bus protect for MFG1 (bits 16..19 and bits 0..3) */
			writel(0x000f000f, mfg_rpc + 0x1044);
			mb();
			udelay(10);
			dev_info(kbdev->dev, "[CSF HW-SEQ] MFGRPC AXI PROT cleared: en=0x%08x rdy=0x%08x\n",
				readl(mfg_rpc + 0x1040), readl(mfg_rpc + 0x1048));

			iounmap(mfg_rpc);
		} else {
			dev_err(kbdev->dev, "[CSF HW-SEQ] Failed to ioremap MFG_RPC (0x13f90000)\n");
		}
	}

	/* Bit 2: ACP sequence (Coherency Port: 0x168, 0x8E0..0x928 from disassembly) */
	if (mali_hw_seq_mask & BIT(2)) {
		dev_info(kbdev->dev, "[CSF HW-SEQ] Executing ACP coherency sequence (bit 2)\n");
		val = readl(mfg_top + 0x168);
		val |= 0xF;
		writel(val, mfg_top + 0x168);

		val = readl(mfg_top + 0x8E0);
		val |= 0x855;
		writel(val, mfg_top + 0x8E0);

		val = readl(mfg_top + 0x8E8);
		val |= 0x855;
		writel(val, mfg_top + 0x8E8);

		val = readl(mfg_top + 0x910);
		val |= 0x855;
		writel(val, mfg_top + 0x910);

		val = readl(mfg_top + 0x918);
		val |= 0x855;
		writel(val, mfg_top + 0x918);

		val = readl(mfg_top + 0x920);
		val |= 0x55;
		writel(val, mfg_top + 0x920);

		val = readl(mfg_top + 0x928);
		val |= 0x55;
		writel(val, mfg_top + 0x928);

		dev_info(kbdev->dev, "[CSF HW-SEQ] ACP 0x168=0x%08x 0x8E0=0x%08x 0x920=0x%08x\n",
			readl(mfg_top + 0x168), readl(mfg_top + 0x8E0), readl(mfg_top + 0x920));
	}

	iounmap(mfg_top);
}

enum gpu_dvfs_status_step {
	GPU_DVFS_STATUS_STEP_1 = 0x1,
	GPU_DVFS_STATUS_STEP_2 = 0x2,
	GPU_DVFS_STATUS_STEP_3 = 0x3,
	GPU_DVFS_STATUS_STEP_4 = 0x4,
	GPU_DVFS_STATUS_STEP_5 = 0x5,
	GPU_DVFS_STATUS_STEP_6 = 0x6,
	GPU_DVFS_STATUS_STEP_7 = 0x7,
	GPU_DVFS_STATUS_STEP_8 = 0x8,
	GPU_DVFS_STATUS_STEP_9 = 0x9,
	GPU_DVFS_STATUS_STEP_A = 0xA,
	GPU_DVFS_STATUS_STEP_B = 0xB,
	GPU_DVFS_STATUS_STEP_C = 0xC,
	GPU_DVFS_STATUS_STEP_D = 0xD,
	GPU_DVFS_STATUS_STEP_E = 0xE,
	GPU_DVFS_STATUS_STEP_F = 0xF,
};

static inline void gpu_dvfs_status_footprint(enum gpu_dvfs_status_step step)
{
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
	aee_rr_rec_gpu_dvfs_status(step |
				(aee_rr_curr_gpu_dvfs_status() & 0xF0));
#endif
}

static inline void gpu_dvfs_status_reset_footprint(void)
{
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
	aee_rr_rec_gpu_dvfs_status(0);
#endif
}

static int pm_callback_power_on_nolock(struct kbase_device *kbdev)
{
#if defined(CONFIG_MTK_GPUFREQ_V2)
	if (mtk_common_gpufreq_bringup()) {
		mtk_common_pm_mfg_active();
		return 0;
	}
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	if (mtk_common_pm_is_mfg_active())
		return 0;

	dev_vdbg(kbdev->dev, "GPU PM Callback - Active");

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_1);

	/* Stock Step 1: on,off/ SWCG(BG3D)/ MTCMOS/ BUCK via gpufreq_power_control */
#if defined(CONFIG_MTK_GPUFREQ_V2)
	dev_info(kbdev->dev, "GPU PM Callback - Calling gpufreq_power_control(GPU_PWR_ON)\n");
	if (gpufreq_power_control(GPU_PWR_ON) < 0) {
		dev_info(kbdev->dev, "GPU PM Callback - Power On Failed");
		return 1;
	}
	dev_info(kbdev->dev, "GPU PM Callback - Power On Successful\n");
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	/* Stock Step 2: Check SPM power & bus status */
	{
		void __iomem *spm = ioremap(0x1c001000, 0x1000);
		if (spm) {
			u32 mfg0 = readl(spm + 0xeb8);
			u32 mfg1 = readl(spm + 0xebc);
			u32 xpu = readl(spm + 0xf3c);
			dev_info(kbdev->dev, "GPU PM: SPM PWR_CON MFG0=0x%08x MFG1=0x%08x XPU=0x%08x\n",
				mfg0, mfg1, xpu);
			iounmap(spm);
		}
		void __iomem *mfg_top = ioremap(0x13fbf000, 0x1000);
		if (mfg_top) {
			u32 sta = readl(mfg_top + 0x0);
			u32 qch_before = readl(mfg_top + 0xb4);
			if (mfg_qchannel_enable) {
				writel(qch_before | BIT(0), mfg_top + 0xb4);
			}
			u32 qch_after = readl(mfg_top + 0xb4);
			dev_info(kbdev->dev, "GPU PM: mfg_top_config sta=0x%08x (BG3D bit0 %s), QCHANNEL_CON before=0x%08x after=0x%08x (mfg_qchannel_enable=%d)\n",
				sta, (sta & 1) ? "GATED" : "UNGATED", qch_before, qch_after, mfg_qchannel_enable);
			if (sta & 1) {
				dev_info(kbdev->dev, "GPU PM: ungating BG3D via clr register\n");
				writel(BIT(0), mfg_top + 0x8); /* clr_ofs = 0x8 */
				sta = readl(mfg_top + 0x0);
				dev_info(kbdev->dev, "GPU PM: mfg_top_config sta after ungate=0x%08x\n", sta);
			}
			iounmap(mfg_top);
		}
		void __iomem *infra = ioremap(0x10001000, 0x1000);
		if (infra) {
			u32 md0_en = readl(infra + 0xca0);
			u32 md0_rdy = readl(infra + 0xcac);
			u32 emi0_en = readl(infra + 0xc60);
			u32 emi0_rdy = readl(infra + 0xc6c);
			u32 emi1_en = readl(infra + 0xc70);
			u32 emi1_rdy = readl(infra + 0xc7c);

			dev_info(kbdev->dev, "GPU PM: INFRACFG MD0_MFG1 en=0x%08x rdy=0x%08x | EMISYS0 en=0x%08x rdy=0x%08x | EMISYS1 en=0x%08x rdy=0x%08x\n",
				md0_en, md0_rdy, emi0_en, emi0_rdy, emi1_en, emi1_rdy);
			iounmap(infra);
		}
		void __iomem *mfgrpc = ioremap(0x13f91000, 0x1000);
		if (mfgrpc) {
			u32 rpc_en_pre = readl(mfgrpc + 0x40);
			u32 rpc_rdy_pre = readl(mfgrpc + 0x48);

			/* Clear MFGRPC AXI bus protect for MFG1 (bits 16..19 and bits 0..3) */
			writel(0x000f000f, mfgrpc + 0x44);
			mb();
			udelay(10);

			u32 rpc_en_post = readl(mfgrpc + 0x40);
			u32 rpc_rdy_post = readl(mfgrpc + 0x48);
			dev_info(kbdev->dev, "GPU PM: MFGRPC PRE en=0x%08x rdy=0x%08x | POST-CLR en=0x%08x rdy=0x%08x\n",
				rpc_en_pre, rpc_rdy_pre, rpc_en_post, rpc_rdy_post);
			iounmap(mfgrpc);
		}
	}

	/* Stock Step 3: Ensure all 3 regulators are powered and at active operating voltages */
	{
		struct regulator *vstack_reg = devm_regulator_get_optional(kbdev->dev, "_vstack");
		struct regulator *vcore_reg = devm_regulator_get_optional(kbdev->dev, "_vcore");
		struct regulator *vsram_reg = devm_regulator_get_optional(kbdev->dev, "_vsram");

		if (IS_ERR_OR_NULL(vstack_reg) || IS_ERR_OR_NULL(vcore_reg) || IS_ERR_OR_NULL(vsram_reg)) {
			struct device_node *gnode = of_find_node_by_name(NULL, "gpufreq");
			if (gnode) {
				struct platform_device *gpdev = of_find_device_by_node(gnode);
				if (gpdev) {
					if (IS_ERR_OR_NULL(vstack_reg))
						vstack_reg = regulator_get_optional(&gpdev->dev, "_vstack");
					if (IS_ERR_OR_NULL(vcore_reg))
						vcore_reg = regulator_get_optional(&gpdev->dev, "_vcore");
					if (IS_ERR_OR_NULL(vsram_reg))
						vsram_reg = regulator_get_optional(&gpdev->dev, "_vsram");
				}
				of_node_put(gnode);
			}
		}

		int reg_ret;

		if (!IS_ERR_OR_NULL(vcore_reg)) {
			/* Leave voltage to mmdvfs shared SoC rail, ensure enabled */
			reg_ret = regulator_enable(vcore_reg);
			dev_info(kbdev->dev, "GPU PM: _vcore regulator = %d uV (en_ret=%d)\n",
				regulator_get_voltage(vcore_reg), reg_ret);
		}
		if (!IS_ERR_OR_NULL(vsram_reg)) {
			reg_ret = regulator_set_voltage(vsram_reg, 750000, 800000);
			reg_ret = regulator_enable(vsram_reg);
			dev_info(kbdev->dev, "GPU PM: _vsram regulator = %d uV (en_ret=%d)\n",
				regulator_get_voltage(vsram_reg), reg_ret);
		}
		if (!IS_ERR_OR_NULL(vstack_reg)) {
			reg_ret = regulator_set_voltage(vstack_reg, 687500, 750000);
			reg_ret = regulator_enable(vstack_reg);
			dev_info(kbdev->dev, "GPU PM: _vstack regulator = %d uV (en_ret=%d)\n",
				regulator_get_voltage(vstack_reg), reg_ret);
		}
	}

	/* Stock Step 4: Execute stock-ordered clock activation (PLLs + Muxes) */
	if (mt6895_power_on_clocks(kbdev) < 0) {
		dev_err(kbdev->dev, "GPU PM: mt6895_power_on_clocks failed!\n");
		if (mali_skip_mcu_on_clk_fail)
			return -EIO;
	}

	/* Stock Step 5: Configure MediaTek PDCv2 for Mali hardware automatic MTCMOS power control */
	mt6895_pdca_config(kbdev, true);

	/* Stock Step 6: Apply stock MMIO power-on sequences (I2M, DCM, ACP) */
	mt6895_apply_stock_hw_sequences(kbdev);

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_2);

#if defined(CONFIG_MTK_GPUFREQ_V2)
	gpufreq_set_timestamp();
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	/* set a flag to enable GPU DVFS */
	mtk_common_pm_mfg_active();

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_3);

	/* resume frequency */
	mtk_common_gpufreq_commit(g_cur_opp_idx);

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_4);

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	ged_dvfs_gpu_clock_switch_notify(1);
#endif

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_5);

	return 0;
}

static void pm_callback_power_off_nolock(struct kbase_device *kbdev)
{
#if defined(CONFIG_MTK_GPUFREQ_V2)
	if (mtk_common_gpufreq_bringup())
		return;
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	if (!mtk_common_pm_is_mfg_active())
		return;

	dev_vdbg(kbdev->dev, "GPU PM Callback - Idle");

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_6);

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	ged_dvfs_gpu_clock_switch_notify(0);
#endif

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_7);

	/* set a flag to disable GPU DVFS */
	mtk_common_pm_mfg_idle();

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_8);

	/* suspend frequency */
	g_cur_opp_idx = mtk_common_ged_dvfs_get_last_commit_idx();

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_9);

	/* check MFG bus if idle */
#if defined(CONFIG_MTK_GPUFREQ_V2)
	gpufreq_check_bus_idle();
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_A);

	/* on,off/ SWCG(BG3D)/ MTCMOS/ BUCK */
#if defined(CONFIG_MTK_GPUFREQ_V2)
	if (gpufreq_power_control(GPU_PWR_OFF) < 0) {
		dev_info(kbdev->dev, "GPU PM Callback - Power Off Failed");
		return;
	}
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	/* Symmetric Step: Power off GPU clocks (switch muxes to ref first, then disable PLLs) */
	mt6895_power_off_clocks(kbdev);

	/* Preserve PDCv2 active power control on power down */
	mt6895_pdca_config(kbdev, false);

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_B);
}

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	int ret = 0;

	mutex_lock(&g_mfg_lock);
	ret = pm_callback_power_on_nolock(kbdev);
#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)
	MTKGPUPower_model_resume();
#endif
	mtk_notify_gpu_power_change(1);
	mutex_unlock(&g_mfg_lock);

	return ret;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	mtk_notify_gpu_power_change(0);
#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)
	MTKGPUPower_model_suspend();
#endif
	pm_callback_power_off_nolock(kbdev);
	mutex_unlock(&g_mfg_lock);
}

static void pm_callback_power_suspend(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	dev_vdbg(kbdev->dev, "GPU PM Callback - Suspend");
	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_E);
	mutex_unlock(&g_mfg_lock);
}

static void pm_callback_power_resume(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	dev_vdbg(kbdev->dev, "GPU PM Callback - Resume");
	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_F);
	mutex_unlock(&g_mfg_lock);
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
	.power_suspend_callback  = pm_callback_power_suspend,
	.power_resume_callback = pm_callback_power_resume,
};

#ifndef CONFIG_OF
static struct kbase_io_resources io_resources = {
	.job_irq_number = 68,
	.mmu_irq_number = 69,
	.gpu_irq_number = 70,
	.io_memory_region = {
	.start = 0xFC010000,
	.end = 0xFC010000 + (4096 * 4) - 1
	}
};
#endif /* CONFIG_OF */

static struct kbase_platform_config versatile_platform_config = {
#ifndef CONFIG_OF
	.io_resources = &io_resources
#endif
};

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &versatile_platform_config;
}

int mtk_platform_device_init(struct kbase_device *kbdev)
{

	if (!kbdev) {
		pr_err("@%s: kbdev is NULL\n", __func__);
		return -1;
	}

	gpu_dvfs_status_reset_footprint();
	dev_info(kbdev->dev, "GPU PM Callback - Initialize Done");

	return 0;
}

void mtk_platform_device_term(struct kbase_device *kbdev) { }
