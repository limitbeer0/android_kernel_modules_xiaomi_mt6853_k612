// SPDX-License-Identifier: GPL-2.0-only
/* Exercise real CAM LARB -> MMQoS -> EMI votes, without camera DMA. */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "mtk-interconnect.h"
#include <dt-bindings/interconnect/mtk,mmqos.h>

struct cannon_mmqos_probe {
	struct icc_path *path;
	struct device *larb;
	struct mutex lock;
	u32 bandwidth;
};
static void cannon_mmqos_put_device(void *data) { put_device(data); }

static ssize_t bandwidth_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cannon_mmqos_probe *probe = dev_get_drvdata(dev);
	u32 bandwidth;

	mutex_lock(&probe->lock);
	bandwidth = probe->bandwidth;
	mutex_unlock(&probe->lock);
	return sysfs_emit(buf, "%u\n", bandwidth);
}
static ssize_t bandwidth_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct cannon_mmqos_probe *probe = dev_get_drvdata(dev);
	u32 bw;
	bool acquired = false;
	int ret = kstrtou32(buf, 0, &bw);

	if (ret)
		return ret;
	if (bw > 200000)
		return -ERANGE;
	mutex_lock(&probe->lock);
	if (bw && !probe->bandwidth) {
		ret = pm_runtime_resume_and_get(probe->larb);
		if (ret < 0)
			goto out;
		acquired = true;
	}
	ret = mtk_icc_set_bw(probe->path, bw, 0);
	if (ret) {
		if (acquired)
			pm_runtime_put_sync(probe->larb);
		goto out;
	}
	if (!bw && probe->bandwidth)
		pm_runtime_put_sync(probe->larb);
	probe->bandwidth = bw;
out:
	mutex_unlock(&probe->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(bandwidth);
static struct attribute *cannon_mmqos_attrs[] = { &dev_attr_bandwidth.attr, NULL };
ATTRIBUTE_GROUPS(cannon_mmqos);

static int cannon_mmqos_probe(struct platform_device *pdev)
{
	struct cannon_mmqos_probe *probe;
	struct device_node *node;
	struct platform_device *larb;
	int ret;
	u32 larb_id, port = 0;

	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	node = of_parse_phandle(pdev->dev.of_node, "mediatek,larb", 0);
	if (!node)
		return -EINVAL;
	ret = of_property_read_u32(node, "mediatek,larb-id", &larb_id);
	if (ret || (larb_id != 2 && larb_id != 9 && larb_id != 11 && larb_id != 16)) {
		of_node_put(node);
		return -EINVAL;
	}
	of_property_read_u32(pdev->dev.of_node, "mediatek,port", &port);
	if (port >= 32 || (larb_id == 11 && (port < 15 || port > 25))) {
		of_node_put(node);
		return -EINVAL;
	}
	larb = of_find_device_by_node(node);
	of_node_put(node);
	if (!larb)
		return -EPROBE_DEFER;
	if (!larb->dev.driver) {
		put_device(&larb->dev);
		return -EPROBE_DEFER;
	}
	ret = devm_add_action_or_reset(&pdev->dev, cannon_mmqos_put_device, &larb->dev);
	if (ret)
		return ret;
	probe = devm_kzalloc(&pdev->dev, sizeof(*probe), GFP_KERNEL);
	if (!probe)
		return -ENOMEM;
	probe->path = mtk_icc_get(&pdev->dev, MASTER_LARB_PORT((larb_id << 5) | port), SLAVE_COMMON(0));
	if (IS_ERR_OR_NULL(probe->path))
		return probe->path ? PTR_ERR(probe->path) : -EPROBE_DEFER;
	probe->larb = &larb->dev;
	mutex_init(&probe->lock);
	platform_set_drvdata(pdev, probe);
	dev_info(&pdev->dev, "native CAM MMQoS diagnostic ready; max vote 200000 kB/s\n");
	return 0;
}
static void cannon_mmqos_remove(struct platform_device *pdev)
{
	struct cannon_mmqos_probe *probe = platform_get_drvdata(pdev);

	if (probe->bandwidth) {
		mtk_icc_set_bw(probe->path, 0, 0);
		pm_runtime_put_sync(probe->larb);
	}
	mtk_icc_put(probe->path);
}
static const struct of_device_id cannon_mmqos_ids[] = {
	{ .compatible = "xiaomi,cannon-mmqos-probe" }, {}
};
MODULE_DEVICE_TABLE(of, cannon_mmqos_ids);
static struct platform_driver cannon_mmqos_driver = {
	.probe = cannon_mmqos_probe,
	.remove = cannon_mmqos_remove,
	.driver = {
		.name = "cannon-mmqos-probe",
		.of_match_table = cannon_mmqos_ids,
		.dev_groups = cannon_mmqos_groups,
	},
};
module_platform_driver(cannon_mmqos_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cannon bounded CAM LARB MMQoS vote diagnostic");
