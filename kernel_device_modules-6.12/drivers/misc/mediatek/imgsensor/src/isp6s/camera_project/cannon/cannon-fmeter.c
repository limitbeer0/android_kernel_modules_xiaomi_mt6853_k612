/*
 * Copyright (C) 2020 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(meter_lock);
static void __iomem *topck_base;
struct cannon_meter_mux {
	unsigned int id, offset, pdn;
};
static const struct cannon_meter_mux muxes[] = {
	{ 1, 0x10, 7 },	  { 2, 0x10, 15 },  { 3, 0x10, 23 },   { 4, 0x10, 31 },
	{ 5, 0x20, 7 },	  { 6, 0x20, 15 },  { 7, 0x20, 23 },   { 8, 0x20, 31 },
	{ 9, 0x30, 7 },	  { 10, 0x30, 15 }, { 11, 0x30, 23 },  { 12, 0x30, 31 },
	{ 13, 0x40, 7 },  { 14, 0x40, 15 }, { 15, 0x40, 23 },  { 18, 0x50, 15 },
	{ 19, 0x50, 23 }, { 20, 0x50, 31 }, { 21, 0x60, 7 },   { 22, 0x60, 15 },
	{ 23, 0x60, 23 }, { 26, 0x60, 15 }, { 27, 0x60, 23 },  { 28, 0x60, 31 },
	{ 29, 0x80, 7 },  { 30, 0x80, 15 }, { 32, 0x80, 31 },  { 33, 0x90, 7 },
	{ 34, 0x90, 15 }, { 35, 0x90, 23 }, { 36, 0x90, 31 },  { 37, 0xA0, 7 },
	{ 38, 0xA0, 15 }, { 39, 0xA0, 23 }, { 40, 0xA0, 31 },  { 41, 0xB0, 7 },
	{ 42, 0xB0, 15 }, { 43, 0xB0, 23 }, { 44, 0xB0, 31 },  { 45, 0xC0, 7 },
	{ 48, 0xC0, 31 }, { 49, 0xD0, 7 },  { 50, 0xD0, 15 },  { 51, 0xD0, 23 },
	{ 52, 0xD0, 31 }, { 53, 0xE0, 7 },  { 54, 0xE0, 15 },  { 55, 0xE0, 23 },
	{ 56, 0xE0, 31 }, { 57, 0xF0, 7 },  { 58, 0xF0, 15 },  { 59, 0xF0, 23 },
	{ 60, 0xF0, 31 }, { 61, 0x100, 7 }, { 62, 0x100, 15 }, { 63, 0x100, 23 }
};
#define fmeter_lock(flags) spin_lock_irqsave(&meter_lock, flags)
#define fmeter_unlock(flags) spin_unlock_irqrestore(&meter_lock, flags)
#define clk_readl(addr) readl(addr)
#define clk_writel(addr, val)          \
	do {                           \
		writel((val), (addr)); \
		wmb();                 \
	} while (0)
#define CLK_MISC_CFG_0 (topck_base + 0x140)
#define CLK_DBG_CFG (topck_base + 0x17c)
#define CLK26CALI_0 (topck_base + 0x220)
#define CLK26CALI_1 (topck_base + 0x224)

static bool check_mux_pdn(unsigned int id)
{
	unsigned int i;
	if (!topck_base)
		return true;
	for (i = 0; i < ARRAY_SIZE(muxes); i++)
		if (muxes[i].id == id)
			return !!(readl(topck_base + muxes[i].offset) &
				  BIT(muxes[i].pdn));
	return true;
}

int cannon_mt6853_fmeter_init(struct device *dev)
{
	struct device_node *node;
	struct resource res;
	int ret;
	node = of_find_compatible_node(NULL, NULL, "mediatek,topckgen");
	if (!node)
		return -ENODEV;
	ret = of_address_to_resource(node, 0, &res);
	of_node_put(node);
	if (ret)
		return ret;
	topck_base = devm_ioremap(dev, res.start, resource_size(&res));
	return topck_base ? 0 : -ENOMEM;
}
unsigned int cannon_mt6853_ckgen_freq(unsigned int ID)
{
	int output = 0, i = 0;
	unsigned int temp, clk_dbg_cfg, clk_misc_cfg_0, clk26cali_1 = 0;
	unsigned long flags;

	if (check_mux_pdn(ID)) {
		pr_notice("ID-%d: MUX PDN, return 0.\n", ID);
		return 0;
	}

	fmeter_lock(flags);
	while (clk_readl(CLK26CALI_0) & 0x1000) {
		udelay(10);
		i++;
		if (i > 30)
			break;
	}

	clk_dbg_cfg = clk_readl(CLK_DBG_CFG);
	clk_writel(CLK_DBG_CFG, (clk_dbg_cfg & 0xFFFFC0FC) | (ID << 8) | (0x1));

	clk_misc_cfg_0 = clk_readl(CLK_MISC_CFG_0);
	clk_writel(CLK_MISC_CFG_0, (clk_misc_cfg_0 & 0x00FFFFFF) | (3 << 24));

	clk26cali_1 = clk_readl(CLK26CALI_1);
	clk_writel(CLK26CALI_0, 0x1000);
	clk_writel(CLK26CALI_0, 0x1010);

	/* wait frequency meter finish */
	while (clk_readl(CLK26CALI_0) & 0x10) {
		udelay(10);
		i++;
		if (i > 30)
			break;
	}
	/* illegal pass */
	if (i == 0) {
		clk_writel(CLK26CALI_0, 0x0000);
		//re-trigger
		clk_writel(CLK26CALI_0, 0x1000);
		clk_writel(CLK26CALI_0, 0x1010);
		while (clk_readl(CLK26CALI_0) & 0x10) {
			udelay(10);
			i++;
			if (i > 30)
				break;
		}
	}

	temp = clk_readl(CLK26CALI_1) & 0xFFFF;

	output = (temp * 26000) / 1024;

	clk_writel(CLK_DBG_CFG, clk_dbg_cfg);
	clk_writel(CLK_MISC_CFG_0, clk_misc_cfg_0);
	/*clk_writel(CLK26CALI_0, clk26cali_0);*/
	/*clk_writel(CLK26CALI_1, clk26cali_1);*/

	clk_writel(CLK26CALI_0, 0x0000);
	fmeter_unlock(flags);
	/*print("ckgen meter[%d] = %d Khz\n", ID, output);*/
	if (i > 30)
		return 0;
	if ((output * 4) < 25000) {
		pr_notice(
			"%s: CLK_DBG_CFG = 0x%x, CLK_MISC_CFG_0 = 0x%x, CLK26CALI_0 = 0x%x, CLK26CALI_1 = 0x%x\n",
			__func__, clk_readl(CLK_DBG_CFG),
			clk_readl(CLK_MISC_CFG_0), clk_readl(CLK26CALI_0),
			clk_readl(CLK26CALI_1));
	}
	return (output * 4);
}
