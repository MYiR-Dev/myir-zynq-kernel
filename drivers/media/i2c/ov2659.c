#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

/* ov2659 registers */
#define REG_CHIP_ID_HIGH		0x300a
#define REG_CHIP_ID_LOW			0x300b


/* active pixel array size */
#define ov2659_SENSOR_SIZE_X	2592
#define ov2659_SENSOR_SIZE_Y	1944

/*
 * About ov2659 resolution, cropping and binning:
 * This sensor supports it all, at least in the feature description.
 * Unfortunately, no combination of appropriate registers settings could make
 * the chip work the intended way. As it works with predefined register lists,
 * some undocumented registers are presumably changed there to achieve their
 * goals.
 * This driver currently only works for resolutions up to 720 lines with a
 * 1:1 scale. Hopefully these restrictions will be removed in the future.
 */
#define ov2659_MAX_WIDTH	ov2659_SENSOR_SIZE_X
#define ov2659_MAX_HEIGHT	720

/* default sizes */
#define ov2659_DEFAULT_WIDTH	1280
#define ov2659_DEFAULT_HEIGHT	ov2659_MAX_HEIGHT

/* minimum extra blanking */
#define BLANKING_EXTRA_WIDTH		500
#define BLANKING_EXTRA_HEIGHT		20

/*
 * the sensor's autoexposure is buggy when setting total_height low.
 * It tries to expose longer than 1 frame period without taking care of it
 * and this leads to weird output. So we set 1000 lines as minimum.
 */
#define BLANKING_MIN_HEIGHT		1000

struct regval_list {
    u16 reg_num;
    u8 value;
};

static struct regval_list ov2659_default_regs_init[] = {
{0x3000, 0x03},
{0x3001, 0xff},
{0x3002, 0xe0},
{0x3633, 0x3d},
{0x3620, 0x02},
{0x3631, 0x11},
{0x3612, 0x04},
{0x3630, 0x20},
{0x4702, 0x02},
{0x370c, 0x34},
{0x3004, 0x10},
{0x3005, 0x22},
{0x3800, 0x00},
{0x3801, 0x00},
{0x3802, 0x00},
{0x3803, 0x00},
{0x3804, 0x06},
{0x3805, 0x5f},
{0x3806, 0x04},
{0x3807, 0xb7},
{0x3808, 0x03},
{0x3809, 0x20},
{0x380a, 0x02},
{0x380b, 0x58},
{0x380c, 0x05},
{0x380d, 0x14},
{0x380e, 0x02},
{0x380f, 0x68},
{0x3811, 0x08},
{0x3813, 0x02},
{0x3814, 0x31},
{0x3815, 0x31},
{0x3a02, 0x02},
{0x3a03, 0x68},
{0x3a08, 0x00},
{0x3a09, 0x5c},
{0x3a0a, 0x00},
{0x3a0b, 0x4d},
{0x3a0d, 0x08},
{0x3a0e, 0x06},
{0x3a14, 0x02},
{0x3a15, 0x28},
{0x4708, 0x01},
{0x3623, 0x00},
{0x3634, 0x76},
{0x3701, 0x44},
{0x3702, 0x18},
{0x3703, 0x24},
{0x3704, 0x24},
{0x3705, 0x0c},
{0x3820, 0x81},
{0x3821, 0x01},
{0x370a, 0x52},
{0x4608, 0x00},
{0x4609, 0x80},
{0x4300, 0x30},
{0x5086, 0x02},
{0x5000, 0xfb},
{0x5001, 0x1f},
{0x5002, 0x00},
{0x5025, 0x0e},
{0x5026, 0x18},
{0x5027, 0x34},
{0x5028, 0x4c},
{0x5029, 0x62},
{0x502a, 0x74},
{0x502b, 0x85},
{0x502c, 0x92},
{0x502d, 0x9e},
{0x502e, 0xb2},
{0x502f, 0xc0},
{0x5030, 0xcc},
{0x5031, 0xe0},
{0x5032, 0xee},
{0x5033, 0xf6},
{0x5034, 0x11},
{0x5070, 0x1c},
{0x5071, 0x5b},
{0x5072, 0x05},
{0x5073, 0x20},
{0x5074, 0x94},
{0x5075, 0xb4},
{0x5076, 0xb4},
{0x5077, 0xaf},
{0x5078, 0x05},
{0x5079, 0x98},
{0x507a, 0x21},
{0x5035, 0x6a},
{0x5036, 0x11},
{0x5037, 0x92},
{0x5038, 0x21},
{0x5039, 0xe1},
{0x503a, 0x01},
{0x503c, 0x05},
{0x503d, 0x08},
{0x503e, 0x08},
{0x503f, 0x64},
{0x5040, 0x58},
{0x5041, 0x2a},
{0x5042, 0xc5},
{0x5043, 0x2e},
{0x5044, 0x3a},
{0x5045, 0x3c},
{0x5046, 0x44},
{0x5047, 0xf8},
{0x5048, 0x08},
{0x5049, 0x70},
{0x504a, 0xf0},
{0x504b, 0xf0},
{0x500c, 0x03},
{0x500d, 0x20},
{0x500e, 0x02},
{0x500f, 0x5c},
{0x5010, 0x48},
{0x5011, 0x00},
{0x5012, 0x66},
{0x5013, 0x03},
{0x5014, 0x30},
{0x5015, 0x02},
{0x5016, 0x7c},
{0x5017, 0x40},
{0x5018, 0x00},
{0x5019, 0x66},
{0x501a, 0x03},
{0x501b, 0x10},
{0x501c, 0x02},
{0x501d, 0x7c},
{0x501e, 0x3a},
{0x501f, 0x00},
{0x5020, 0x66},
{0x506e, 0x44},
{0x5064, 0x08},
{0x5065, 0x10},
{0x5066, 0x12},
{0x5067, 0x02},
{0x506c, 0x08},
{0x506d, 0x10},
{0x506f, 0xa6},
{0x5068, 0x08},
{0x5069, 0x10},
{0x506a, 0x04},
{0x506b, 0x12},
{0x507e, 0x40},
{0x507f, 0x20},
{0x507b, 0x02},
{0x507a, 0x01},
{0x5084, 0x0c},
{0x5085, 0x3e},
{0x5005, 0x80},
{0x3a0f, 0x30},
{0x3a10, 0x28},
{0x3a1b, 0x32},
{0x3a1e, 0x26},
{0x3a11, 0x60},
{0x3a1f, 0x14},
{0x5060, 0x69},
{0x5061, 0x7d},
{0x5062, 0x7d},
{0x5063, 0x69},
{0x0000, 0x00},

/* Output Format Configuration
 * 0x00 -- RAW Bayer BGGR <== Verified
 * 0x30 -- YUV422 YUYV    <== Verified
 * 0x32 -- YUV422 UYVY    <== Verified
 * 0x40 -- YUV420         <== Does not appear to be supported
 * 0x50 -- YUV420 Legacy  <== Does not appear to be supported
 * 0x60 -- RGB565         <== Not Verified yet
 */
{0x4300, 0x60},

{0x3800, 0x00},
{0x3801, 0xa0},
{0x3802, 0x00},
{0x3803, 0xf0},
{0x3804, 0x05},
{0x3805, 0xbf},
{0x3806, 0x03},
{0x3807, 0xcb},
{0x3808, 0x05},
{0x3809, 0x00},
{0x380a, 0x02},
{0x380b, 0xd0},
{0x380c, 0x06},
{0x380d, 0x4c},
{0x380e, 0x02},
{0x380f, 0xe8},
{0x3811, 0x10},
{0x3813, 0x06},
{0x3814, 0x11},
{0x3815, 0x11},
{0x3820, 0x80},
{0x3821, 0x00},
{0x3a03, 0xe8},
{0x3a09, 0x6f},
{0x3a0b, 0x5d},
{0x3a15, 0x9a},
{0x0000, 0x00},

{0x3005, 0x18},
{0x3006, 0x05},

{0x3000, 0x03},
{0x3001, 0xff},
{0x3002, 0xe0},
{0x0100, 0x01},
{0xffff,0xff}, //end of array
};

struct ov2659_datafmt {
    u32	code;
    enum v4l2_colorspace		colorspace;
};

struct ov2659 {
    struct v4l2_subdev		subdev;
    struct media_pad pad;
    const struct ov2659_datafmt	*fmt;
    struct v4l2_rect crop_rect;
    struct i2c_client *client;

    /* blanking information */
    int total_width;
    int total_height;
    enum v4l2_colorspace colorspace;
	
	struct gpio_desc *reset_gpio;
	struct mutex lock; /* mutex lock for operations */
};

static const struct ov2659_datafmt ov2659_colour_fmts[] = {
{MEDIA_BUS_FMT_RBG888_1X24, V4L2_COLORSPACE_JPEG},
// {MEDIA_BUS_FMT_UYVY8_1X16, V4L2_COLORSPACE_SRGB},
// {MEDIA_BUS_FMT_UYVY8_1X16, V4L2_COLORSPACE_JPEG},
// {MEDIA_BUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_SRGB},
};

static struct ov2659 *to_ov2659(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct ov2659, subdev);
}

/*
 * ov2659_reset - Function called to reset the sensor
 * @priv: Pointer to device structure
 * @rst: Input value for determining the sensor's end state after reset
 *
 * Set the senor in reset and then
 * if rst = 0, keep it in reset;
 * if rst = 1, bring it out of reset.
 *
 */
static void ov2659_reset(struct ov2659 *priv, int rst)
{
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(2000, 2200);
	gpiod_set_value_cansleep(priv->reset_gpio, !!rst);
	usleep_range(2000, 2200);
}

/* Find a data format by a pixel code in an array */
static const struct ov2659_datafmt
        *ov2659_find_datafmt(u32 code)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ov2659_colour_fmts); i++)
        if (ov2659_colour_fmts[i].code == code)
            return ov2659_colour_fmts + i;

    return NULL;
}

static int reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
    int ret;
    /* We have 16-bit i2c addresses - care for endianness */
    unsigned char data[2] = { reg >> 8, reg & 0xff };

    ret = i2c_master_send(client, data, 2);
    if (ret < 2) {
        dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
                __func__, reg);
        return ret < 0 ? ret : -EIO;
    }

    ret = i2c_master_recv(client, val, 1);
    if (ret < 1) {
        dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
                __func__, reg);
        return ret < 0 ? ret : -EIO;
    }
    return 0;
}

static int reg_write(struct i2c_client *client, u16 reg, u8 val)
{
    int ret;
    unsigned char data[3] = { reg >> 8, reg & 0xff, val };

    ret = i2c_master_send(client, data, 3);
    if (ret < 3) {
        dev_err(&client->dev, "%s: i2c write error, reg: %x\n",
                __func__, reg);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

/*
 * convenience function to write 16 bit register values that are split up
 * into two consecutive high and low parts
 */
static int reg_write16(struct i2c_client *client, u16 reg, u16 val16)
{
    int ret;

    ret = reg_write(client, reg, val16 >> 8);
    if (ret)
        return ret;
    return reg_write(client, reg + 1, val16 & 0x00ff);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov2659_get_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    int ret;
    u8 val;

    if (reg->reg & ~0xffff)
        return -EINVAL;

    reg->size = 1;

    ret = reg_read(client, reg->reg, &val);
    if (!ret)
        reg->val = (__u64)val;

    return ret;
}

static int ov2659_set_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);

    if (reg->reg & ~0xffff || reg->val & ~0xff)
        return -EINVAL;

    return reg_write(client, reg->reg, reg->val);
}
#endif

static int ov2659_write_array(struct i2c_client *client,
                              struct regval_list *vals)
{
    while (vals->reg_num != 0xffff || vals->value != 0xff) {
        int ret = reg_write(client, vals->reg_num, vals->value);
        if (ret < 0)
            return ret;
        vals++;

		usleep_range(5000, 6000);
    }
    dev_dbg(&client->dev, "Register list loaded\n");
    return 0;
}

static int ov2659_video_probe(struct i2c_client *client)
{
    struct v4l2_subdev *subdev = i2c_get_clientdata(client);
    int ret;
    u8 id_high, id_low;
    u16 id;

	// /* soft reset */
	// ret = reg_write(client, REG_SOFTWARE_RESET, 0x01);
	// if (ret != 0) {
	// 	dev_err(&client->dev, "Sensor soft reset %s failed\n",
	// 		"ov2569");
	// 	ret = -ENODEV;
	// }
	// mdelay(5);		/* delay 5 microseconds */

    /* Read sensor Model ID */
    ret = reg_read(client, REG_CHIP_ID_HIGH, &id_high);
    if (ret < 0)
        goto done;

    id = id_high << 8;

    ret = reg_read(client, REG_CHIP_ID_LOW, &id_low);
    if (ret < 0)
        goto done;

    id |= id_low;

    dev_info(&client->dev, "Chip ID 0x%04x detected\n", id);

    if (id != 0x2659) {
        ret = -ENODEV;
        goto done;
    }

    ret = 0;

done:
    return ret;
}

static int ov2659_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2659 *priv = to_ov2659(client);

	const struct ov2659_datafmt *fmt = priv->fmt;

	if (format->pad)
		return -EINVAL;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->width	= priv->crop_rect.width;
	mf->height	= priv->crop_rect.height;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov2659_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2659 *priv = to_ov2659(client);
	const struct ov2659_datafmt *fmt = ov2659_find_datafmt(mf->code);

    priv->crop_rect.width   = format->format.width;
    priv->crop_rect.height  = format->format.height;
    priv->crop_rect.left    = (ov2659_MAX_WIDTH - format->format.width) / 2;
    priv->crop_rect.top     = (ov2659_MAX_HEIGHT - format->format.height) / 2;
    priv->total_width       = format->format.width + BLANKING_EXTRA_WIDTH;
    priv->total_height      = format->format.height + BLANKING_EXTRA_HEIGHT;
    priv->colorspace        = format->format.colorspace;

    printk(KERN_WARNING "++++++++++reay set format %dx%d++++++++++\n",
           priv->crop_rect.width,
           priv->crop_rect.height);

    // reg_write(client, 0x3808, (format->format.width>>8)&0xff);
    // reg_write(client, 0x3809, (format->format.width>>0)&0xff);
    // reg_write(client, 0x380a, (format->format.height>>8)&0xff);
    // reg_write(client, 0x380b, (format->format.height>>0)&0xff);

    return 0; /* FIXME */
}

static int ov2659_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov2659_colour_fmts))
		return -EINVAL;

	code->code = ov2659_colour_fmts[code->index].code;
	return 0;
}

static int ov2659_s_power(struct v4l2_subdev *sd, int on)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	ret = ov2659_write_array(client, ov2659_default_regs_init);

	return ret;
}
static int ov2659_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	return ret;
}
static struct v4l2_subdev_video_ops ov2659_subdev_video_ops = {
	.s_stream = ov2659_s_stream,	
};

static const struct v4l2_subdev_pad_ops ov2659_subdev_pad_ops = {
	.enum_mbus_code = ov2659_enum_mbus_code,
	.get_fmt	= ov2659_get_fmt,
	.set_fmt	= ov2659_set_fmt,
};

static struct v4l2_subdev_core_ops ov2659_subdev_core_ops = {
	.s_power	= ov2659_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov2659_get_register,
	.s_register	= ov2659_set_register,
#endif
};


static const struct v4l2_subdev_ops ov2659_ops = {
	.core	= &ov2659_subdev_core_ops,
	.video	= &ov2659_subdev_video_ops,
    .pad   = &ov2659_subdev_pad_ops,
};

static int ov2659_probe(struct i2c_client *client,
                        const struct i2c_device_id *did)
{
    struct ov2659 *priv;
    int ret;
    priv = devm_kzalloc(&client->dev, sizeof(struct ov2659), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->fmt               = &ov2659_colour_fmts[0];
    priv->crop_rect.width   = ov2659_DEFAULT_WIDTH;
    priv->crop_rect.height  = ov2659_DEFAULT_HEIGHT;
    priv->crop_rect.left    = (ov2659_MAX_WIDTH - ov2659_DEFAULT_WIDTH) / 2;
    priv->crop_rect.top     = (ov2659_MAX_HEIGHT - ov2659_DEFAULT_HEIGHT) / 2;
    priv->total_width       = ov2659_DEFAULT_WIDTH + BLANKING_EXTRA_WIDTH;
    priv->total_height      = BLANKING_MIN_HEIGHT;
    priv->colorspace        = 8;

    priv->client = client;
    priv->client->flags = I2C_CLIENT_SCCB;

    v4l2_i2c_subdev_init(&priv->subdev, client, &ov2659_ops);

    /* V4L init */
    strcpy(priv->subdev.name, "ov2659");

    priv->pad.flags = MEDIA_PAD_FL_SOURCE;
    priv->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
    ret = media_entity_pads_init(&priv->subdev.entity, 1, &priv->pad);
    if (ret) {
        v4l_err(client, "Failed to initialized pad\n");
        goto done;
    }
	
	/* initialize sensor reset gpio */
	priv->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset-gpios",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio)) {
		if (PTR_ERR(priv->reset_gpio) != -EPROBE_DEFER)
			dev_err(&client->dev, "Reset GPIO not setup in DT");
		ret = PTR_ERR(priv->reset_gpio);
	}
	
	/* pull sensor out of reset */
	ov2659_reset(priv, 1);
	
	ret = ov2659_write_array(client, ov2659_default_regs_init);

    ret = v4l2_async_register_subdev(&priv->subdev);
    if (ret) {
        v4l_err(client, "Failed to register subdev for async detection\n");
        goto done;
    }

done:
    if (ret > 0) {
        media_entity_cleanup(&priv->subdev.entity);
    }

    ov2659_video_probe(client);

err_me:
	//media_entity_cleanup(&sd->entity);
	//mutex_destroy(&priv->lock);
	return ret;
}

static int ov2659_remove(struct i2c_client *client)
{
    return 0;
}

static const struct of_device_id ov2659_of_id_table[] = {
	{ .compatible = "myir,ov2659" },
	{ }
};
MODULE_DEVICE_TABLE(of, ov2659_of_id_table);

static const struct i2c_device_id ov2659_id[] = {
	{ "ov2659", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov2659_id);

static struct i2c_driver ov2659_i2c_driver = {
    .driver = {
        .name = "ov2659",
		.of_match_table	= ov2659_of_id_table,
    },
    .probe		= ov2659_probe,
    .remove		= ov2659_remove,
    .id_table	= ov2659_id,
};

module_i2c_driver(ov2659_i2c_driver);

MODULE_DESCRIPTION("Omnivision ov2659 Camera driver");
MODULE_AUTHOR("myir");
MODULE_LICENSE("GPL v2");


