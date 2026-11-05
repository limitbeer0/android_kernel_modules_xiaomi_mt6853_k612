// SPDX-License-Identifier: GPL-2.0-only
/*
 * FPC1022 electrical resource/IRQ glue for Cannon, ported from its working
 * 4.14 fpc1022_tee.c. Biometric processing remains in the original TEE/HAL.
 * Copyright (c) 2015 Fingerprint Cards AB <tech@fingerprints.com>
 * Copyright (C) 2021 XiaoMi, Inc.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

struct cannon_fpc {
	struct device *dev;
	struct spi_device *spi;
	struct gpio_desc *reset, *irq_gpio;
	struct input_dev *input;
	struct input_handler handler;
	struct wakeup_source *wakeup;
	struct mutex lock;
	atomic_t wakeup_enabled;
	int irq;
	bool irq_wake, clock_held, fingerdown_wait;
};

static int cannon_fpc_reset(struct cannon_fpc *fpc)
{
	gpiod_set_value_cansleep(fpc->reset, 1);
	usleep_range(100, 200);
	gpiod_set_value_cansleep(fpc->reset, 0);
	usleep_range(5000, 5100);
	gpiod_set_value_cansleep(fpc->reset, 1);
	usleep_range(5000, 5100);
	return 0;
}

static int cannon_fpc_check_id(struct cannon_fpc *fpc)
{
	u8 *buffer;
	struct spi_transfer transfer[] = {
		{ .len = 1,
		  .speed_hz = 1000000,
		  .delay = { .value = 5, .unit = SPI_DELAY_UNIT_USECS } },
		{ .len = 2,
		  .speed_hz = 1000000,
		  .delay = { .value = 5, .unit = SPI_DELAY_UNIT_USECS } },
	};
	int attempt, ret;
	u16 id = 0;

	buffer = kzalloc(5, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	buffer[0] = 0xfc;
	transfer[0].tx_buf = buffer;
	transfer[1].tx_buf = buffer + 1;
	transfer[1].rx_buf = buffer + 3;
	for (attempt = 0; attempt < 2; attempt++) {
		ret = spi_sync_transfer(fpc->spi, transfer,
					ARRAY_SIZE(transfer));
		if (ret)
			goto out;
		id = (buffer[3] << 8) | buffer[4];
		if ((id & 0xff00) == 0x1800) {
			dev_info(fpc->dev, "physical FPC1022 HWID=0x%04x\n",
				 id);
			goto out;
		}
		usleep_range(1000, 2000);
	}
	ret = dev_err_probe(fpc->dev, -ENODEV,
			    "unexpected fingerprint HWID 0x%04x\n", id);
out:
	kfree(buffer);
	return ret;
}

static ssize_t hw_reset_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct cannon_fpc *fpc = dev_get_drvdata(dev);
	int ret;
	if (!sysfs_streq(buf, "reset"))
		return -EINVAL;
	mutex_lock(&fpc->lock);
	ret = cannon_fpc_reset(fpc);
	mutex_unlock(&fpc->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR(hw_reset, 0200, NULL, hw_reset_store);

static ssize_t wakeup_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct cannon_fpc *fpc = dev_get_drvdata(dev);
	if (sysfs_streq(buf, "enable"))
		atomic_set(&fpc->wakeup_enabled, 1);
	else if (sysfs_streq(buf, "disable"))
		atomic_set(&fpc->wakeup_enabled, 0);
	else
		return -EINVAL;
	return count;
}
static DEVICE_ATTR(wakeup_enable, 0200, NULL, wakeup_enable_store);

static ssize_t do_wakeup_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct cannon_fpc *fpc = dev_get_drvdata(dev);
	if (!count)
		return -EINVAL;
	input_report_key(fpc->input, KEY_POWER, 1);
	input_report_key(fpc->input, KEY_POWER, 0);
	input_sync(fpc->input);
	return count;
}
static DEVICE_ATTR(do_wakeup, 0200, NULL, do_wakeup_store);

static ssize_t clk_enable_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct cannon_fpc *fpc = dev_get_drvdata(dev);
	struct device *controller = fpc->spi->controller->dev.parent;
	bool enable;
	int ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;
	mutex_lock(&fpc->lock);
	if (enable && !fpc->clock_held) {
		ret = pm_runtime_resume_and_get(controller);
		if (ret < 0)
			goto out;
		fpc->clock_held = true;
	} else if (!enable && fpc->clock_held) {
		pm_runtime_put_sync(controller);
		fpc->clock_held = false;
	}
	ret = 0;
out:
	mutex_unlock(&fpc->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR(clk_enable, 0200, NULL, clk_enable_store);

static ssize_t irq_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct cannon_fpc *fpc = dev_get_drvdata(dev);
	int level = gpiod_get_value_cansleep(fpc->irq_gpio);
	return level < 0 ? level : sysfs_emit(buf, "%d\n", level);
}
static ssize_t irq_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return count;
}
static DEVICE_ATTR_RW(irq);

static ssize_t fpid_get_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	/* These attributes only exist after a real 0x18xx HWID was read. */
	return sysfs_emit(buf, "1\n");
}
static DEVICE_ATTR_RO(fpid_get);

static ssize_t fingerdown_wait_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct cannon_fpc *fpc = dev_get_drvdata(dev);
	if (sysfs_streq(buf, "enable"))
		WRITE_ONCE(fpc->fingerdown_wait, true);
	else if (sysfs_streq(buf, "disable"))
		WRITE_ONCE(fpc->fingerdown_wait, false);
	else
		return -EINVAL;
	return count;
}
static DEVICE_ATTR(fingerdown_wait, 0200, NULL, fingerdown_wait_store);

static struct attribute *cannon_fpc_attrs[] = { &dev_attr_hw_reset.attr,
						&dev_attr_wakeup_enable.attr,
						&dev_attr_do_wakeup.attr,
						&dev_attr_clk_enable.attr,
						&dev_attr_irq.attr,
						&dev_attr_fpid_get.attr,
						&dev_attr_fingerdown_wait.attr,
						NULL };
static const struct attribute_group cannon_fpc_group = {
	.attrs = cannon_fpc_attrs
};

static irqreturn_t cannon_fpc_irq(int irq, void *data)
{
	struct cannon_fpc *fpc = data;
	if (atomic_read(&fpc->wakeup_enabled))
		__pm_wakeup_event(fpc->wakeup, 2000);
	sysfs_notify(&fpc->dev->kobj, NULL, "irq");
	return IRQ_HANDLED;
}

/* Retain the original HAL's uinput-fpc filtering behaviour. */
static bool cannon_fpc_input_filter(struct input_handle *handle,
				    unsigned int type, unsigned int code,
				    int value)
{
	return true;
}
static int cannon_fpc_input_connect(struct input_handler *handler,
				    struct input_dev *dev,
				    const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;
	if (!dev->name || !strstr(dev->name, "uinput-fpc"))
		return -ENODEV;
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;
	handle->dev = dev;
	handle->handler = handler;
	handle->name = "fpc1022_irq";
	ret = input_register_handle(handle);
	if (ret)
		goto free;
	ret = input_open_device(handle);
	if (!ret)
		return 0;
	input_unregister_handle(handle);
free:
	kfree(handle);
	return ret;
}
static void cannon_fpc_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}
static const struct input_device_id cannon_fpc_input_ids[] = {
	{ .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT_MASK(EV_KEY) } },
	{}
};

static void cannon_fpc_put_device(void *dev)
{
	put_device(dev);
}
static void cannon_fpc_disable_supply(void *supply)
{
	regulator_disable(supply);
}
static void cannon_fpc_unregister_input_handler(void *handler)
{
	input_unregister_handler(handler);
}
static void cannon_fpc_disable_irq_wake(void *data)
{
	struct cannon_fpc *fpc = data;

	if (fpc->irq_wake)
		disable_irq_wake(fpc->irq);
}

static void cannon_fpc_cleanup(void *data)
{
	struct cannon_fpc *fpc = data;
	/* The managed IRQ has already been freed and synchronized here. */
	if (fpc->clock_held)
		pm_runtime_put_sync(fpc->spi->controller->dev.parent);
	wakeup_source_unregister(fpc->wakeup);
	device_init_wakeup(fpc->dev, false);
}

static int cannon_fpc_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev, *spi_dev;
	struct device_node *np;
	struct regulator *supply;
	struct cannon_fpc *fpc;
	const char *input_name;
	u32 type = EV_MSC, code = MSC_SCAN;
	int ret;

	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	np = of_parse_phandle(dev->of_node, "fpc,spi", 0);
	if (!np)
		return -EINVAL;
	spi_dev = bus_find_device_by_of_node(&spi_bus_type, np);
	of_node_put(np);
	if (!spi_dev)
		return -EPROBE_DEFER;
	if (!device_is_bound(spi_dev) ||
	    strcmp(spi_dev->driver->name, "cannon-fpc-spi")) {
		put_device(spi_dev);
		return -EPROBE_DEFER;
	}
	if (!device_link_add(dev, spi_dev, DL_FLAG_AUTOREMOVE_CONSUMER)) {
		put_device(spi_dev);
		return -ENOMEM;
	}
	ret = devm_add_action_or_reset(dev, cannon_fpc_put_device, spi_dev);
	if (ret)
		return ret;
	fpc = devm_kzalloc(dev, sizeof(*fpc), GFP_KERNEL);
	if (!fpc)
		return -ENOMEM;
	fpc->dev = dev;
	fpc->spi = to_spi_device(spi_dev);
	mutex_init(&fpc->lock);
	atomic_set(&fpc->wakeup_enabled, 1);
	platform_set_drvdata(pdev, fpc);
	supply = devm_regulator_get(dev, "vfp");
	if (IS_ERR(supply))
		return dev_err_probe(dev, PTR_ERR(supply), "VFP supply\n");
	ret = regulator_set_voltage(supply, 3000000, 3000000);
	if (ret)
		return ret;
	ret = regulator_enable(supply);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, cannon_fpc_disable_supply, supply);
	if (ret)
		return ret;
	fpc->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(fpc->reset))
		return dev_err_probe(dev, PTR_ERR(fpc->reset), "reset GPIO\n");
	fpc->irq_gpio = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(fpc->irq_gpio))
		return dev_err_probe(dev, PTR_ERR(fpc->irq_gpio), "IRQ GPIO\n");
	cannon_fpc_reset(fpc);
	ret = cannon_fpc_check_id(fpc);
	if (ret)
		return ret;
	fpc->input = devm_input_allocate_device(dev);
	if (!fpc->input)
		return -ENOMEM;
	if (of_property_read_string(dev->of_node, "input-device-name",
				    &input_name))
		input_name = "fpc_irq@fingerprint";
	fpc->input->name = input_name;
	of_property_read_u32(dev->of_node, "fpc,event-type", &type);
	of_property_read_u32(dev->of_node, "fpc,event-code", &code);
	input_set_capability(fpc->input, type, code);
	input_set_capability(fpc->input, EV_KEY, KEY_WAKEUP);
	input_set_capability(fpc->input, EV_KEY, KEY_POWER);
	ret = input_register_device(fpc->input);
	if (ret)
		return ret;
	fpc->wakeup = wakeup_source_register(dev, "fpc_ttw_wl");
	if (!fpc->wakeup)
		return -ENOMEM;
	ret = device_init_wakeup(dev, true);
	if (ret) {
		wakeup_source_unregister(fpc->wakeup);
		return ret;
	}
	ret = devm_add_action_or_reset(dev, cannon_fpc_cleanup, fpc);
	if (ret)
		return ret;
	fpc->irq = gpiod_to_irq(fpc->irq_gpio);
	if (fpc->irq < 0)
		return fpc->irq;
	ret = devm_request_threaded_irq(dev, fpc->irq, NULL, cannon_fpc_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(dev), fpc);
	if (ret)
		return ret;
	ret = enable_irq_wake(fpc->irq);
	if (!ret)
		fpc->irq_wake = true;
	else
		dev_warn(dev, "IRQ wake unavailable: %d\n", ret);
	/* Registered after request_irq: undo wake before managed IRQ teardown. */
	ret = devm_add_action_or_reset(dev, cannon_fpc_disable_irq_wake, fpc);
	if (ret)
		return ret;
	fpc->handler.name = "fpc1022_irq";
	fpc->handler.id_table = cannon_fpc_input_ids;
	fpc->handler.connect = cannon_fpc_input_connect;
	fpc->handler.disconnect = cannon_fpc_input_disconnect;
	fpc->handler.filter = cannon_fpc_input_filter;
	ret = input_register_handler(&fpc->handler);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, cannon_fpc_unregister_input_handler,
				       &fpc->handler);
	if (ret)
		return ret;
	return devm_device_add_group(dev, &cannon_fpc_group);
}

static const struct of_device_id cannon_fpc_platform_ids[] = {
	{ .compatible = "mediatek,fpc1022_irq" },
	{}
};
MODULE_DEVICE_TABLE(of, cannon_fpc_platform_ids);
static struct platform_driver cannon_fpc_platform_driver = {
	.driver = { .name = "cannon-fpc1022",
		    .of_match_table = cannon_fpc_platform_ids },
	.probe = cannon_fpc_platform_probe,
};

static int cannon_fpc_spi_probe(struct spi_device *spi)
{
	if (!of_machine_is_compatible("xiaomi,cannon"))
		return -ENODEV;
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	spi->max_speed_hz = 1000000;
	/* Original controller setup/hold: 20 ticks at its 100 MHz source. */
	spi->cs_setup.value = 200;
	spi->cs_setup.unit = SPI_DELAY_UNIT_NSECS;
	spi->cs_hold.value = 200;
	spi->cs_hold.unit = SPI_DELAY_UNIT_NSECS;
	return spi_setup(spi);
}
static const struct of_device_id cannon_fpc_spi_ids[] = {
	{ .compatible = "fpc,fpc1022" },
	/* Stock signed DTBO names the shared SPI transport after Goodix. */
	{ .compatible = "goodix,goodix-fp" },
	{}
};
MODULE_DEVICE_TABLE(of, cannon_fpc_spi_ids);
static const struct spi_device_id cannon_fpc_spi_names[] = { { "fpc1022", 0 },
							     { "goodix-fp", 0 },
							     {} };
MODULE_DEVICE_TABLE(spi, cannon_fpc_spi_names);
static struct spi_driver cannon_fpc_spi_driver = {
	.driver = { .name = "cannon-fpc-spi",
		    .of_match_table = cannon_fpc_spi_ids },
	.id_table = cannon_fpc_spi_names,
	.probe = cannon_fpc_spi_probe,
};
static int __init cannon_fpc_init(void)
{
	int ret = spi_register_driver(&cannon_fpc_spi_driver);
	if (ret)
		return ret;
	ret = platform_driver_register(&cannon_fpc_platform_driver);
	if (ret)
		spi_unregister_driver(&cannon_fpc_spi_driver);
	return ret;
}
module_init(cannon_fpc_init);
static void __exit cannon_fpc_exit(void)
{
	platform_driver_unregister(&cannon_fpc_platform_driver);
	spi_unregister_driver(&cannon_fpc_spi_driver);
}
module_exit(cannon_fpc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("limitbeer0");
MODULE_DESCRIPTION("Cannon FPC1022 resource and IRQ glue");
