// SPDX-License-Identifier: GPL-2.0-only
/* Optional Cannon diagnostic: GCE writes coherent scratch memory only. */
#include <linux/dma-mapping.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq-ext.h>

struct cannon_cmdq_result {
	int error;
	u32 words[4];
};

static ssize_t result_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cannon_cmdq_result *result = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s error=%d words=%08x %08x %08x %08x\n",
		result->error ? "FAIL" : "PASS", result->error,
		result->words[0], result->words[1], result->words[2], result->words[3]);
}
static DEVICE_ATTR_RO(result);
static struct attribute *cannon_cmdq_attrs[] = { &dev_attr_result.attr, NULL };
ATTRIBUTE_GROUPS(cannon_cmdq);

static int cannon_cmdq_probe(struct platform_device *pdev)
{
	static const u32 expected[] = { 0x43414e4e, 0x4f4e3631, 0x32544351, 0xa55ac33c };
	struct device *dev = &pdev->dev, *dma_dev;
	struct cannon_cmdq_result *result;
	struct cmdq_client *client;
	struct cmdq_pkt *packet;
	dma_addr_t address;
	u32 *scratch;
	int error, i;

	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	result = devm_kzalloc(dev, sizeof(*result), GFP_KERNEL);
	if (!result)
		return -ENOMEM;
	platform_set_drvdata(pdev, result);
	client = cmdq_mbox_create(dev, 0);
	if (IS_ERR(client))
		return PTR_ERR(client);
	if (!client)
		return -EPROBE_DEFER;
	dma_dev = client->share_dev;
	scratch = dma_alloc_coherent(dma_dev, PAGE_SIZE, &address, GFP_KERNEL);
	if (!scratch) {
		cmdq_mbox_destroy(client);
		return -ENOMEM;
	}
	packet = cmdq_pkt_create(client);
	if (IS_ERR_OR_NULL(packet)) {
		error = packet ? PTR_ERR(packet) : -ENOMEM;
		goto free_memory;
	}
	/* No register addresses from the platform test suite are used here. */
	for (i = 0; i < ARRAY_SIZE(expected); i++) {
		error = cmdq_pkt_write_value_addr(packet, address + i * sizeof(u32),
						 expected[i], U32_MAX);
		if (error)
			goto free_packet;
	}
	dma_wmb();
	cmdq_mbox_enable(client->chan);
	error = cmdq_pkt_flush(packet);
	/* Stop the thread before releasing DMA memory, including on timeout. */
	cmdq_mbox_stop(client);
	cmdq_mbox_disable(client->chan);
	dma_rmb();
	for (i = 0; i < ARRAY_SIZE(expected); i++) {
		result->words[i] = READ_ONCE(scratch[i]);
		if (!error && result->words[i] != expected[i])
			error = -EIO;
	}
free_packet:
	cmdq_pkt_destroy(packet);
free_memory:
	dma_free_coherent(dma_dev, PAGE_SIZE, scratch, address);
	cmdq_mbox_destroy(client);
	result->error = error;
	dev_info(dev, "cannon GCE scratch %s error=%d words=%08x %08x %08x %08x\n",
		error ? "FAIL" : "PASS", error, result->words[0], result->words[1],
		result->words[2], result->words[3]);
	/* Keep a failed result readable; this is an explicitly selected diagnostic. */
	return 0;
}

static const struct of_device_id cannon_cmdq_match[] = {
	{ .compatible = "xiaomi,cannon-cmdq-probe" }, {}
};
MODULE_DEVICE_TABLE(of, cannon_cmdq_match);
static struct platform_driver cannon_cmdq_driver = {
	.probe = cannon_cmdq_probe,
	.driver = {
		.name = "cannon-cmdq-probe",
		.of_match_table = cannon_cmdq_match,
		.dev_groups = cannon_cmdq_groups,
	},
};
module_platform_driver(cannon_cmdq_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Optional Cannon GCE coherent-memory execution diagnostic");
