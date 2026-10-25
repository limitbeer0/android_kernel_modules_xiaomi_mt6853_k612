// SPDX-License-Identifier: GPL-2.0-only
/* Optional Cannon diagnostic: DMA mappings, no display DMA submissions. */
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>

struct cannon_iommu_result {
	int error;
	unsigned int checked_pages;
};

static ssize_t result_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cannon_iommu_result *result = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s error=%d checked_pages=%u (mapping only)\n",
		result->error ? "FAIL" : "PASS", result->error, result->checked_pages);
}
static DEVICE_ATTR_RO(result);
static struct attribute *cannon_iommu_attrs[] = { &dev_attr_result.attr, NULL };
ATTRIBUTE_GROUPS(cannon_iommu);

static int cannon_iommu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cannon_iommu_result *result;
	struct iommu_domain *domain;
	struct sg_table table;
	struct scatterlist *sg;
	dma_addr_t iova;
	void *coherent;
	unsigned long linear;
	size_t offset, position = 0;
	unsigned int i;
	int error;

	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		return dev_err_probe(dev, -EPROBE_DEFER, "IOMMU domain not attached\n");
	if (domain->type != IOMMU_DOMAIN_DMA && domain->type != IOMMU_DOMAIN_DMA_FQ)
		return dev_err_probe(dev, -EINVAL, "A translated DMA domain is required\n");
	result = devm_kzalloc(dev, sizeof(*result), GFP_KERNEL);
	if (!result)
		return -ENOMEM;
	platform_set_drvdata(pdev, result);
	error = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(34));
	if (error)
		goto done;

	coherent = dma_alloc_coherent(dev, 3 * PAGE_SIZE, &iova, GFP_KERNEL);
	if (!coherent) {
		error = -ENOMEM;
		goto done;
	}
	error = dma_get_sgtable(dev, &table, coherent, iova, 3 * PAGE_SIZE);
	if (error)
		goto free_coherent;
	for_each_sg(table.sgl, sg, table.orig_nents, i) {
		for (offset = 0; offset < sg->length; offset += PAGE_SIZE) {
			if (iommu_iova_to_phys(domain, iova + position + offset) !=
			    sg_phys(sg) + offset) {
				error = -EIO;
				goto free_table;
			}
			result->checked_pages++;
		}
		position += sg->length;
	}
	if (position != 3 * PAGE_SIZE)
		error = -EIO;
free_table:
	sg_free_table(&table);
free_coherent:
	dma_free_coherent(dev, 3 * PAGE_SIZE, coherent, iova);
	if (error)
		goto done;

	linear = __get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
	if (!linear) {
		error = -ENOMEM;
		goto done;
	}
	iova = dma_map_single(dev, (void *)linear, 2 * PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, iova)) {
		error = -EIO;
		goto free_linear;
	}
	for (offset = 0; offset < 2 * PAGE_SIZE; offset += PAGE_SIZE) {
		if (iommu_iova_to_phys(domain, iova + offset) !=
		    virt_to_phys((void *)(linear + offset))) {
			error = -EIO;
			break;
		}
		result->checked_pages++;
	}
	dma_unmap_single(dev, iova, 2 * PAGE_SIZE, DMA_BIDIRECTIONAL);
free_linear:
	free_pages(linear, 1);
done:
	result->error = error;
	dev_info(dev, "cannon IOMMU mapping %s error=%d checked_pages=%u\n",
		 error ? "FAIL" : "PASS", error, result->checked_pages);
	return 0;
}

static const struct of_device_id cannon_iommu_match[] = {
	{ .compatible = "xiaomi,cannon-iommu-probe" }, {}
};
MODULE_DEVICE_TABLE(of, cannon_iommu_match);
static struct platform_driver cannon_iommu_driver = {
	.probe = cannon_iommu_probe,
	.driver = {
		.name = "cannon-iommu-probe",
		.of_match_table = cannon_iommu_match,
		.dev_groups = cannon_iommu_groups,
	},
};
module_platform_driver(cannon_iommu_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Optional Cannon IOMMU DMA mapping diagnostic");
