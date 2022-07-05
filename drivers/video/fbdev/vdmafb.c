/*
 * MYIR AXI VDMA frame buffer driver
 * Based on: vdmafb.c
 *
 * Copyright (C) 2019 MYIR tech
 * Author: calvin <calvin.liu@myirtech.com>
 *
 * Licensed under the GPL-2.
 *
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/dma/xilinx_dma.h>
#include <asm-generic/gpio.h>

#include "vdmafb.h"

static char *fmrfb_mode_option;

struct vdmafb_dev
{
	struct fb_info info;
	/* Physical and virtual addresses of framebuffer */
	phys_addr_t fb_phys;
	void __iomem *fb_virt;
	/* VDMA handle */
	struct dma_chan *dma;
	struct dma_interleaved_template *dma_template;
	/* Palette data */
	u32 pseudo_palette[16];
};

static int vdmafb_parse_hw_info(struct device_node *np,
								struct vdmafb_init_data *init_data)
{
	u32 const *prop;
	int size;

	prop = of_get_property(np, "fmr,display-interface", &size);
	if (!prop)
	{
		pr_err("Error fmrfb getting display interface\n");
		return -EINVAL;
	}
	init_data->display_interface_type = be32_to_cpup(prop) << 4;

	prop = of_get_property(np, "fmr,display-color-space", &size);
	if (!prop)
	{
		pr_err("Error fmrfb getting display color space\n");
		return -EINVAL;
	}
	init_data->display_interface_type |= be32_to_cpup(prop);

	prop = of_get_property(np, "fmr,vtc-baseaddr", &size);
	if (!prop)
	{
		pr_warn("Error fmrfb getting vtc baseaddr\n");
	}
	else
	{
		if (be32_to_cpup(prop))
			init_data->vtc_baseaddr = be32_to_cpup(prop);
	}

	prop = of_get_property(np, "fmr,vtc-size", &size);
	if (!prop)
	{
		pr_warn("Error fmrfb getting vtc size\n");
	}
	else
	{
		if (be32_to_cpup(prop))
			init_data->vtc_size = be32_to_cpup(prop);
	}

	prop = of_get_property(np, "fmr,clk-en", &size);
	if (!prop)
	{
		init_data->clk_en = 0;
	}
	else
	{
		init_data->clk_en = be32_to_cpup(prop);
	}

	prop = of_get_property(np, "fmr,clk-baseaddr", &size);
	if (!prop)
	{
		pr_warn("Error fmrfb getting clk baseaddr\n");
	}
	else
	{
		if (be32_to_cpup(prop))
			init_data->clk_baseaddr = be32_to_cpup(prop);
	}

	prop = of_get_property(np, "fmr,clk-size", &size);
	if (!prop)
	{
		pr_warn("Error fmrfb getting clk size\n");
	}
	else
	{
		if (be32_to_cpup(prop))
			init_data->clk_size = be32_to_cpup(prop);
	}

	return 0;
}

static void vdmafb_set_ctrl_reg(struct vdmafb_init_data *init_data,
								unsigned long pix_data_invert, unsigned long pix_clk_act_high)
{
	u32 sync = init_data->vmode_data.fb_vmode.sync;
	u32 ctrl = CTRL_REG_INIT;

	/* FB_SYNC_HOR_HIGH_ACT */
	if (!(sync & (1 << 0)))
		ctrl &= (~(1 << 1));
	/* FB_SYNC_VERT_HIGH_ACT */
	if (!(sync & (1 << 1)))
		ctrl &= (~(1 << 3));
	if (pix_data_invert)
		ctrl |= LOGICVC_PIX_DATA_INVERT;
	if (pix_clk_act_high)
		ctrl |= LOGICVC_PIX_ACT_HIGH;

	init_data->vmode_data.ctrl_reg = ctrl;
}

static int vdmafb_parse_vmode_info(struct device_node *np,
								   struct vdmafb_init_data *init_data)
{
	struct device_node *dn, *vmode_np;
	u32 const *prop;
	char *c;
	unsigned long pix_data_invert, pix_clk_act_high;
	int size, tmp;

	vmode_np = NULL;
	init_data->vmode_data.fb_vmode.refresh = 60;
	init_data->active_layer = 0;
	init_data->vmode_params_set = false;

	prop = of_get_property(np, "active-layer", &size);
	if (prop)
	{
		tmp = be32_to_cpup(prop);
		init_data->active_layer = (unsigned char)tmp;
	}
	else
	{
		pr_info("fmrfb setting default layer to %d\n",
				init_data->active_layer);
	}

	prop = of_get_property(np, "videomode", &size);
	if (prop)
	{
		if (strlen((char *)prop) <= VMODE_NAME_SZ)
		{
			dn = NULL;
			dn = of_find_node_by_name(NULL, "fmr-video-params");
			if (dn)
			{
				strcpy(init_data->vmode_data.fb_vmode_name,
					   (char *)prop);
				vmode_np = of_find_node_by_name(dn,
												init_data->vmode_data.fb_vmode_name);
				c = strchr((char *)prop, '_');
				if (c)
					*c = 0;
				strcpy(init_data->vmode_data.fb_vmode_name, (char *)prop);
			}
			else
			{
				strcpy(init_data->vmode_data.fb_vmode_name, (char *)prop);
			}
			of_node_put(dn);
		}
		else
		{
			pr_err("Error videomode name to long\n");
		}
		if (vmode_np)
		{
			prop = of_get_property(vmode_np, "refresh", &size);
			if (!prop)
				pr_err("Error getting refresh rate\n");
			else
				init_data->vmode_data.fb_vmode.refresh =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "xres", &size);
			if (!prop)
				pr_err("Error getting xres\n");
			else
				init_data->vmode_data.fb_vmode.xres =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "yres", &size);
			if (!prop)
				pr_err("Error getting yres\n");
			else
				init_data->vmode_data.fb_vmode.yres =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "pixclock-khz", &size);
			if (!prop)
				pr_err("Error getting pixclock-khz\n");
			else
				init_data->vmode_data.fb_vmode.pixclock =
					KHZ2PICOS(be32_to_cpup(prop));

			prop = of_get_property(vmode_np, "left-margin", &size);
			if (!prop)
				pr_err("Error getting left-margin\n");
			else
				init_data->vmode_data.fb_vmode.left_margin =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "right-margin", &size);
			if (!prop)
				pr_err("Error getting right-margin\n");
			else
				init_data->vmode_data.fb_vmode.right_margin =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "upper-margin", &size);
			if (!prop)
				pr_err("Error getting upper-margin\n");
			else
				init_data->vmode_data.fb_vmode.upper_margin =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "lower-margin", &size);
			if (!prop)
				pr_err("Error getting lower-margin\n");
			else
				init_data->vmode_data.fb_vmode.lower_margin =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "hsync-len", &size);
			if (!prop)
				pr_err("Error getting hsync-len\n");
			else
				init_data->vmode_data.fb_vmode.hsync_len =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "vsync-len", &size);
			if (!prop)
				pr_err("Error getting vsync-len\n");
			else
				init_data->vmode_data.fb_vmode.vsync_len =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "sync", &size);
			if (!prop)
				pr_err("Error getting sync\n");
			else
				init_data->vmode_data.fb_vmode.sync =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "vmode", &size);
			if (!prop)
				pr_err("Error getting vmode\n");
			else
				init_data->vmode_data.fb_vmode.vmode =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "dynclk-clk0L", &size);
			if (!prop)
				pr_err("Error getting clk0L\n");
			else
				init_data->clkreg.clk0L =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "dynclk-clkFBL", &size);
			if (!prop)
				pr_err("Error getting clkFBL\n");
			else
				init_data->clkreg.clkFBL =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "dynclk-clkFBH_clk0H", &size);
			if (!prop)
				pr_err("Error getting clkFBH_clk0H\n");
			else
				init_data->clkreg.clkFBH_clk0H =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "dynclk-divclk", &size);
			if (!prop)
				pr_err("Error getting divclk\n");
			else
				init_data->clkreg.divclk =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "dynclk-lockL", &size);
			if (!prop)
				pr_err("Error getting lockL\n");
			else
				init_data->clkreg.lockL =
					be32_to_cpup(prop);

			prop = of_get_property(vmode_np, "dynclk-fltr_lockH", &size);
			if (!prop)
				pr_err("Error getting fltr_lockH\n");
			else
				init_data->clkreg.fltr_lockH =
					be32_to_cpup(prop);

			init_data->vmode_params_set = true;
		}
	}
	else
	{
		pr_info("fmrfb using default driver video mode\n");
	}

	vdmafb_set_ctrl_reg(init_data, pix_data_invert, pix_clk_act_high);

	return 0;
}

static void vdmafb_init_vtc_regs(struct vdmafb_layer_data *ld, struct vdmafb_common_data *cd)
{
	int h_pixels, v_lines;

	// printk("0x00=%0x\n\r", readl(ld->vtc_base_virt + 0x00));
	// printk("0x60=%0x\n\r", readl(ld->vtc_base_virt + 0x60));
	// printk("0x68=%0x\n\r", readl(ld->vtc_base_virt + 0x68));
	// printk("0x6c=%0x\n\r", readl(ld->vtc_base_virt + 0x6c));
	// printk("0x70=%0x\n\r", readl(ld->vtc_base_virt + 0x70));
	// printk("0x74=%0x\n\r", readl(ld->vtc_base_virt + 0x74));
	// printk("0x78=%0x\n\r", readl(ld->vtc_base_virt + 0x78));
	// printk("0x7c=%0x\n\r", readl(ld->vtc_base_virt + 0x7c));
	// printk("0x80=%0x\n\r", readl(ld->vtc_base_virt + 0x80));
	// printk("0x84=%0x\n\r", readl(ld->vtc_base_virt + 0x84));
	// printk("0x88=%0x\n\r", readl(ld->vtc_base_virt + 0x88));
	// printk("0x8c=%0x\n\r", readl(ld->vtc_base_virt + 0x8c));
	// printk("0x90=%0x\n\r", readl(ld->vtc_base_virt + 0x90));

	writel(0x3F7EF06, ld->vtc_base_virt);

	h_pixels = cd->vmode_data_current.fb_vmode.xres + cd->vmode_data_current.fb_vmode.hsync_len + cd->vmode_data_current.fb_vmode.left_margin + cd->vmode_data_current.fb_vmode.right_margin;

	v_lines = cd->vmode_data_current.fb_vmode.yres + cd->vmode_data_current.fb_vmode.vsync_len + cd->vmode_data_current.fb_vmode.upper_margin + cd->vmode_data_current.fb_vmode.lower_margin;

	writel((cd->vmode_data_current.fb_vmode.yres << 16) | cd->vmode_data_current.fb_vmode.xres, ld->vtc_base_virt + 0x60);
	writel(0x0000002, ld->vtc_base_virt + 0x68);
	writel(0x000007F, ld->vtc_base_virt + 0x6c);

	writel(h_pixels, ld->vtc_base_virt + 0x70);
	writel((v_lines << 16) | v_lines, ld->vtc_base_virt + 0x74);
	writel((cd->vmode_data_current.fb_vmode.xres + cd->vmode_data_current.fb_vmode.left_margin) | ((h_pixels - cd->vmode_data_current.fb_vmode.right_margin) << 16), ld->vtc_base_virt + 0x78);
	writel((cd->vmode_data_current.fb_vmode.xres << 16) | cd->vmode_data_current.fb_vmode.xres, ld->vtc_base_virt + 0x7c);

	writel((cd->vmode_data_current.fb_vmode.yres + cd->vmode_data_current.fb_vmode.lower_margin - 1) | ((cd->vmode_data_current.fb_vmode.yres + cd->vmode_data_current.fb_vmode.lower_margin + cd->vmode_data_current.fb_vmode.vsync_len - 1) << 16), ld->vtc_base_virt + 0x80);
	writel(((cd->vmode_data_current.fb_vmode.xres + cd->vmode_data_current.fb_vmode.left_margin) << 16) |
			   (cd->vmode_data_current.fb_vmode.xres + cd->vmode_data_current.fb_vmode.left_margin), ld->vtc_base_virt + 0x84);
	writel((cd->vmode_data_current.fb_vmode.xres << 16) | cd->vmode_data_current.fb_vmode.xres, ld->vtc_base_virt + 0x88);
	writel((cd->vmode_data_current.fb_vmode.yres + cd->vmode_data_current.fb_vmode.lower_margin - 1) | ((cd->vmode_data_current.fb_vmode.yres + cd->vmode_data_current.fb_vmode.lower_margin + cd->vmode_data_current.fb_vmode.vsync_len - 1) << 16), ld->vtc_base_virt + 0x8c);

	writel(((cd->vmode_data_current.fb_vmode.xres + cd->vmode_data_current.fb_vmode.left_margin) << 16) |
			   (cd->vmode_data_current.fb_vmode.xres + cd->vmode_data_current.fb_vmode.left_margin), ld->vtc_base_virt + 0x90);

	// writel(0x3F7EF06, ld->vtc_base_virt + 0x00);
	// writel(0x4380780, ld->vtc_base_virt + 0x60);
	// writel(0x0000002, ld->vtc_base_virt + 0x68);
	// writel(0x000007F, ld->vtc_base_virt + 0x6c);
	// writel(0x0000898, ld->vtc_base_virt + 0x70);
	// writel(0x4650465, ld->vtc_base_virt + 0x74);
	// writel(0x80407D8, ld->vtc_base_virt + 0x78);
	// writel(0x7800780, ld->vtc_base_virt + 0x7c);
	// writel(0x440043B, ld->vtc_base_virt + 0x80);
	// writel(0x7D807D8, ld->vtc_base_virt + 0x84);
	// writel(0x7800780, ld->vtc_base_virt + 0x88);
	// writel(0x440043B, ld->vtc_base_virt + 0x8c);
	// writel(0x7D807D8, ld->vtc_base_virt + 0x90);

	// printk("ffffffffffffffffffffffffffffff\n\r");

	// printk("0x00=%0x\n\r", readl(ld->vtc_base_virt + 0x00));
	// printk("0x60=%0x\n\r", readl(ld->vtc_base_virt + 0x60));
	// printk("0x68=%0x\n\r", readl(ld->vtc_base_virt + 0x68));
	// printk("0x6c=%0x\n\r", readl(ld->vtc_base_virt + 0x6c));
	// printk("0x70=%0x\n\r", readl(ld->vtc_base_virt + 0x70));
	// printk("0x74=%0x\n\r", readl(ld->vtc_base_virt + 0x74));
	// printk("0x78=%0x\n\r", readl(ld->vtc_base_virt + 0x78));
	// printk("0x7c=%0x\n\r", readl(ld->vtc_base_virt + 0x7c));
	// printk("0x80=%0x\n\r", readl(ld->vtc_base_virt + 0x80));
	// printk("0x84=%0x\n\r", readl(ld->vtc_base_virt + 0x84));
	// printk("0x88=%0x\n\r", readl(ld->vtc_base_virt + 0x88));
	// printk("0x8c=%0x\n\r", readl(ld->vtc_base_virt + 0x8c));
	// printk("0x90=%0x\n\r", readl(ld->vtc_base_virt + 0x90));

	// printk("\n"
	// 	   "vdmafb parameters:\n"
	// 	   "    Horizontal Front Porch: %d pixclks\n"
	// 	   "    Horizontal Sync:        %d pixclks\n"
	// 	   "    Horizontal Back Porch:  %d pixclks\n"
	// 	   "    Vertical Front Porch:   %d pixclks\n"
	// 	   "    Vertical Sync:          %d pixclks\n"
	// 	   "    Vertical Back Porch:    %d pixclks\n"
	// 	   "    Pixel Clock:            %d ps\n"
	// 	   "    Horizontal Res:         %d\n"
	// 	   "    Vertical Res:           %d\n"
	// 	   "\n",
	// 	   cd->vmode_data_current.fb_vmode.right_margin,
	// 	   cd->vmode_data_current.fb_vmode.hsync_len,
	// 	   cd->vmode_data_current.fb_vmode.left_margin,
	// 	   cd->vmode_data_current.fb_vmode.lower_margin,
	// 	   cd->vmode_data_current.fb_vmode.vsync_len,
	// 	   cd->vmode_data_current.fb_vmode.upper_margin,
	// 	   cd->vmode_data_current.fb_vmode.pixclock,
	// 	   cd->vmode_data_current.fb_vmode.xres,
	// 	   cd->vmode_data_current.fb_vmode.yres);
}

static int vdmafb_setupfb(struct vdmafb_dev *fbdev)
{
	struct fb_var_screeninfo *var = &fbdev->info.var;
	struct dma_async_tx_descriptor *desc;
	struct dma_interleaved_template *dma_template = fbdev->dma_template;
	struct xilinx_vdma_config vdma_config;
	int hsize = var->xres * 4;

	dmaengine_terminate_all(fbdev->dma);

	/* Setup VDMA address etc */
	memset(&vdma_config, 0, sizeof(vdma_config));
	vdma_config.park = 1;
	xilinx_vdma_channel_set_config(fbdev->dma, &vdma_config);

	/*
	* Interleaved DMA:
	* Each interleaved frame is a row (hsize) implemented in ONE
	* chunk (sgl has len 1).
	* The number of interleaved frames is the number of rows (vsize).
	* The icg in used to pack data to the HW, so that the buffer len
	* is fb->piches[0], but the actual size for the hw is somewhat less
	*/
	dma_template->dir = DMA_MEM_TO_DEV;
	dma_template->src_start = fbdev->fb_phys;
	/* sgl list have just one entry (each interleaved frame have 1 chunk) */
	dma_template->frame_size = 1;
	/* the number of interleaved frame, each has the size specified in sgl */
	dma_template->numf = var->yres;
	dma_template->src_sgl = 1;
	dma_template->src_inc = 1;
	/* vdma IP does not provide any addr to the hdmi IP */
	dma_template->dst_inc = 0;
	dma_template->dst_sgl = 0;
	/* horizontal size */
	dma_template->sgl[0].size = hsize;
	/* the vdma driver seems to look at icg, and not src_icg */
	dma_template->sgl[0].icg = 0; /*  stride - hsize */

	desc = dmaengine_prep_interleaved_dma(fbdev->dma, dma_template, 0);
	if (!desc)
	{
		pr_err("Failed to prepare DMA descriptor\n");
		return -ENOMEM;
	}
	else
	{
		dmaengine_submit(desc);
		dma_async_issue_pending(fbdev->dma);
	}

	return 0;
}

// u32 ClkDivider(u32 divide)
// {
// 	u32 output = 0;
// 	u32 highTime = 0;
// 	u32 lowTime = 0;

// 	if ((divide < 1) || (divide > 128))
// 		return -1;

// 	if (divide == 1)
// 		return 0x1041;

// 	highTime = divide / 2;
// 	if (divide & 0b1) //if divide is odd
// 	{
// 		lowTime = highTime + 1;
// 		output = 1 << CLK_BIT_WEDGE;
// 	}
// 	else
// 	{
// 		lowTime = highTime;
// 	}

// 	output |= 0x03F & lowTime;
// 	output |= 0xFC0 & (highTime << 6);
// 	return output;
// }

// u32 ClkCountCalc(u32 divide)
// {
// 	u32 output = 0;
// 	u32 divCalc = 0;

// 	divCalc = ClkDivider(divide);
// 	if (divCalc == ERR_CLKDIVIDER)
// 		output = ERR_CLKCOUNTCALC;
// 	else
// 		output = (0xFFF & divCalc) | ((divCalc << 10) & 0x00C00000);
// 	return output;
// }

// int ClkFindParams(u32 freq, ClkMode *bestPick)
// {
// 	u32 bestError = MMCM_FREQ_OUTMAX;
// 	u32 curError;
// 	u32 curClkMult;
// 	u32 curFreq;
// 	u32 curDiv, curFb, curClkDiv;
// 	u32 minFb = 0;
// 	u32 maxFb = 0;

// 	/*
// 	 * This is necessary because the MMCM actual is generating 5x the desired pixel clock, and that
// 	 * clock is then run through a BUFR that divides it by 5 to generate the pixel clock. Note this
// 	 * means the pixel clock is on the Regional clock network, not the global clock network. In the
// 	 * future if options like these are parameterized in the axi_dynclk core, then this function will
// 	 * need to change.
// 	 */
// 	freq = (freq / 1000) * 5;

// 	bestPick->freq = 0;

// 	/*
// 	* TODO: replace with a smarter algorithm that doesn't doesn't check every possible combination
// 	*/
// 	for (curDiv = 1; curDiv <= 106; curDiv++)
// 	{
// 		minFb = curDiv * 6; //This accounts for the 100MHz input and the 600MHz minimum VCO
// 		maxFb = curDiv * 12; //This accounts for the 100MHz input and the 1200MHz maximum VCO
// 		if (maxFb > 64)
// 			maxFb = 64;

// 		curClkMult = (100 / curDiv) / freq; //This multiplier is used to find the best clkDiv value for each FB value

// 		curFb = minFb;
// 		while (curFb <= maxFb)
// 		{
// 			curClkDiv = (u32) ((curClkMult * curFb) + 1);

// 			curFreq = ((100 / curDiv) / curClkDiv) * curFb;

// 			if (curFreq >= freq)
// 				curError = curFreq - freq;
// 			else
// 				curError = freq - curFreq;

// 			if (curError < bestError)
// 			{
// 				bestError = curError;
// 				bestPick->clkdiv = curClkDiv;
// 				bestPick->fbmult = curFb;
// 				bestPick->maindiv = curDiv;
// 				bestPick->freq = curFreq;
// 			}

// 			curFb++;
// 		}
// 	}

// 	/*
// 	 * We want the ClkMode struct and errors to be based on the desired frequency. Need to check this doesn't introduce
// 	 * rounding errors.
// 	 */
// 	// bestPick->freq = bestPick->freq / 5;
// 	// bestError = bestError / 5;
// 	return bestError;
// }

// u32 ClkFindReg (ClkConfig *regValues, ClkMode *clkParams)
// {
// 	if ((clkParams->fbmult < 2) || clkParams->fbmult > 64 )
// 		return 0;

// 	regValues->clk0L = ClkCountCalc(clkParams->clkdiv);
// 	if (regValues->clk0L == ERR_CLKCOUNTCALC)
// 		return 0;

// 	regValues->clkFBL = ClkCountCalc(clkParams->fbmult);
// 	if (regValues->clkFBL == ERR_CLKCOUNTCALC)
// 		return 0;

// 	regValues->clkFBH_clk0H = 0;

// 	regValues->divclk = ClkDivider(clkParams->maindiv);
// 	if (regValues->divclk == ERR_CLKDIVIDER)
// 		return 0;

// 	regValues->lockL = (u32) (lock_lookup[clkParams->fbmult - 1] & 0xFFFFFFFF);

// 	regValues->fltr_lockH = (u32) ((lock_lookup[clkParams->fbmult - 1] >> 32) & 0x000000FF);
// 	regValues->fltr_lockH |= ((filter_lookup_low[clkParams->fbmult - 1] << 16) & 0x03FF0000);

// 	return 1;
// }

void ClkWriteReg (ClkConfig *regValues, u32 dynClkAddr)
{
	// printk("regValues->clk0L %04x, regValues->clkFBL %04x, regValues->clkFBH_clk0H %04x, regValues->divclk %04x, regValues->lockL %04x, regValues->fltr_lockH %04x\n", regValues->clk0L, regValues->clkFBL, regValues->clkFBH_clk0H, regValues->divclk, regValues->lockL, regValues->fltr_lockH);

	writel(regValues->clk0L, 		dynClkAddr + OFST_DYNCLK_CLK_L);
	writel(regValues->clkFBL, 		dynClkAddr + OFST_DYNCLK_FB_L);
	writel(regValues->clkFBH_clk0H, dynClkAddr + OFST_DYNCLK_FB_H_CLK_H);
	writel(regValues->divclk, 		dynClkAddr + OFST_DYNCLK_DIV);
	writel(regValues->lockL, 		dynClkAddr + OFST_DYNCLK_LOCK_L);
	writel(regValues->fltr_lockH, 	dynClkAddr + OFST_DYNCLK_FLTR_LOCK_H);
}

void ClkStart(u32 dynClkAddr)
{
	writel((1 << BIT_DYNCLK_START),   dynClkAddr + OFST_DYNCLK_CTRL);
	writel((1 << BIT_DYNCLK_RUNNING), dynClkAddr + OFST_DYNCLK_STATUS);
	while(!(readl(dynClkAddr + OFST_DYNCLK_STATUS) & (1 << BIT_DYNCLK_RUNNING)));

	return;
}

void ClkStop(u32 dynClkAddr)
{
	writel(0,   dynClkAddr + OFST_DYNCLK_CTRL);
	while((readl(dynClkAddr + OFST_DYNCLK_STATUS) & (1 << BIT_DYNCLK_RUNNING)));

	return;
}

static void vdmafb_init_fix(struct vdmafb_dev *fbdev)
{
	struct fb_var_screeninfo *var = &fbdev->info.var;
	struct fb_fix_screeninfo *fix = &fbdev->info.fix;

	strcpy(fix->id, "vdma-fb");
	fix->line_length = var->xres * (var->bits_per_pixel / 8);
	fix->smem_len = fix->line_length * var->yres;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual = FB_VISUAL_TRUECOLOR;
}

static void vdmafb_init_var(struct vdmafb_dev *fbdev, struct platform_device *pdev, const struct vdmafb_layer_data *ld, const struct vdmafb_common_data *cd)
{
	struct device_node *np = pdev->dev.of_node;
	struct fb_var_screeninfo *var = &fbdev->info.var;
	ClkConfig clkReg;
	ClkMode clkMode;
	int ret;

	var->xres = cd->vmode_data_current.fb_vmode.xres;
	var->yres = cd->vmode_data_current.fb_vmode.yres;

	var->accel_flags = FB_ACCEL_NONE;
	var->activate = FB_ACTIVATE_NOW;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->bits_per_pixel = 32;
	/* Clock settings */
	var->pixclock = cd->vmode_data_current.fb_vmode.pixclock;//KHZ2PICOS(cd->vmode_data_current.fb_vmode.pixclock * 1000);
	var->vmode = cd->vmode_data_current.fb_vmode.vmode;
	/* 32 BPP */
	var->transp.offset = 24;
	var->transp.length = 8;

	if(cd->fmrfb_display_interface_type == 1) //RGBA
	{
		var->red.offset = 0;
		var->blue.offset = 16;
	}
	else //default mode: BGRA
	{
		var->red.offset = 16;
		var->blue.offset = 0;
	}

	var->red.length = 8;
	var->green.offset = 8;
	var->green.length = 8;
	var->blue.length = 8;

	/*
	 * Init VTC Module
	*/
	vdmafb_init_vtc_regs(ld, cd);
}

static int vdmafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
							u_int transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;
	u32 cr = red >> (16 - info->var.red.length);
	u32 cg = green >> (16 - info->var.green.length);
	u32 cb = blue >> (16 - info->var.blue.length);
	u32 value;

	if (regno >= 16)
		return -EINVAL;

	value = (cr << info->var.red.offset) |
			(cg << info->var.green.offset) |
			(cb << info->var.blue.offset);
	if (info->var.transp.length > 0)
	{
		u32 mask = (1 << info->var.transp.length) - 1;
		mask <<= info->var.transp.offset;
		value |= mask;
	}
	pal[regno] = value;

	return 0;
}

static struct fb_ops vdmafb_ops = {
	.owner = THIS_MODULE,
	.fb_setcolreg = vdmafb_setcolreg,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
};

static int vdmafb_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct vdmafb_dev *fbdev;
	struct vdmafb_common_data *cd;
	struct vdmafb_layer_data *ld;
	struct resource *reg_res, *irq_res;
	int fbsize;
	unsigned long reg_base_phys;
	void *vtc_base_virt;
	unsigned long vtc_baseaddr;
	int vtc_size;
	void *clk_base_virt;
	unsigned long clk_baseaddr;
	int clk_size;
	int reg_range, layers, active_layer;
	int i, rc;
	struct vdmafb_init_data init_data;
	ClkConfig clkReg;
	ClkMode clkMode;

	//gpio_request(962, "gpio_56");
	//gpio_direction_output(962, 1);

	gpio_request(963, "gpio_57");
	gpio_direction_output(963, 1);

	init_data.pdev = pdev;

	fbdev = devm_kzalloc(&pdev->dev, sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, fbdev);

	fbdev->info.fbops = &vdmafb_ops;
	fbdev->info.device = &pdev->dev;
	fbdev->info.par = fbdev;

	fbdev->dma_template = devm_kzalloc(&pdev->dev,
									   sizeof(struct dma_interleaved_template) +
										   sizeof(struct data_chunk),
									   GFP_KERNEL);
	if (!fbdev->dma_template)
		return -ENOMEM;

	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	// irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if ((!reg_res))
	{
		pr_err("Error fmrfb resources\n");
		return -ENODEV;
	}

	ret = vdmafb_parse_hw_info(pdev->dev.of_node, &init_data);
	if (ret)
		return ret;

	vdmafb_parse_vmode_info(pdev->dev.of_node, &init_data);

	layers = init_data.layers;
	if (layers == 0)
	{
		pr_err("Error fmrfb zero layers\n");
		return -ENODEV;
	}

	active_layer = init_data.active_layer;
	if (active_layer >= layers)
	{
		pr_err("Error fmrfb default layer: set to 0\n");
		active_layer = 0;
	}

	vtc_baseaddr = init_data.vtc_baseaddr;
	vtc_size = init_data.vtc_size;
	if (vtc_baseaddr == 0 || vtc_size == 0)
	{
		pr_err("Error fmrfb vtc baseaddr\n");
		return -ENODEV;
	}
	
	clk_baseaddr = init_data.clk_baseaddr;
	clk_size = init_data.clk_size;
	if (clk_baseaddr == 0 || clk_size == 0)
	{
		pr_err("Error fmrfb clk baseaddr\n");
		return -ENODEV;
	}

	cd = kzalloc(sizeof(struct vdmafb_common_data), GFP_KERNEL);
	if (!cd)
	{
		pr_err("Error fmrfb allocating internal data\n");
		rc = -ENOMEM;
		goto err_mem;
	}

	ld = kzalloc(sizeof(struct vdmafb_layer_data), GFP_KERNEL);
	if (!cd)
	{
		pr_err("Error fmrfb allocating layer data\n");
		rc = -ENOMEM;
		goto err_mem;
	}

	cd->vmode_data = init_data.vmode_data;
	cd->vmode_data_current = init_data.vmode_data;
	cd->fmrfb_display_interface_type = init_data.display_interface_type;

	reg_base_phys = reg_res->start;
	reg_range = resource_size(reg_res);

	// reg_base_virt = ioremap_nocache(reg_base_phys, reg_range);

	vtc_base_virt = ioremap_nocache(vtc_baseaddr, vtc_size);

	clk_base_virt = ioremap_nocache(clk_baseaddr, clk_size);

	ld->vtc_base_virt = vtc_base_virt;
	ld->clk_base_virt = clk_base_virt;

	/*
	 * Calculate the PLL divider parameters based on the required pixel clock frequency
	 */
	// ClkFindParams(cd->vmode_data_current.fb_vmode.pixclock, &clkMode);

	// /*
	//  * Write to the PLL dynamic configuration registers to configure it with the calculated
	//  * parameters.
	//  */
	// if (!ClkFindReg(&clkReg, &clkMode))
	// {
	// 	printk("Error calculating CLK register values\n");

	// 	return -1;
	// }

	//init dynclk
	ClkWriteReg(&init_data.clkreg, ld->clk_base_virt);
	// ClkWriteReg(&clkReg, ld->clk_base_virt);

	/*
	 * Enable the dynamically generated clock
    */
    if(init_data.clk_en)
   	{
		ClkStop(ld->clk_base_virt);
		ClkStart(ld->clk_base_virt);
	}

	//init vtc
	vdmafb_init_var(fbdev, pdev, ld, cd);
	vdmafb_init_fix(fbdev);

	/* Allocate framebuffer memory */
	fbsize = fbdev->info.fix.smem_len;
	fbdev->fb_virt = dma_alloc_coherent(&pdev->dev, PAGE_ALIGN(fbsize),
										&fbdev->fb_phys, GFP_KERNEL);
	if (!fbdev->fb_virt)
	{
		dev_err(&pdev->dev,
				"Frame buffer memory allocation failed\n");
		return -ENOMEM;
	}
	fbdev->info.fix.smem_start = fbdev->fb_phys;
	fbdev->info.screen_base = fbdev->fb_virt;
	fbdev->info.pseudo_palette = fbdev->pseudo_palette;

	pr_debug("%s virt=%p phys=%x size=%d\n", __func__,
			 fbdev->fb_virt, fbdev->fb_phys, fbsize);

	/* Clear framebuffer */
	memset_io(fbdev->fb_virt, 0, fbsize);

	fbdev->dma = dma_request_slave_channel(&pdev->dev, "axivdma");
	if (IS_ERR_OR_NULL(fbdev->dma))
	{
		ret = PTR_ERR(fbdev->dma);
		dev_err(&pdev->dev, "Failed to allocate DMA channel (%d).\n", ret);
		goto err_dma_free;
	}

	/* Setup and enable the framebuffer */
	vdmafb_setupfb(fbdev);

	ret = fb_alloc_cmap(&fbdev->info.cmap, 256, 0);
	if (ret)
	{
		dev_err(&pdev->dev, "fb_alloc_cmap failed\n");
	}

	/* Register framebuffer */
	ret = register_framebuffer(&fbdev->info);
	if (ret)
	{
		dev_err(&pdev->dev, "Framebuffer registration failed\n");
		goto err_channel_free;
	}

	dev_dbg(&pdev->dev, "vdmafb probe ok\n");

	return 0;

err_channel_free:
	dma_release_channel(fbdev->dma);
err_dma_free:
	dma_free_coherent(&pdev->dev, PAGE_ALIGN(fbsize), fbdev->fb_virt,
					  fbdev->fb_phys);

err_mem:
	if (cd)
	{
		kfree(cd->reg_list);
		kfree(cd);
	}

	return ret;
}

static int vdmafb_remove(struct platform_device *pdev)
{
	struct vdmafb_dev *fbdev = platform_get_drvdata(pdev);

	unregister_framebuffer(&fbdev->info);

	dma_release_channel(fbdev->dma);
	dma_free_coherent(&pdev->dev, PAGE_ALIGN(fbdev->info.fix.smem_len),
					  fbdev->fb_virt, fbdev->fb_phys);
	fb_dealloc_cmap(&fbdev->info.cmap);
	return 0;
}

static struct of_device_id vdmafb_match[] = {
	{
		.compatible = "myir,vdma-fb",
	},
	{},
};
MODULE_DEVICE_TABLE(of, vdmafb_match);

static struct platform_driver vdmafb_driver = {
	.probe = vdmafb_probe,
	.remove = vdmafb_remove,
	.driver = {
		.name = "vdmafb_fb",
		.of_match_table = vdmafb_match,
	}};
module_platform_driver(vdmafb_driver);

MODULE_AUTHOR("Calvin <calvin.liu@myirtech.com>");
MODULE_DESCRIPTION("Driver for VDMA controlled framebuffer");
MODULE_LICENSE("GPL v2");
