#ifndef __VDMAFB_H__
#define __VDMAFB_H__

#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include "logicvc.h"

/* FmrFB driver flags */
#define FMRFB_FLAG_RESERVED_0x01 LOGICVC_READABLE_REGS
#define FMRFB_FLAG_DMA_BUFFER 0x02
#define FMRFB_FLAG_MEMORY_LE 0x04
#define FMRFB_FLAG_PIXCLK_VALID 0x08
#define FMRFB_FLAG_VMODE_INIT 0x10
#define FMRFB_FLAG_EDID_VMODE 0x20
#define FMRFB_FLAG_EDID_PRINT 0x40
#define FMRFB_FLAG_DEFAULT_VMODE_SET 0x80
#define FMRFB_FLAG_VMODE_SET 0x100

/*
	Following flags must be updated in fmrfb miscellaneous
	header files for every functionality specifically
*/
#define FMRFB_FLAG_MISC_ADV7511 0x1000
#define FMRFB_FLAG_ADV7511_SKIP 0x2000
#define FMRFB_FLAG_EDID_RDY 0x4000
#define FMRFB_EDID_SIZE 256
#define FMRFB_EDID_WAIT_TOUT 60

#define VMODE_NAME_SZ (20 + 1)
#define VMODE_OPTS_SZ (2 + 1)

/* ------------------------------------------------------------ */
/*					Miscellaneous Declarations					*/
/* ------------------------------------------------------------ */

#define CLK_BIT_WEDGE 13
#define CLK_BIT_NOCOUNT 12

/*
 * WEDGE and NOCOUNT can't both be high, so this is used to signal an error state
 */
#define ERR_CLKDIVIDER (1 << CLK_BIT_WEDGE | 1 << CLK_BIT_NOCOUNT)

#define ERR_CLKCOUNTCALC 0xFFFFFFFF //This value is used to signal an error

#define OFST_DYNCLK_CTRL 0x0
#define OFST_DYNCLK_STATUS 0x4
#define OFST_DYNCLK_CLK_L 0x8
#define OFST_DYNCLK_FB_L 0x0C
#define OFST_DYNCLK_FB_H_CLK_H 0x10
#define OFST_DYNCLK_DIV 0x14
#define OFST_DYNCLK_LOCK_L 0x18
#define OFST_DYNCLK_FLTR_LOCK_H 0x1C

#define BIT_DYNCLK_START 0
#define BIT_DYNCLK_RUNNING 0

#define MMCM_FREQ_VCOMIN 600000
#define MMCM_FREQ_VCOMAX 1200000
#define MMCM_FREQ_PFDMIN 10000
#define MMCM_FREQ_PFDMAX 450000
#define MMCM_FREQ_OUTMIN 4000
#define MMCM_FREQ_OUTMAX 800000
#define MMCM_DIV_MAX 106
#define MMCM_FB_MIN 2
#define MMCM_FB_MAX 64
#define MMCM_CLKDIV_MAX 128
#define MMCM_CLKDIV_MIN 1

#define FB_EVENT_FBI_UPDATE 0x01
#define FB_FLAG_EDID_VMODE  0x20
#define FB_FLAG_EDID_RDY    0x4000

static const u64 lock_lookup[64] = {
	0b0011000110111110100011111010010000000001,
	0b0011000110111110100011111010010000000001,
	0b0100001000111110100011111010010000000001,
	0b0101101011111110100011111010010000000001,
	0b0111001110111110100011111010010000000001,
	0b1000110001111110100011111010010000000001,
	0b1001110011111110100011111010010000000001,
	0b1011010110111110100011111010010000000001,
	0b1100111001111110100011111010010000000001,
	0b1110011100111110100011111010010000000001,
	0b1111111111111000010011111010010000000001,
	0b1111111111110011100111111010010000000001,
	0b1111111111101110111011111010010000000001,
	0b1111111111101011110011111010010000000001,
	0b1111111111101000101011111010010000000001,
	0b1111111111100111000111111010010000000001,
	0b1111111111100011111111111010010000000001,
	0b1111111111100010011011111010010000000001,
	0b1111111111100000110111111010010000000001,
	0b1111111111011111010011111010010000000001,
	0b1111111111011101101111111010010000000001,
	0b1111111111011100001011111010010000000001,
	0b1111111111011010100111111010010000000001,
	0b1111111111011001000011111010010000000001,
	0b1111111111011001000011111010010000000001,
	0b1111111111010111011111111010010000000001,
	0b1111111111010101111011111010010000000001,
	0b1111111111010101111011111010010000000001,
	0b1111111111010100010111111010010000000001,
	0b1111111111010100010111111010010000000001,
	0b1111111111010010110011111010010000000001,
	0b1111111111010010110011111010010000000001,
	0b1111111111010010110011111010010000000001,
	0b1111111111010001001111111010010000000001,
	0b1111111111010001001111111010010000000001,
	0b1111111111010001001111111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001,
	0b1111111111001111101011111010010000000001
};

static const u32 filter_lookup_low[64] = {
	0b0001011111,
	0b0001010111,
	0b0001111011,
	0b0001011011,
	0b0001101011,
	0b0001110011,
	0b0001110011,
	0b0001110011,
	0b0001110011,
	0b0001001011,
	0b0001001011,
	0b0001001011,
	0b0010110011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001010011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0001100011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010010011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011,
	0b0010100011
};

struct vdmafb_vmode_data
{
	u32 ctrl_reg;
	struct fb_videomode fb_vmode;
	char fb_vmode_name[VMODE_NAME_SZ];
	char fb_vmode_opts_cvt[VMODE_OPTS_SZ];
	char fb_vmode_opts_ext[VMODE_OPTS_SZ];
};

struct vdmafb_layer_fix_data
{
	unsigned int offset;
	unsigned short buffer_offset;
	unsigned short width;
	unsigned short height;
	unsigned char bpp;
	unsigned char bpp_virt;
	unsigned char layer_type;
	unsigned char alpha_mode;
	/* higher 4 bits: number of layer buffers, lower 4 bits: layer ID */
	unsigned char layer_fix_info;
};

// struct vdmafb_register_access {
// 	u32 (*fmrfb_get_reg_val)
// 		(void *reg_base_virt, unsigned long offset,
// 		 struct fmrfb_layer_data *layer_data);
// 	void (*fmrfb_set_reg_val)
// 		(u32 value, void *reg_base_virt, unsigned long offset,
// 		 struct fmrfb_layer_data *layer_data);
// };

struct vdmafb_sync {
	wait_queue_head_t wait;
	unsigned int cnt;
};

struct vdmafb_registers {
	u32 ctrl_reg;
	u32 dtype_reg;
	u32 bg_reg;
	u32 unused_reg[3];
	u32 int_mask_reg;
};

struct vdmafb_common_data
{
	struct mutex irq_mutex;
	// struct vdmafb_register_access reg_access;
	struct vdmafb_registers *reg_list;
	struct vdmafb_sync vsync;
	struct vdmafb_vmode_data vmode_data;
	struct vdmafb_vmode_data vmode_data_current;
	struct blocking_notifier_head fmrfb_notifier_list;
	struct notifier_block fmrfb_nb;
	/* Delay after applying display power and
		before applying display signals */
	unsigned int power_on_delay;
	/* Delay after applying display signal and
		before applying display backlight power supply */
	unsigned int signal_on_delay;
	unsigned long fmrfb_flags;
	unsigned char fmrfb_pixclk_src_id;
	unsigned char fmrfb_layers;
	unsigned char fmrfb_irq;
	unsigned char fmrfb_use_ref;
	unsigned char fmrfb_console_layer;
	unsigned char fmrfb_bg_layer_bpp;
	unsigned char fmrfb_bg_layer_alpha_mode;
	/* higher 4 bits: display interface
	   lower 4 bits: display color space */
	unsigned char fmrfb_display_interface_type;
};

struct vdmafb_layer_data
{
	struct vdmafb_common_data *vdmafb_cd;
	struct mutex layer_mutex;
	dma_addr_t reg_base_phys;
	dma_addr_t fb_phys;
	void *reg_base_virt;
	void *vtc_base_virt;
	void *clk_base_virt;
	void *fb_virt;
	unsigned long fb_size;
	void *layer_reg_base_virt;
	void *layer_clut_base_virt;
	struct vdmafb_layer_fix_data layer_fix;
	struct vdmafb_layer_registers *layer_reg_list;
	unsigned char layer_ctrl_flags;
	unsigned char layer_use_ref;
};

typedef struct {
		u32 clk0L;
		u32 clkFBL;
		u32 clkFBH_clk0H;
		u32 divclk;
		u32 lockL;
		u32 fltr_lockH;
} ClkConfig;

typedef struct {
		u32 freq;
		u32 fbmult;
		u32 clkdiv;
		u32 maindiv;
} ClkMode;

struct vdmafb_init_data
{
	struct platform_device *pdev;
	struct vdmafb_vmode_data vmode_data;
	struct vdmafb_layer_fix_data lfdata[LOGICVC_MAX_LAYERS];
	ClkConfig clkreg;
	unsigned long vmem_base_addr;
	unsigned long vmem_high_addr;
	unsigned long vtc_baseaddr;
	int vtc_size;
	unsigned long clk_baseaddr;
	int clk_size;
	unsigned char clk_en;
	unsigned char pixclk_src_id;
	unsigned char layer_ctrl_flags[LOGICVC_MAX_LAYERS];
	unsigned char layers;
	unsigned char active_layer;
	unsigned char bg_layer_bpp;
	unsigned char bg_layer_alpha_mode;
	unsigned char display_interface_type;
	unsigned short flags;
	bool vmode_params_set;
};

#endif
