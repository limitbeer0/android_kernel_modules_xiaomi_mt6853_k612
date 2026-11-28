// SPDX-License-Identifier: GPL-2.0-only
/* Read-only validation of the real native DSP factors and legacy muxes. */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/string.h>

static int __init cannon_alias_init(void)
{
	struct device_node *node = of_find_compatible_node(NULL, NULL,
		"mediatek,mt6853-topckgen");
	struct of_phandle_args args = { .args_count = 1 };
	struct clk *factor, *mux;
	int i, ret = 0;
	static const char * const names[] = { "dsp1_sel_ck", "dsp2_sel_ck" };

	if (!node)
		return -ENODEV;
	args.np = node;
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		args.args[0] = 236 + i;
		factor = of_clk_get_from_provider(&args);
		if (IS_ERR(factor)) {
			ret = PTR_ERR(factor);
			goto out;
		}
		args.args[0] = 166 + 2 * i;
		mux = of_clk_get_from_provider(&args);
		if (IS_ERR(mux)) {
			ret = PTR_ERR(mux);
			clk_put(factor);
			goto out;
		}
		if (strcmp(__clk_get_name(factor), names[i]) ||
		    clk_get_rate(factor) != clk_get_rate(mux))
			ret = -EINVAL;
		pr_info("cannon-clock-alias-probe: id=%d name=%s rate=%lu legacy_mux=%lu result=%d\n",
			236 + i, __clk_get_name(factor), clk_get_rate(factor),
			clk_get_rate(mux), ret);
		clk_put(mux);
		clk_put(factor);
		if (ret)
			goto out;
	}
	args.args[0] = 238;
	factor = of_clk_get_from_provider(&args);
	if (!IS_ERR(factor)) {
		clk_put(factor);
		ret = -EINVAL;
		goto out;
	}
	if (PTR_ERR(factor) != -ENOENT) {
		ret = PTR_ERR(factor);
		goto out;
	}
	pr_info("cannon-clock-alias-probe: PASS actual DSP factors, legacy rates, invalid ID rejection; no writes\n");
out:
	of_node_put(node);
	return ret;
}
static void __exit cannon_alias_exit(void) {}
module_init(cannon_alias_init);
module_exit(cannon_alias_exit);
MODULE_LICENSE("GPL");
