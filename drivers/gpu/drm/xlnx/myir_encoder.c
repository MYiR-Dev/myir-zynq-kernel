#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drmP.h>
#include <drm/drm_probe_helper.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <video/videomode.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>

#define PIXELS_PER_CLK 2

#define SII902X_ADDR 0x3B

#define MYIR_MAX_FREQ 150000
#define MYIR_MAX_H 1920
#define MYIR_MAX_V 1080
#define MYIR_PREF_H 800
#define MYIR_PREF_V 600

#define OFF 0
#define ON 1

#define connector_to_sdi(c) container_of(c, struct myir_drm_t, connector)
#define encoder_to_sdi(e) container_of(e, struct myir_drm_t, encoder)

char disp_type[8] = {}; //Display type, should be "HDMI" or "LCD"

struct myir_drm_t
{
    struct drm_encoder encoder;
    struct drm_connector connector;
    struct device *dev;

    u32 mode_flags;
    struct drm_display_mode video_mode;

    struct drm_property *sdi_mode;
    u32 sdi_mod_prop_val;
    struct drm_property *sdi_data_strm;
    u32 sdi_data_strm_prop_val;
    struct drm_property *height_out;
    u32 height_out_prop_val;
    struct drm_property *width_out;
    u32 width_out_prop_val;
    struct drm_property *in_fmt;
    u32 in_fmt_prop_val;
    struct drm_property *out_fmt;
    u32 out_fmt_prop_val;
    struct drm_property *is_frac_prop;
    bool is_frac_prop_val;

    struct clk *pixel_clock;
    u32 connect_type;
    u32 gpio;
    struct drm_display_mode *drm_dsp_mode;
    u32 drm_mode;
    struct i2c_adapter *i2c_bus;
    bool i2c_present;
    struct delayed_work hpd_work;
    struct drm_device *drm;
    int irq;
};

static const struct drm_display_mode myir_drm_lcd_default_mode = {
    .clock = 33330,
    .hdisplay = 800,
    .hsync_start = 800 + 44,
    .hsync_end = 800 + 44 + 210,
    .htotal = 800 + 44 + 210 + 2,
    .vdisplay = 480,
    .vsync_start = 480 + 21,
    .vsync_end = 480 + 21 + 22,
    .vtotal = 480 + 21 + 22 + 2,
    .vrefresh = 60,
    .flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
    .type = 0,
    .name = "800x480",
};

static const struct drm_display_mode myir_drm_hdmi_default_mode = {
    .clock = 148500,
    .hdisplay = 1920,
    .hsync_start = 1920 + 88,
    .hsync_end = 1920 + 88 + 44,
    .htotal = 1920 + 88 + 44 + 148,
    .vdisplay = 1080,
    .vsync_start = 1080 + 4,
    .vsync_end = 1080 + 4 + 5,
    .vtotal = 1080 + 4 + 5 + 36,
    .vrefresh = 60,
    .flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
    .type = 0,
    .name = "1920x1080",
};

static int sii902x_read_unlocked(struct i2c_adapter *adp, u8 reg, u8 *val)
{
    union i2c_smbus_data data;
    int ret;

    ret = __i2c_smbus_xfer(adp, SII902X_ADDR, 0,
                           I2C_SMBUS_READ, reg, I2C_SMBUS_BYTE_DATA, &data);

    if (ret < 0)
        return ret;

    *val = data.byte;
    return 0;
}

static int sii902x_write_unlocked(struct i2c_adapter *adp, u8 reg, u8 val)
{
    union i2c_smbus_data data;

    data.byte = val;

    return __i2c_smbus_xfer(adp, SII902X_ADDR, 0,
                            I2C_SMBUS_WRITE, reg, I2C_SMBUS_BYTE_DATA,
                            &data);
}

static int sii902x_update_bits_unlocked(struct i2c_adapter *adp, u8 reg, u8 mask,
                                        u8 val)
{
    int ret;
    u8 status;

    ret = sii902x_read_unlocked(adp, reg, &status);
    if (ret)
        return ret;
    status &= ~mask;
    status |= val & mask;
    return sii902x_write_unlocked(adp, reg, status);
}

static void sii902x_power(struct i2c_adapter *adp, char status)
{
    if (status == ON)
    {
        sii902x_write_unlocked(adp, 0x1A, 0x01);
    }
    else
        sii902x_write_unlocked(adp, 0x1A, 0x11);
}

static void sii902x_setup(struct i2c_adapter *adp, char *timing)
{
    int ret = 0;

    char i = 0;

    sii902x_write_unlocked(adp, 0xC7, 0x00);

    mdelay(2);

    sii902x_write_unlocked(adp, 0x1E, 0x00);
    sii902x_write_unlocked(adp, 0x08, 0x70);

    sii902x_write_unlocked(adp, 0x09, 0x06);
    sii902x_write_unlocked(adp, 0x19, 0x00);

    // if(IsYuv == 0) /* Set input format to RGB */
    // 	sii902x_write_unlocked(adp, 0x09, 0x00);
    // else /* Set input format to YUV */
    // {
    // 	sii902x_write_unlocked(adp, 0x09, 0x06);
    // 	sii902x_write_unlocked(adp, 0x19, 0x00);
    // }

    /* set output format to RGB */
    sii902x_write_unlocked(adp, 0x0A, 0x00);
    sii902x_write_unlocked(adp, 0x60, 0x04);
    sii902x_write_unlocked(adp, 0x3C, 0x01);

    /* Power Off */
    // sii902x_write_unlocked(adp, 0x1A, 0x11);
    sii902x_power(adp, OFF);

    /* Set Timing ...*/
    for (i = 0; i < 8; i++)
    {
        sii902x_write_unlocked(adp, i, timing[i]);
        // printk("0x%02x", timing[i]);
    }

    /* input bus/pixel: full pixel wide (24bit), rising edge */
    sii902x_write_unlocked(adp, 0x08, 0x70);

    /* Power On */
    // sii902x_write_unlocked(adp, 0x1A, 0x01);
    sii902x_power(adp, ON);
}

static int sii902x_edid_readblk(struct i2c_adapter *adp, u8 *edid)
{
    int ret = 0;
    int extblknum = 0;
    unsigned char regaddr = 0x0;

    struct i2c_msg msg[2] = {
        {
            .addr = 0x50,
            .flags = 0,
            .len = 1,
            .buf = &regaddr,
        },
        {
            .addr = 0x50,
            .flags = I2C_M_RD,
            .len = 128,
            .buf = edid,
        },
    };

    ret = i2c_transfer(adp, msg, ARRAY_SIZE(msg));
    if (ret != ARRAY_SIZE(msg))
    {
        printk("unable to read EDID block, ret=%d(expected:%d)\n", ret, ARRAY_SIZE(msg));

        return -1;
    }

    extblknum = edid[0x7E];

    if (extblknum)
    {
        msleep(20);
        regaddr = 128;
        msg[1].buf = edid + 128;

        ret = i2c_transfer(adp, msg, ARRAY_SIZE(msg));
        if (ret != ARRAY_SIZE(msg))
        {
            printk("unable to read EDID ext block, ret=%d(expected:%d)\n", ret, ARRAY_SIZE(msg));

            return -1;
        }
    }

    return 0;
}

static int sii902x_get_edid(struct i2c_adapter *adp, u8 *buf)
{
    int cnt = 100;
    u8 old, dat = 0;
    int ret = 0;

    /* Set 902x in hardware TPI mode on and jump out of D3 state */
    sii902x_write_unlocked(adp, 0xc7, 0);

    msleep(10);

    sii902x_read_unlocked(adp, 0x1A, &old);

    sii902x_write_unlocked(adp, 0x1A, old | 0x4);

    do
    {
        cnt--;
        msleep(10);
        sii902x_read_unlocked(adp, 0x1A, &dat);
    } while ((!(dat & 0x2)) && cnt);

    sii902x_write_unlocked(adp, 0x1A, old | 0x6);

    msleep(10);

    // edid = drm_get_edid(connector, adp);
    ret = sii902x_edid_readblk(adp, buf);

    if (ret < 0)
        return ret;

    // edid = (struct edid *)buf;

    cnt = 100;
    do
    {
        cnt--;
        sii902x_write_unlocked(adp, 0x1A, old & ~0x6);
        msleep(10);
        sii902x_read_unlocked(adp, 0x1A, &dat);
    } while ((dat & 0x6) && cnt);

    return 0;
}

static int myir_drm_atomic_set_property(struct drm_connector *connector,
                                        struct drm_connector_state *state,
                                        struct drm_property *property, uint64_t val)
{
    struct myir_drm_t *sdi = connector_to_sdi(connector);
    if (property == sdi->sdi_mode)
        sdi->sdi_mod_prop_val = (unsigned int)val;
    else if (property == sdi->sdi_data_strm)
        sdi->sdi_data_strm_prop_val = (unsigned int)val;
    else if (property == sdi->is_frac_prop)
        sdi->is_frac_prop_val = !!val;
    else if (property == sdi->height_out)
        sdi->height_out_prop_val = (unsigned int)val;
    else if (property == sdi->width_out)
        sdi->width_out_prop_val = (unsigned int)val;
    else if (property == sdi->in_fmt)
        sdi->in_fmt_prop_val = (unsigned int)val;
    else if (property == sdi->out_fmt)
        sdi->out_fmt_prop_val = (unsigned int)val;
    else
        return -EINVAL;
    return 0;
}

static int myir_drm_atomic_get_property(struct drm_connector *connector,
                                        const struct drm_connector_state *state,
                                        struct drm_property *property, uint64_t *val)
{
    struct myir_drm_t *sdi = connector_to_sdi(connector);
    if (property == sdi->sdi_mode)
        sdi->sdi_mod_prop_val = (unsigned int)val;
    else if (property == sdi->sdi_data_strm)
        *val = sdi->sdi_data_strm_prop_val;
    else if (property == sdi->is_frac_prop)
        sdi->is_frac_prop_val = !!val;
    else if (property == sdi->height_out)
        sdi->height_out_prop_val = (unsigned int)val;
    else if (property == sdi->width_out)
        sdi->width_out_prop_val = (unsigned int)val;
    else if (property == sdi->in_fmt)
        sdi->in_fmt_prop_val = (unsigned int)val;
    else if (property == sdi->out_fmt)
        sdi->out_fmt_prop_val = (unsigned int)val;
    else
        return -EINVAL;
    return 0;
}

static enum drm_connector_status
myir_drm_detect(struct drm_connector *connector, bool force)
{
    char status = -1;
    struct myir_drm_t *sdi = connector_to_sdi(connector);

    if (sdi->i2c_present == true)
    {
        sii902x_read_unlocked(sdi->i2c_bus, 0x3D, &status);

        return (status & 0x4) ? connector_status_connected : connector_status_disconnected;
    }
    else
        return connector_status_connected;
}

static void myir_drm_connector_destroy(struct drm_connector *connector)
{
    drm_connector_unregister(connector);
    drm_connector_cleanup(connector);
    connector->dev = NULL;
}

static const struct drm_connector_funcs myir_drm_connector_funcs = {
    .detect = myir_drm_detect,
    .fill_modes = drm_helper_probe_single_connector_modes,
    .destroy = myir_drm_connector_destroy,
    .atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
    .atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
    .reset = drm_atomic_helper_connector_reset,
    .atomic_set_property = myir_drm_atomic_set_property,
    .atomic_get_property = myir_drm_atomic_get_property,
};

static struct drm_encoder *
myir_drm_best_encoder(struct drm_connector *connector)
{
    return &(connector_to_sdi(connector)->encoder);
}

static int myir_drm_get_modes(struct drm_connector *connector)
{
    int ret = -1;
    struct drm_display_mode *mode;
    struct drm_device *dev = connector->dev;
    struct myir_drm_t *sdi = connector_to_sdi(connector);
    struct edid *edid;
    u8 buf[256] = {0};

    if (sdi->i2c_present == true) //HDMI
    {
        ret = sii902x_get_edid(sdi->i2c_bus, buf);

        edid = (struct edid *)buf;

        /*
        *Other drivers tend to call update edid property after the call to 
        *drm_add_edid_modes. If problems with modesetting, this could be why.
        */
        drm_connector_update_edid_property(connector, edid);
        if (edid && (ret == 0))
        {
            ret = drm_add_edid_modes(connector, edid);
            kfree(edid);
        }
        else //Failed to read edid, use default mode
        {
            mode = drm_mode_duplicate(dev, &myir_drm_hdmi_default_mode);
            drm_mode_probed_add(connector, mode);

            ret++;
        }

        // drm_set_preferred_mode(connector, MYIR_PREF_H, MYIR_PREF_V);
    }
    else //LCD
    {
        mode = drm_mode_duplicate(dev, &myir_drm_lcd_default_mode);
        drm_mode_probed_add(connector, mode);

        ret++;
    }
    return ret;
}

static int myir_drm_connector_mode_valid(struct drm_connector *connector,
                                         struct drm_display_mode *mode)
{
    struct myir_drm_t *sdi = connector_to_sdi(connector);

    if (mode->clock > MYIR_MAX_FREQ)
    {
        dev_dbg(sdi->dev, "filtered the mode, %s,for high pixel rate\n", mode->name);
        drm_mode_debug_printmodeline(mode);
        return MODE_CLOCK_HIGH;
    }

    if ((mode->hdisplay > MYIR_MAX_H) || (mode->vdisplay > MYIR_MAX_V))
        return MODE_BAD;

    return MODE_OK;
}

static struct drm_connector_helper_funcs myir_drm_connector_helper_funcs = {
    .get_modes = myir_drm_get_modes,
    .mode_valid = myir_drm_connector_mode_valid,
    .best_encoder = myir_drm_best_encoder,
};

static void myir_drm_drm_connector_create_property(struct drm_connector *base_connector)
{
    struct drm_device *dev = base_connector->dev;
    struct myir_drm_t *sdi = connector_to_sdi(base_connector);

    sdi->is_frac_prop = drm_property_create_bool(dev, 0, "is_frac");
    sdi->sdi_mode = drm_property_create_range(dev, 0, "sdi_mode", 0, 5);
    sdi->sdi_data_strm = drm_property_create_range(dev, 0, "sdi_data_stream", 2, 8);
    sdi->height_out = drm_property_create_range(dev, 0, "height_out", 2, 4096);
    sdi->width_out = drm_property_create_range(dev, 0, "width_out", 2, 4096);
    sdi->in_fmt = drm_property_create_range(dev, 0, "in_fmt", 0, 16384);
    sdi->out_fmt = drm_property_create_range(dev, 0, "out_fmt", 0, 16384);
}

static void myir_drm_drm_connector_attach_property(struct drm_connector *base_connector)
{
    struct myir_drm_t *sdi = connector_to_sdi(base_connector);
    struct drm_mode_object *obj = &base_connector->base;

    if (sdi->sdi_mode)
        drm_object_attach_property(obj, sdi->sdi_mode, 0);

    if (sdi->sdi_data_strm)
        drm_object_attach_property(obj, sdi->sdi_data_strm, 0);

    if (sdi->is_frac_prop)
        drm_object_attach_property(obj, sdi->is_frac_prop, 0);

    if (sdi->height_out)
        drm_object_attach_property(obj, sdi->height_out, 0);

    if (sdi->width_out)
        drm_object_attach_property(obj, sdi->width_out, 0);

    if (sdi->in_fmt)
        drm_object_attach_property(obj, sdi->in_fmt, 0);

    if (sdi->out_fmt)
        drm_object_attach_property(obj, sdi->out_fmt, 0);
}

static int myir_drm_create_connector(struct drm_encoder *encoder)
{
    struct myir_drm_t *sdi = encoder_to_sdi(encoder);
    struct drm_connector *connector = &sdi->connector;
    int ret;

    connector->interlace_allowed = true;
    connector->doublescan_allowed = true;

    if (sdi->connect_type == 1) //HDMI
    {
        connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

        /* To Do */
        // connector->polled = DRM_CONNECTOR_POLL_HPD;
    }

    ret = drm_connector_init(encoder->dev, connector,
                             &myir_drm_connector_funcs,
                             sdi->drm_mode);
    if (ret)
    {
        dev_err(sdi->dev, "Failed to initialize connector with drm\n");
        return ret;
    }

    drm_connector_helper_add(connector, &myir_drm_connector_helper_funcs);
    drm_connector_register(connector);
    drm_connector_attach_encoder(connector, encoder);
    myir_drm_drm_connector_create_property(connector);
    myir_drm_drm_connector_attach_property(connector);

    return 0;
}

static int myir_drm_dynclk_set(struct myir_drm_t *sdi, struct videomode *vm)
{
    int ret;

    /* set pixel clock */
    ret = clk_set_rate(sdi->pixel_clock, vm->pixelclock);
    if (ret)
    {
        DRM_ERROR("failed to set a pixel clock\n");
        return ret;
    }

    return 0;
}

static void myir_drm_encoder_atomic_mode_set(struct drm_encoder *encoder,
                                             struct drm_crtc_state *crtc_state,
                                             struct drm_connector_state *connector_state)
{
    struct myir_drm_t *sdi = encoder_to_sdi(encoder);
    struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
    struct videomode vm;
    u32 sditx_blank, vtc_blank;
    u16 sii902x_timing[4];

    vm.hactive = adjusted_mode->hdisplay / PIXELS_PER_CLK;
    vm.hfront_porch = (adjusted_mode->hsync_start - adjusted_mode->hdisplay) / PIXELS_PER_CLK;
    vm.hback_porch = (adjusted_mode->htotal - adjusted_mode->hsync_end) / PIXELS_PER_CLK;
    vm.hsync_len = (adjusted_mode->hsync_end - adjusted_mode->hsync_start) / PIXELS_PER_CLK;

    vm.vactive = adjusted_mode->vdisplay;
    vm.vfront_porch = adjusted_mode->vsync_start - adjusted_mode->vdisplay;
    vm.vback_porch = adjusted_mode->vtotal - adjusted_mode->vsync_end;
    vm.vsync_len = adjusted_mode->vsync_end - adjusted_mode->vsync_start;
    vm.flags = 0;
    if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
        vm.flags |= DISPLAY_FLAGS_INTERLACED;
    if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
        vm.flags |= DISPLAY_FLAGS_HSYNC_LOW;
    if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
        vm.flags |= DISPLAY_FLAGS_VSYNC_LOW;

    do
    {
        sditx_blank = (adjusted_mode->hsync_start -
                       adjusted_mode->hdisplay) +
                      (adjusted_mode->hsync_end -
                       adjusted_mode->hsync_start) +
                      (adjusted_mode->htotal -
                       adjusted_mode->hsync_end);

        vtc_blank = (vm.hfront_porch + vm.hback_porch +
                     vm.hsync_len) *
                    PIXELS_PER_CLK;

        if (vtc_blank != sditx_blank)
            vm.hfront_porch++;
    } while (vtc_blank < sditx_blank);

    vm.pixelclock = adjusted_mode->clock * 1000;

    // printk("sdi->video_mode.clock:%d\n", sdi->video_mode.clock);
    // printk("adjusted_mode->clock:%d\n", adjusted_mode->clock);

    // sdi->video_mode.vdisplay = adjusted_mode->vdisplay;
    // sdi->video_mode.hdisplay = adjusted_mode->hdisplay;
    // sdi->video_mode.vrefresh = adjusted_mode->vrefresh;
    // sdi->video_mode.flags = adjusted_mode->flags;

    drm_mode_copy(&sdi->video_mode, adjusted_mode);

    if (sdi->i2c_present == true) //HDMI
    {
        sii902x_timing[0] = adjusted_mode->clock / 10;
        sii902x_timing[1] = adjusted_mode->vrefresh * 100;
        sii902x_timing[2] = adjusted_mode->htotal;
        sii902x_timing[3] = adjusted_mode->vtotal;

        sii902x_setup(sdi->i2c_bus, (u8 *)sii902x_timing);
    }

    myir_drm_dynclk_set(sdi, &vm);
}

static void myir_drm_encoder_enable(struct drm_encoder *encoder)
{
    struct myir_drm_t *sdi = encoder_to_sdi(encoder);
}

static void myir_drm_encoder_disable(struct drm_encoder *encoder)
{
    struct myir_drm_t *sdi = encoder_to_sdi(encoder);
}

static bool myir_drm_encoder_mode_fixup(struct drm_encoder *encoder,
                                        const struct drm_display_mode *mode,
                                        struct drm_display_mode *adjusted_mode)
{
    struct myir_drm_t *sdi = encoder_to_sdi(encoder);

    // printk("mode->clock:%d\n", mode->clock);
    // printk("adjusted_mode->clock:%d\n", adjusted_mode->clock);

    // drm_mode_copy(&sdi->video_mode, adjusted_mode);

    return true;
}

static const struct drm_encoder_helper_funcs myir_drm_encoder_helper_funcs = {
    .atomic_mode_set = myir_drm_encoder_atomic_mode_set,
    .enable = myir_drm_encoder_enable,
    .disable = myir_drm_encoder_disable,
    .mode_fixup = myir_drm_encoder_mode_fixup,
};

static const struct drm_encoder_funcs myir_drm_encoder_funcs = {
    .destroy = drm_encoder_cleanup,
};

static void myir_drm_hpd_work_func(struct work_struct *work)
{
    struct myir_drm_t *sdi;

    sdi = container_of(work, struct myir_drm_t, hpd_work.work);

    if (sdi->drm)
        drm_helper_hpd_irq_event(sdi->drm);
}

static irqreturn_t myir_drm_irq_thread(int irq, void *arg)
{
    struct myir_drm_t *sdi = arg;

    char status = 0;

    sii902x_read_unlocked(sdi->i2c_bus, 0x3d, &status);
    sii902x_write_unlocked(sdi->i2c_bus, 0x3d, status);

    schedule_delayed_work(&sdi->hpd_work, msecs_to_jiffies(20));
    // mod_delayed_work(system_wq, &sdi->hpd_work, msecs_to_jiffies(1100));

    return IRQ_HANDLED;
}

static int myir_drm_bind(struct device *dev, struct device *master,
                         void *data)
{
    struct myir_drm_t *sdi = dev_get_drvdata(dev);
    struct drm_encoder *encoder = &sdi->encoder;
    struct drm_device *drm_dev = data;
    int ret;

    encoder->possible_crtcs = 1;

    drm_encoder_init(drm_dev, encoder, &myir_drm_encoder_funcs,
                     DRM_MODE_ENCODER_TMDS, NULL);

    drm_encoder_helper_add(encoder, &myir_drm_encoder_helper_funcs);

    INIT_DELAYED_WORK(&sdi->hpd_work, myir_drm_hpd_work_func);

    ret = myir_drm_create_connector(encoder);
    if (ret)
    {
        dev_err(sdi->dev, "fail creating connector, ret = %d\n", ret);
        drm_encoder_cleanup(encoder);
    }
    return ret;
}

static void myir_drm_unbind(struct device *dev, struct device *master,
                            void *data)
{
    struct myir_drm_t *sdi = dev_get_drvdata(dev);

    // disable_irq(sdi->irq);
    cancel_delayed_work_sync(&sdi->hpd_work);
    drm_encoder_cleanup(&sdi->encoder);
    drm_connector_cleanup(&sdi->connector);
}

static const struct component_ops myir_drm_component_ops = {
    .bind = myir_drm_bind,
    .unbind = myir_drm_unbind,
};

static int myir_drm_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct myir_drm_t *sdi;
    int ret;
    struct device_node *ports, *port, *sub_node;
    u32 nports = 0, portmask = 0;
    const char *con_type;
    char status = 0;

    sdi = devm_kzalloc(dev, sizeof(*sdi), GFP_KERNEL);
    if (!sdi)
        return -ENOMEM;

    sdi->dev = dev;

    sdi->pixel_clock = devm_clk_get(sdi->dev, NULL);
    if (IS_ERR(sdi->pixel_clock))
    {
        if (PTR_ERR(sdi->pixel_clock) == -EPROBE_DEFER)
        {
            ret = PTR_ERR(sdi->pixel_clock);
            // goto err_plane;
        }
        else
        {
            DRM_DEBUG_KMS("failed to get pixel clock\n");
            sdi->pixel_clock = NULL;
        }
    }

    sdi->gpio = of_get_gpio(dev->of_node, 0);
    if (!gpio_is_valid(sdi->gpio))
        dev_warn(dev, "GPIO not specified in DT (of_get_gpio returned %d)\n", sdi->gpio);
    else
    {
        gpio_request(sdi->gpio, "dis_swt");
        gpio_direction_output(sdi->gpio, 1);
    }

    if (disp_type[0] != 0) // use kernel command-line parameters - "display_type"
    {
        con_type = disp_type;
    }
    else
    {
        ret = of_property_read_string(dev->of_node, "myir,connector-type", &con_type);
        if (ret)
        {
            dev_err(dev, "No myir,connector-type value in dts\n");
            ret = -EINVAL;
            return ret;
        }
    }

    sdi->i2c_present = false;

    if ((strcmp(con_type, "HDMI") == 0) || (strcmp(con_type, "hdmi") == 0)) //HDMI mode
    {
        sdi->connect_type = 1;
        gpio_set_value(sdi->gpio, 1);
        // sdi->drm_dsp_mode = &myir_drm_hdmi_mode;
        sdi->drm_mode = DRM_MODE_CONNECTOR_HDMIA;
        sdi->i2c_present = true;

        sub_node = of_parse_phandle(dev->of_node, "ddc-i2c-bus", 0);
        of_node_put(sub_node);
        if (sub_node)
        {
            sdi->i2c_bus = of_find_i2c_adapter_by_node(sub_node);

            if (!sdi->i2c_bus)
                printk("failed to get the edid i2c adapter, using default modes\n");
            else
                sii902x_write_unlocked(sdi->i2c_bus, 0xC7, 0x00);
        }

        sdi->irq = platform_get_irq(pdev, 0);
        if (sdi->irq < 0)
        {
            DRM_DEV_ERROR(dev, "failed to get irq\n");
        }

        sii902x_read_unlocked(sdi->i2c_bus, 0x3d, &status);
        sii902x_write_unlocked(sdi->i2c_bus, 0x3d, status);

        if (sdi->irq > 0)
        {
            sii902x_write_unlocked(sdi->i2c_bus, 0x3C, 0x01);

            ret = devm_request_threaded_irq(sdi->dev, sdi->irq, NULL,
                                            myir_drm_irq_thread, IRQF_ONESHOT,
                                            dev_name(sdi->dev), sdi);
            if (ret < 0)
                DRM_DEV_ERROR(dev, "failed to request irq\n");
        }
    }
    else if ((strcmp(con_type, "LCD") == 0) || ((strcmp(con_type, "lcd") == 0)))
    {
        sdi->connect_type = 0;
        gpio_set_value(sdi->gpio, 0);
        // sdi->drm_dsp_mode = &myir_drm_lcd_mode;
        sdi->drm_mode = DRM_MODE_CONNECTOR_Unknown;
        sdi->i2c_present = false;
    }
    else
    {
        dev_err(dev, "Wrong myir,connector-type value in dts, must be HDMI or LCD\n");
        return -EINVAL;
    }

    platform_set_drvdata(pdev, sdi);

    ports = of_get_child_by_name(sdi->dev->of_node, "ports");
    if (!ports)
    {
        dev_dbg(dev, "Searching for port nodes in device node.\n");
        ports = sdi->dev->of_node;
    }

    for_each_child_of_node(ports, port)
    {
        struct device_node *endpoint;
        u32 index;

        if (!port->name || of_node_cmp(port->name, "port"))
        {
            dev_dbg(dev, "port name is null or node name is not port!\n");
            continue;
        }

        endpoint = of_get_next_child(port, NULL);
        if (!endpoint)
        {
            dev_err(dev, "No remote port at %s\n", port->name);
            of_node_put(endpoint);
            ret = -EINVAL;
            return ret;
        }

        of_node_put(endpoint);

        ret = of_property_read_u32(port, "reg", &index);
        if (ret)
        {
            dev_err(dev, "reg property not present - %d\n", ret);
            return ret;
        }

        portmask |= (1 << index);

        nports++;
    }

    if (nports == 1 && portmask & 0x1)
    {
        dev_dbg(dev, "no ancillary port\n");
    }
    else
    {
        dev_err(dev, "Incorrect dt node!\n");
        ret = -EINVAL;
        return ret;
    }

    pdev->dev.platform_data = &sdi->video_mode;

    ret = component_add(dev, &myir_drm_component_ops);

    return ret;
}

static int myir_drm_remove(struct platform_device *pdev)
{
    component_del(&pdev->dev, &myir_drm_component_ops);

    return 0;
}

static const struct of_device_id myir_drm_of_match[] = {
    {.compatible = "myir,drm-encoder"},
    {}};
MODULE_DEVICE_TABLE(of, myir_drm_of_match);

static struct platform_driver sdi_tx_driver = {
    .probe = myir_drm_probe,
    .remove = myir_drm_remove,
    .driver = {
        .name = "myir_encoder",
        .of_match_table = myir_drm_of_match,
    },
};

module_platform_driver(sdi_tx_driver);

module_param_string(display_type, disp_type, sizeof(disp_type), 0664);
MODULE_PARM_DESC(display_type, "A kernel command-line parameters of display type, should be HDMI or LCD");

MODULE_AUTHOR("Calvin<calvin.liu@myirtech.com>");
MODULE_DESCRIPTION("MYIR drm encoder driver");
MODULE_LICENSE("GPL v2");
