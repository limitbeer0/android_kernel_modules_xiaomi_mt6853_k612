// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cannon AW87359/AW87519 analogue amplifier bridge.
 * Register ordering and reset sequence ported from the working 4.14 drivers.
 * Copyright (c) 2019 AWINIC Technology CO., LTD
 * Copyright (C) 2021 XiaoMi, Inc.
 */
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include "cannon-aw87xxx.h"

struct cannon_amp {
	struct i2c_client *client;
	struct gpio_desc *reset;
	struct mutex lock;
	const struct firmware *speaker, *receiver;
	bool enabled, reset_required;
};

static int cannon_amp_write(struct cannon_amp *amp, u8 reg, u8 value)
{
	int ret, i;
	for (i = 0; i < 5; i++) {
		ret = i2c_smbus_write_byte_data(amp->client, reg, value);
		if (!ret)
			return 0;
		usleep_range(2000, 2500);
	}
	return ret;
}

static int cannon_amp_off(struct cannon_amp *amp)
{
	int ret = 0;
	if (amp->enabled)
		ret = cannon_amp_write(amp, 1, 0);
	if (amp->reset)
		gpiod_set_value_cansleep(amp->reset, 0);
	amp->enabled = false;
	return ret;
}

int cannon_aw87xxx_set_mode(struct device *dev, enum cannon_amp_mode mode)
{
	struct cannon_amp *amp = dev_get_drvdata(dev);
	const struct firmware *fw;
	int ret = 0;
	size_t i;

	if (!amp || mode > CANNON_AMP_RECEIVER)
		return -EINVAL;
	mutex_lock(&amp->lock);
	if (mode == CANNON_AMP_OFF) {
		ret = cannon_amp_off(amp);
		goto out;
	}
	fw = mode == CANNON_AMP_RECEIVER ? amp->receiver : amp->speaker;
	if (!amp->enabled && amp->reset) {
		gpiod_set_value_cansleep(amp->reset, 0);
		usleep_range(2000, 2500);
		gpiod_set_value_cansleep(amp->reset, 1);
		usleep_range(2000, 2500);
		ret = cannon_amp_write(amp, 0x64, 0x2c);
		if (ret)
			goto fail;
	}
	amp->enabled = true;
	/* Keep the vendor AGC/boost settings and their exact write order. */
	for (i = 0; i < fw->size; i += 2) {
		ret = cannon_amp_write(amp, fw->data[i], fw->data[i + 1]);
		if (ret)
			goto fail;
	}
	dev_info(dev, "mode %u: applied %zu vendor register pairs\n", mode,
		 fw->size / 2);
	goto out;
fail:
	cannon_amp_off(amp);
out:
	mutex_unlock(&amp->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cannon_aw87xxx_set_mode);

static void cannon_amp_put_device(void *dev)
{
	put_device(dev);
}

struct device *cannon_aw87xxx_get(struct device *consumer, const char *property)
{
	struct device_node *np;
	struct i2c_client *client;
	int ret;

	np = of_parse_phandle(consumer->of_node, property, 0);
	if (!np)
		return ERR_PTR(-EINVAL);
	client = of_find_i2c_device_by_node(np);
	of_node_put(np);
	if (!client)
		return ERR_PTR(-EPROBE_DEFER);
	if (!device_is_bound(&client->dev) ||
	    strcmp(client->dev.driver->name, "cannon-aw87xxx")) {
		put_device(&client->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}
	if (!device_link_add(consumer, &client->dev,
			     DL_FLAG_AUTOREMOVE_CONSUMER)) {
		put_device(&client->dev);
		return ERR_PTR(-ENOMEM);
	}
	ret = devm_add_action_or_reset(consumer, cannon_amp_put_device,
				       &client->dev);
	return ret ? ERR_PTR(ret) : &client->dev;
}
EXPORT_SYMBOL_GPL(cannon_aw87xxx_get);

static void cannon_amp_cleanup(void *data)
{
	struct cannon_amp *amp = data;
	mutex_lock(&amp->lock);
	cannon_amp_off(amp);
	mutex_unlock(&amp->lock);
	release_firmware(amp->speaker);
	release_firmware(amp->receiver);
}

static int cannon_amp_firmware(struct device *dev, const char *name,
			       const struct firmware **firmware)
{
	int ret = request_firmware(firmware, name, dev);
	if (ret)
		return dev_err_probe(dev, ret, "firmware %s\n", name);
	if (!(*firmware)->size || (*firmware)->size % 2) {
		release_firmware(*firmware);
		*firmware = NULL;
		return -EINVAL;
	}
	return 0;
}

static int cannon_amp_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cannon_amp *amp;
	const char *speaker, *receiver;
	int ret, id = -1, i;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;
	amp = devm_kzalloc(dev, sizeof(*amp), GFP_KERNEL);
	if (!amp)
		return -ENOMEM;
	amp->client = client;
	amp->reset_required =
		of_device_is_compatible(dev->of_node, "awinic,aw87519_pa");
	mutex_init(&amp->lock);
	amp->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(amp->reset))
		return dev_err_probe(dev, PTR_ERR(amp->reset), "reset GPIO\n");
	if (amp->reset_required && !amp->reset)
		return -EINVAL;
	i2c_set_clientdata(client, amp);
	if (amp->reset) {
		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(amp->reset, 1);
		usleep_range(2000, 2500);
	}
	amp->enabled = true;
	ret = devm_add_action_or_reset(dev, cannon_amp_cleanup, amp);
	if (ret)
		return ret;
	for (i = 0; i < 5; i++) {
		if (amp->reset_required) {
			ret = cannon_amp_write(amp, 0x64, 0x2c);
			if (ret)
				return ret;
		}
		id = i2c_smbus_read_byte_data(client, 0);
		if (id == 0x59)
			break;
		usleep_range(2000, 2500);
	}
	if (id != 0x59)
		return dev_err_probe(dev, id < 0 ? id : -ENODEV, "chip ID %d\n",
				     id);
	speaker = amp->reset_required ? "aw87519_kspk.bin" : "aw87359_dspk.bin";
	receiver = amp->reset_required ? "aw87519_drcv.bin" :
					 "aw87359_abrcv.bin";
	ret = cannon_amp_firmware(dev, speaker, &amp->speaker);
	if (ret)
		return ret;
	ret = cannon_amp_firmware(dev, receiver, &amp->receiver);
	if (ret)
		return ret;
	ret = cannon_amp_off(amp);
	if (ret)
		return ret;
	dev_info(dev, "chip ID 0x%02x, loaded %s and %s\n", id, speaker,
		 receiver);
	return 0;
}

static void cannon_amp_shutdown(struct i2c_client *client)
{
	struct cannon_amp *amp = i2c_get_clientdata(client);
	mutex_lock(&amp->lock);
	cannon_amp_off(amp);
	mutex_unlock(&amp->lock);
}

static const struct of_device_id cannon_amp_match[] = {
	{ .compatible = "awinic,aw87359_pa" },
	{ .compatible = "awinic,aw87519_pa" },
	{}
};
MODULE_DEVICE_TABLE(of, cannon_amp_match);

static struct i2c_driver cannon_amp_driver = {
 .driver = {
  .name = "cannon-aw87xxx",
  .of_match_table = cannon_amp_match,
 },
 .probe = cannon_amp_probe,
 .shutdown = cannon_amp_shutdown,
};
module_i2c_driver(cannon_amp_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cannon AW87359/AW87519 vendor profile amplifier bridge");
MODULE_AUTHOR("limitbeer0");
