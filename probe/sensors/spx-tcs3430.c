// SPDX-License-Identifier: GPL-2.0-only
/* TCS3430 direct XYZ/IR sampling. Raw channels deliberately do not claim lux. */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>

#define TCS_ENABLE 0x80
#define TCS_ATIME 0x81
#define TCS_CFG1 0x90
#define TCS_ID 0x92
#define TCS_STATUS 0x93
#define TCS_DATA 0x94
#define TCS_CFG2 0x9f
#define TCS_CFG3 0xab
#define TCS_INTENAB 0xdd
#define TCS_CYCLES 36
#define TCS_GAIN_DEFAULT_CODE 2
#define TCS_GAIN_HIGH_Y 52000
#define TCS_GAIN_LOW_Y 200

static const int tcs_gains[4] = { 1, 4, 16, 64 };

struct spx_tcs3430 {
	struct i2c_client *client;
	struct mutex lock;
	int frame[4];
	int frame_gain;
	u8 gain_code;
	unsigned long expires;
	unsigned int read_mask;
	bool frame_valid;
	bool shutting_down;
};

static int tcs_write(struct spx_tcs3430 *s, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(s->client, reg, value);
}

static int tcs_set_gain(struct spx_tcs3430 *s, u8 code)
{
	int ret;

	ret = tcs_write(s, TCS_CFG1, code);
	if (ret < 0)
		return ret;
	s->gain_code = code;
	return 0;
}

static int tcs_sample(struct spx_tcs3430 *s, unsigned int channel, int *value)
{
	u8 raw[8];
	int ret, stop, attempt, used_gain, y;
	unsigned int i;

	for (attempt = 0; attempt < 2; attempt++) {
		used_gain = tcs_gains[s->gain_code & 3];
		/* One conversion, no interrupt, no background sampling or wake lock. */
		ret = tcs_write(s, TCS_ENABLE, 0);
		if (ret < 0)
			return ret;
		ret = tcs_write(s, TCS_STATUS, 0x90);
		if (ret < 0)
			return ret;
		ret = tcs_write(s, TCS_ENABLE, 3);
		if (ret < 0)
			goto power_off;
		/* Autozero can precede the 100.08 ms integration cycle. */
		msleep(120);
		ret = i2c_smbus_read_byte_data(s->client, TCS_STATUS);
		if (ret < 0)
			goto power_off;
		if (ret & 0x80) {
			stop = tcs_write(s, TCS_ENABLE, 0);
			if (stop < 0)
				return stop;
			if (s->gain_code > 0) {
				ret = tcs_set_gain(s, s->gain_code - 1);
				if (ret < 0)
					return ret;
				if (attempt == 0)
					continue;
			}
			return -EOVERFLOW;
		}
		ret = i2c_smbus_read_i2c_block_data(s->client, TCS_DATA, sizeof(raw), raw);
		stop = tcs_write(s, TCS_ENABLE, 0);
		if (stop < 0)
			return stop;
		if (ret < 0)
			return ret;
		if (ret != sizeof(raw))
			return -EIO;
		for (i = 0; i < 4; i++)
			s->frame[i] = raw[i * 2] | (raw[i * 2 + 1] << 8);
		s->frame_gain = used_gain;
		y = s->frame[1];
		if (y > TCS_GAIN_HIGH_Y && s->gain_code > 0) {
			ret = tcs_set_gain(s, s->gain_code - 1);
			if (ret < 0)
				return ret;
			if (attempt == 0)
				continue;
		} else if (y < TCS_GAIN_LOW_Y && s->gain_code < 3) {
			ret = tcs_set_gain(s, s->gain_code + 1);
			if (ret < 0)
				return ret;
		}
		*value = s->frame[channel];
		return IIO_VAL_INT;
	}
	return -EOVERFLOW;

power_off:
	stop = tcs_write(s, TCS_ENABLE, 0);
	return stop < 0 ? stop : ret;
}

/* Consecutive channel reads share one conversion; repeated channels refresh it. */
static int tcs_cached_sample(struct spx_tcs3430 *s, unsigned int channel, int *value)
{
	int ret;

	if (!s->frame_valid || !time_before(jiffies, s->expires) ||
	    (s->read_mask & BIT(channel))) {
		s->frame_valid = false;
		ret = tcs_sample(s, channel, value);
		if (ret != IIO_VAL_INT)
			return ret;
		s->frame_valid = true;
		s->read_mask = 0;
		s->expires = jiffies + msecs_to_jiffies(200);
	}
	*value = s->frame[channel];
	s->read_mask |= BIT(channel);
	return IIO_VAL_INT;
}

static int tcs_read_raw(struct iio_dev *indio,
			const struct iio_chan_spec *chan,
			int *val, int *val2, long mask)
{
	struct spx_tcs3430 *s = iio_priv(indio);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&s->lock);
		if (s->shutting_down)
			ret = -ESHUTDOWN;
		else
			ret = tcs_cached_sample(s, chan->address, val);
		mutex_unlock(&s->lock);
		return ret;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		mutex_lock(&s->lock);
		*val = s->frame_valid ? s->frame_gain : tcs_gains[s->gain_code & 3];
		mutex_unlock(&s->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = TCS_CYCLES * 2780;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

#define TCS_CHANNEL(label, index) { \
	.type = IIO_INTENSITY, .indexed = 1, .channel = index, \
	.extend_name = label, .address = index, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_HARDWAREGAIN) | \
				  BIT(IIO_CHAN_INFO_INT_TIME), \
}

static const struct iio_chan_spec tcs_channels[] = {
	TCS_CHANNEL("z", 0), TCS_CHANNEL("y", 1),
	TCS_CHANNEL("ir", 2), TCS_CHANNEL("x", 3),
};

static const struct iio_info tcs_info = {
	.read_raw = tcs_read_raw,
};

static void tcs_power_off(void *data)
{
	struct spx_tcs3430 *s = data;

	mutex_lock(&s->lock);
	if (!s->shutting_down)
		tcs_write(s, TCS_ENABLE, 0);
	mutex_unlock(&s->lock);
}

static void tcs_shutdown(struct i2c_client *client)
{
	struct spx_tcs3430 *s = i2c_get_clientdata(client);

	if (!s)
		return;
	mutex_lock(&s->lock);
	s->shutting_down = true;
	s->frame_valid = false;
	tcs_write(s, TCS_ENABLE, 0);
	mutex_unlock(&s->lock);
}

static int tcs_probe(struct i2c_client *client)
{
	struct spx_tcs3430 *s;
	struct iio_dev *indio;
	int ret;
	static const u8 config[][2] = {
		{TCS_ENABLE, 0}, {TCS_ATIME, TCS_CYCLES - 1},
		{TCS_CFG1, TCS_GAIN_DEFAULT_CODE}, {TCS_CFG2, 4}, {TCS_CFG3, 0x0c},
		{TCS_INTENAB, 0},
	};
	unsigned int i;

	if (!i2c_check_functionality(client->adapter,
				    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -EOPNOTSUPP;
	ret = i2c_smbus_read_byte_data(client, TCS_ID);
	if (ret < 0)
		return ret;
	if ((ret & 0xfc) != 0xdc)
		return dev_err_probe(&client->dev, -ENODEV, "not a TCS3430 (ID=%02x)\n", ret);
	indio = devm_iio_device_alloc(&client->dev, sizeof(*s));
	if (!indio)
		return -ENOMEM;
	s = iio_priv(indio);
	s->client = client;
	i2c_set_clientdata(client, s);
	s->gain_code = TCS_GAIN_DEFAULT_CODE;
	s->frame_gain = tcs_gains[TCS_GAIN_DEFAULT_CODE];
	mutex_init(&s->lock);
	for (i = 0; i < ARRAY_SIZE(config); i++) {
		ret = tcs_write(s, config[i][0], config[i][1]);
		if (ret < 0)
			return ret;
	}
	ret = devm_add_action_or_reset(&client->dev, tcs_power_off, s);
	if (ret)
		return ret;
	indio->name = "spx-tcs3430";
	indio->info = &tcs_info;
	indio->modes = INDIO_DIRECT_MODE;
	indio->channels = tcs_channels;
	indio->num_channels = ARRAY_SIZE(tcs_channels);
	return devm_iio_device_register(&client->dev, indio);
}

static const struct of_device_id tcs_of_match[] = {
	{ .compatible = "ams,tcs3430" }, {}
};
MODULE_DEVICE_TABLE(of, tcs_of_match);
static const struct i2c_device_id tcs_ids[] = {
	{ "tcs3430", 0 }, {}
};
MODULE_DEVICE_TABLE(i2c, tcs_ids);
static struct i2c_driver tcs_driver = {
	.driver = { .name = "spx-tcs3430", .of_match_table = tcs_of_match },
	.probe = tcs_probe, .shutdown = tcs_shutdown, .id_table = tcs_ids,
};
module_i2c_driver(tcs_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Surface Pro X TCS3430 direct XYZ/IR sensor");
