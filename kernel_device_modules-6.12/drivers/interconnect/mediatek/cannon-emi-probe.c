// SPDX-License-Identifier: GPL-2.0-only
/* Bounded native EMI vote diagnostic; never submits hardware DMA. */
#include <linux/interconnect.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct cannon_emi_probe {
	struct icc_path *path;
	struct mutex lock;
	u32 bandwidth;
};

static ssize_t bandwidth_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct cannon_emi_probe *probe = dev_get_drvdata(dev);
	u32 bw;

	mutex_lock(&probe->lock);
	bw = probe->bandwidth;
	mutex_unlock(&probe->lock);
	return sysfs_emit(buf, "%u\n", bw);
}

static ssize_t bandwidth_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct cannon_emi_probe *probe = dev_get_drvdata(dev);
	u32 bw;
	int ret = kstrtou32(buf, 0, &bw);

	if (ret)
		return ret;
	if (bw > 200000)
		return -ERANGE;
	mutex_lock(&probe->lock);
	ret = icc_set_bw(probe->path, bw, 0);
	if (!ret)
		probe->bandwidth = bw;
	mutex_unlock(&probe->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(bandwidth);
static struct attribute *cannon_emi_attrs[] = { &dev_attr_bandwidth.attr, NULL };
ATTRIBUTE_GROUPS(cannon_emi);

static int cannon_emi_probe(struct platform_device *pdev)
{
	struct cannon_emi_probe *probe;

	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	probe = devm_kzalloc(&pdev->dev, sizeof(*probe), GFP_KERNEL);
	if (!probe)
		return -ENOMEM;
	probe->path = of_icc_get(&pdev->dev, "icc-bw");
	if (IS_ERR(probe->path))
		return dev_err_probe(&pdev->dev, PTR_ERR(probe->path), "EMI path unavailable\n");
	mutex_init(&probe->lock);
	platform_set_drvdata(pdev, probe);
	dev_info(&pdev->dev, "native EMI diagnostic ready; vote range 0..200000 kB/s\n");
	return 0;
}

static void cannon_emi_remove(struct platform_device *pdev)
{
	struct cannon_emi_probe *probe = platform_get_drvdata(pdev);

	icc_set_bw(probe->path, 0, 0);
	icc_put(probe->path);
}
static const struct of_device_id cannon_emi_ids[] = {
	{ .compatible = "xiaomi,cannon-emi-probe" }, {}
};
MODULE_DEVICE_TABLE(of, cannon_emi_ids);
static struct platform_driver cannon_emi_driver = {
	.probe = cannon_emi_probe,
	.remove = cannon_emi_remove,
	.driver = {
		.name = "cannon-emi-probe",
		.of_match_table = cannon_emi_ids,
		.dev_groups = cannon_emi_groups,
	},
};
module_platform_driver(cannon_emi_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cannon bounded native EMI interconnect vote diagnostic");
