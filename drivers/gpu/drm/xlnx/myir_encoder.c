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

#define PIXELS_PER_CLK 2

#define connector_to_sdi(c) container_of(c, struct myir_drm_t, connector)
#define encoder_to_sdi(e) container_of(e, struct myir_drm_t, encoder)

struct myir_drm_t
{
    struct drm_encoder encoder;
    struct drm_connector connector;
    struct device *dev;

    u32 mode_flags;
    struct drm_display_mode video_mode;

    struct drm_property *sdi_mode;
    u32 sdi_mod_prop_val;
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
};

static const struct drm_display_mode myir_drm_lcd_mode = {
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

static const struct drm_display_mode myir_drm_hdmi_mode = {
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

static int myir_drm_atomic_set_property(struct drm_connector *connector,
                                        struct drm_connector_state *state,
                                        struct drm_property *property, uint64_t val)
{
    struct myir_drm_t *sdi = connector_to_sdi(connector);
    if (property == sdi->sdi_mode)
        sdi->sdi_mod_prop_val = (unsigned int)val;
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

static int myir_drm_drm_add_modes(struct drm_connector *connector)
{
    int num_modes = 0;
    struct drm_display_mode *mode;
    struct drm_device *dev = connector->dev;
    struct myir_drm_t *sdi = connector_to_sdi(connector);

    if(sdi->connect_type == 1)
        mode = drm_mode_duplicate(dev, &myir_drm_hdmi_mode);
    else
        mode = drm_mode_duplicate(dev, &myir_drm_lcd_mode);
    drm_mode_probed_add(connector, mode);
    num_modes++;

    return num_modes;
}

static enum drm_connector_status
myir_drm_detect(struct drm_connector *connector, bool force)
{
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
    return myir_drm_drm_add_modes(connector);
}

static struct drm_connector_helper_funcs myir_drm_connector_helper_funcs = {
    .get_modes = myir_drm_get_modes,
    .best_encoder = myir_drm_best_encoder,
};

static void myir_drm_drm_connector_create_property(struct drm_connector *base_connector)
{
    struct drm_device *dev = base_connector->dev;
    struct myir_drm_t *sdi = connector_to_sdi(base_connector);

    sdi->is_frac_prop = drm_property_create_bool(dev, 0, "is_frac");
    sdi->sdi_mode = drm_property_create_range(dev, 0, "sdi_mode", 0, 5);
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

    ret = drm_connector_init(encoder->dev, connector,
                             &myir_drm_connector_funcs,
                             DRM_MODE_CONNECTOR_Unknown);
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

    sdi->video_mode.vdisplay = adjusted_mode->vdisplay;
    sdi->video_mode.hdisplay = adjusted_mode->hdisplay;
    sdi->video_mode.vrefresh = adjusted_mode->vrefresh;
    sdi->video_mode.flags = adjusted_mode->flags;

    myir_drm_dynclk_set(sdi, &vm);
}

static void myir_drm_commit(struct drm_encoder *encoder)
{
    struct myir_drm_t *sdi = encoder_to_sdi(encoder);
}

static void myir_drm_disable(struct drm_encoder *encoder)
{
    struct myir_drm_t *sdi = encoder_to_sdi(encoder);
}

static const struct drm_encoder_helper_funcs myir_drm_encoder_helper_funcs = {
    .atomic_mode_set = myir_drm_encoder_atomic_mode_set,
    .enable = myir_drm_commit,
    .disable = myir_drm_disable,
};

static const struct drm_encoder_funcs myir_drm_encoder_funcs = {
    .destroy = drm_encoder_cleanup,
};

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
    struct device_node *ports, *port;
    u32 nports = 0, portmask = 0;
    const char *con_type;

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

    ret = of_property_read_string(dev->of_node, "myir,connector-type", &con_type);
	if (ret) {
		dev_err(dev, "No myir,connector-type value in dts\n");
		ret = -EINVAL;
        return ret;
	}

    if (strcmp(con_type, "HDMI") == 0)
        sdi->connect_type = 1;
    else if(strcmp(con_type, "LCD") == 0)
        sdi->connect_type = 0;
    else
        dev_err(dev, "Wrong myir,connector-type value in dts, must be HDMI or LCD\n");

    sdi->gpio = of_get_gpio(dev->of_node, 0);
    if (!gpio_is_valid(sdi->gpio))
		dev_warn(dev, "GPIO not specified in DT (of_get_gpio returned %d)\n", sdi->gpio);
    else
    {
        gpio_request(sdi->gpio,"dis_swt");
        gpio_direction_output(sdi->gpio,1);
    }

    if(sdi->connect_type == 1) //HDMI mode
        gpio_set_value(sdi->gpio,1);
    else
        gpio_set_value(sdi->gpio,0);

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
    {.compatible = "myir-drm-encoder"},
    {}};
MODULE_DEVICE_TABLE(of, myir_drm_of_match);

static struct platform_driver sdi_tx_driver = {
    .probe = myir_drm_probe,
    .remove = myir_drm_remove,
    .driver = {
        .name = "myir-drm-encoder",
        .of_match_table = myir_drm_of_match,
    },
};

module_platform_driver(sdi_tx_driver);

MODULE_AUTHOR("Calvin<calvin.liu@myirtech.com>");
MODULE_DESCRIPTION("MYIR drm encoder driver");
MODULE_LICENSE("GPL v2");
