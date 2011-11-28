/*
 * hdmi.c
 *
 * HDMI interface DSS driver setting for TI's OMAP4 family of processor.
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 * Authors: Yong Zhi
 *	Mythri pk <mythripk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "HDMI"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <video/omapdss.h>
#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "ti_hdmi_4xxx_ip.h"
#endif

#include "ti_hdmi.h"
#include "dss.h"
#include "dss_features.h"

#define HDMI_WP			0x0
#define HDMI_CORE_SYS		0x400
#define HDMI_CORE_AV		0x900
#define HDMI_PLLCTRL		0x200
#define HDMI_PHY		0x300

/* HDMI EDID Length move this */
#define HDMI_EDID_MAX_LENGTH			256
#define EDID_TIMING_DESCRIPTOR_SIZE		0x12
#define EDID_DESCRIPTOR_BLOCK0_ADDRESS		0x36
#define EDID_DESCRIPTOR_BLOCK1_ADDRESS		0x80
#define EDID_SIZE_BLOCK0_TIMING_DESCRIPTOR	4
#define EDID_SIZE_BLOCK1_TIMING_DESCRIPTOR	4

#define OMAP_HDMI_TIMINGS_NB			34

#define HDMI_DEFAULT_REGN 16
#define HDMI_DEFAULT_REGM2 1

static struct {
	struct mutex lock;
	struct omap_display_platform_data *pdata;
	struct platform_device *pdev;
	struct hdmi_ip_data ip_data;
	int code;
	int mode;

	struct clk *sys_clk;
} hdmi;

/*
 * Logic for the below structure :
 * user enters the CEA or VESA timings by specifying the HDMI/DVI code.
 * There is a correspondence between CEA/VESA timing and code, please
 * refer to section 6.3 in HDMI 1.3 specification for timing code.
 *
 * In the below structure, cea_vesa_timings corresponds to all OMAP4
 * supported CEA and VESA timing values.code_cea corresponds to the CEA
 * code, It is used to get the timing from cea_vesa_timing array.Similarly
 * with code_vesa. Code_index is used for back mapping, that is once EDID
 * is read from the TV, EDID is parsed to find the timing values and then
 * map it to corresponding CEA or VESA index.
 */

static const struct hdmi_timings cea_vesa_timings[OMAP_HDMI_TIMINGS_NB] = {
	{ {640, 480, 25200, 96, 16, 48, 2, 10, 33} , 0 , 0},
	{ {1280, 720, 74250, 40, 440, 220, 5, 5, 20}, 1, 1},
	{ {1280, 720, 74250, 40, 110, 220, 5, 5, 20}, 1, 1},
	{ {720, 480, 27027, 62, 16, 60, 6, 9, 30}, 0, 0},
	{ {2880, 576, 108000, 256, 48, 272, 5, 5, 39}, 0, 0},
	{ {1440, 240, 27027, 124, 38, 114, 3, 4, 15}, 0, 0},
	{ {1440, 288, 27000, 126, 24, 138, 3, 2, 19}, 0, 0},
	{ {1920, 540, 74250, 44, 528, 148, 5, 2, 15}, 1, 1},
	{ {1920, 540, 74250, 44, 88, 148, 5, 2, 15}, 1, 1},
	{ {1920, 1080, 148500, 44, 88, 148, 5, 4, 36}, 1, 1},
	{ {720, 576, 27000, 64, 12, 68, 5, 5, 39}, 0, 0},
	{ {1440, 576, 54000, 128, 24, 136, 5, 5, 39}, 0, 0},
	{ {1920, 1080, 148500, 44, 528, 148, 5, 4, 36}, 1, 1},
	{ {2880, 480, 108108, 248, 64, 240, 6, 9, 30}, 0, 0},
	{ {1920, 1080, 74250, 44, 638, 148, 5, 4, 36}, 1, 1},
	/* VESA From Here */
	{ {640, 480, 25175, 96, 16, 48, 2 , 11, 31}, 0, 0},
	{ {800, 600, 40000, 128, 40, 88, 4 , 1, 23}, 1, 1},
	{ {848, 480, 33750, 112, 16, 112, 8 , 6, 23}, 1, 1},
	{ {1280, 768, 79500, 128, 64, 192, 7 , 3, 20}, 1, 0},
	{ {1280, 800, 83500, 128, 72, 200, 6 , 3, 22}, 1, 0},
	{ {1360, 768, 85500, 112, 64, 256, 6 , 3, 18}, 1, 1},
	{ {1280, 960, 108000, 112, 96, 312, 3 , 1, 36}, 1, 1},
	{ {1280, 1024, 108000, 112, 48, 248, 3 , 1, 38}, 1, 1},
	{ {1024, 768, 65000, 136, 24, 160, 6, 3, 29}, 0, 0},
	{ {1400, 1050, 121750, 144, 88, 232, 4, 3, 32}, 1, 0},
	{ {1440, 900, 106500, 152, 80, 232, 6, 3, 25}, 1, 0},
	{ {1680, 1050, 146250, 176 , 104, 280, 6, 3, 30}, 1, 0},
	{ {1366, 768, 85500, 143, 70, 213, 3, 3, 24}, 1, 1},
	{ {1920, 1080, 148500, 44, 148, 80, 5, 4, 36}, 1, 1},
	{ {1280, 768, 68250, 32, 48, 80, 7, 3, 12}, 0, 1},
	{ {1400, 1050, 101000, 32, 48, 80, 4, 3, 23}, 0, 1},
	{ {1680, 1050, 119000, 32, 48, 80, 6, 3, 21}, 0, 1},
	{ {1280, 800, 79500, 32, 48, 80, 6, 3, 14}, 0, 1},
	{ {1280, 720, 74250, 40, 110, 220, 5, 5, 20}, 1, 1}
};

/*
 * This is a static mapping array which maps the timing values
 * with corresponding CEA / VESA code
 */
static const int code_index[OMAP_HDMI_TIMINGS_NB] = {
	1, 19, 4, 2, 37, 6, 21, 20, 5, 16, 17, 29, 31, 35, 32,
	/* <--15 CEA 17--> vesa*/
	4, 9, 0xE, 0x17, 0x1C, 0x27, 0x20, 0x23, 0x10, 0x2A,
	0X2F, 0x3A, 0X51, 0X52, 0x16, 0x29, 0x39, 0x1B
};

/*
 * This is reverse static mapping which maps the CEA / VESA code
 * to the corresponding timing values
 */
static const int code_cea[39] = {
	-1,  0,  3,  3,  2,  8,  5,  5, -1, -1,
	-1, -1, -1, -1, -1, -1,  9, 10, 10,  1,
	7,   6,  6, -1, -1, -1, -1, -1, -1, 11,
	11, 12, 14, -1, -1, 13, 13,  4,  4
};

static const int code_vesa[85] = {
	-1, -1, -1, -1, 15, -1, -1, -1, -1, 16,
	-1, -1, -1, -1, 17, -1, 23, -1, -1, -1,
	-1, -1, 29, 18, -1, -1, -1, 32, 19, -1,
	-1, -1, 21, -1, -1, 22, -1, -1, -1, 20,
	-1, 30, 24, -1, -1, -1, -1, 25, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, 31, 26, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 27, 28, -1, 33};

static int hdmi_runtime_get(void)
{
	int r;

	DSSDBG("hdmi_runtime_get\n");

	r = pm_runtime_get_sync(&hdmi.pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

static void hdmi_runtime_put(void)
{
	int r;

	DSSDBG("hdmi_runtime_put\n");

	r = pm_runtime_put(&hdmi.pdev->dev);
	WARN_ON(r < 0);
}

int hdmi_init_display(struct omap_dss_device *dssdev)
{
	DSSDBG("init_display\n");

	dss_init_hdmi_ip_ops(&hdmi.ip_data);
	return 0;
}

static int get_timings_index(void)
{
	int code;

	if (hdmi.mode == 0)
		code = code_vesa[hdmi.code];
	else
		code = code_cea[hdmi.code];

	if (code == -1)	{
		/* HDMI code 4 corresponds to 640 * 480 VGA */
		hdmi.code = 4;
		/* DVI mode 1 corresponds to HDMI 0 to DVI */
		hdmi.mode = HDMI_DVI;

		code = code_vesa[hdmi.code];
	}
	return code;
}

static struct hdmi_cm hdmi_get_code(struct omap_video_timings *timing)
{
	int i = 0, code = -1, temp_vsync = 0, temp_hsync = 0;
	int timing_vsync = 0, timing_hsync = 0;
	struct hdmi_video_timings temp;
	struct hdmi_cm cm = {-1};
	DSSDBG("hdmi_get_code\n");

	for (i = 0; i < OMAP_HDMI_TIMINGS_NB; i++) {
		temp = cea_vesa_timings[i].timings;
		if ((temp.pixel_clock == timing->pixel_clock) &&
			(temp.x_res == timing->x_res) &&
			(temp.y_res == timing->y_res)) {

			temp_hsync = temp.hfp + temp.hsw + temp.hbp;
			timing_hsync = timing->hfp + timing->hsw + timing->hbp;
			temp_vsync = temp.vfp + temp.vsw + temp.vbp;
			timing_vsync = timing->vfp + timing->vsw + timing->vbp;

			DSSDBG("temp_hsync = %d , temp_vsync = %d"
				"timing_hsync = %d, timing_vsync = %d\n",
				temp_hsync, temp_hsync,
				timing_hsync, timing_vsync);

			if ((temp_hsync == timing_hsync) &&
					(temp_vsync == timing_vsync)) {
				code = i;
				cm.code = code_index[i];
				if (code < 14)
					cm.mode = HDMI_HDMI;
				else
					cm.mode = HDMI_DVI;
				DSSDBG("Hdmi_code = %d mode = %d\n",
					 cm.code, cm.mode);
				break;
			 }
		}
	}

	return cm;
}

static void update_hdmi_timings(struct hdmi_config *cfg,
		struct omap_video_timings *timings, int code)
{
	cfg->timings.timings.x_res = timings->x_res;
	cfg->timings.timings.y_res = timings->y_res;
	cfg->timings.timings.hbp = timings->hbp;
	cfg->timings.timings.hfp = timings->hfp;
	cfg->timings.timings.hsw = timings->hsw;
	cfg->timings.timings.vbp = timings->vbp;
	cfg->timings.timings.vfp = timings->vfp;
	cfg->timings.timings.vsw = timings->vsw;
	cfg->timings.timings.pixel_clock = timings->pixel_clock;
	cfg->timings.vsync_pol = cea_vesa_timings[code].vsync_pol;
	cfg->timings.hsync_pol = cea_vesa_timings[code].hsync_pol;
}

unsigned long hdmi_get_pixel_clock(void)
{
	/* HDMI Pixel Clock in Mhz */
	return hdmi.ip_data.cfg.timings.timings.pixel_clock * 10000;
}

static void hdmi_compute_pll(struct omap_dss_device *dssdev, int phy,
		struct hdmi_pll_info *pi)
{
	unsigned long clkin, refclk;
	u32 mf;

	clkin = clk_get_rate(hdmi.sys_clk) / 10000;
	/*
	 * Input clock is predivided by N + 1
	 * out put of which is reference clk
	 */
	if (dssdev->clocks.hdmi.regn == 0)
		pi->regn = HDMI_DEFAULT_REGN;
	else
		pi->regn = dssdev->clocks.hdmi.regn;

	refclk = clkin / pi->regn;

	/*
	 * multiplier is pixel_clk/ref_clk
	 * Multiplying by 100 to avoid fractional part removal
	 */
	pi->regm = (phy * 100 / (refclk)) / 100;

	if (dssdev->clocks.hdmi.regm2 == 0)
		pi->regm2 = HDMI_DEFAULT_REGM2;
	else
		pi->regm2 = dssdev->clocks.hdmi.regm2;

	/*
	 * fractional multiplier is remainder of the difference between
	 * multiplier and actual phy(required pixel clock thus should be
	 * multiplied by 2^18(262144) divided by the reference clock
	 */
	mf = (phy - pi->regm * refclk) * 262144;
	pi->regmf = mf / (refclk);

	/*
	 * Dcofreq should be set to 1 if required pixel clock
	 * is greater than 1000MHz
	 */
	pi->dcofreq = phy > 1000 * 100;
	pi->regsd = ((pi->regm * clkin / 10) / (pi->regn * 250) + 5) / 10;

	/* Set the reference clock to sysclk reference */
	pi->refsel = HDMI_REFSEL_SYSCLK;

	DSSDBG("M = %d Mf = %d\n", pi->regm, pi->regmf);
	DSSDBG("range = %d sd = %d\n", pi->dcofreq, pi->regsd);
}

static int hdmi_power_on(struct omap_dss_device *dssdev)
{
	int r, code = 0;
	struct omap_video_timings *p;
	unsigned long phy;

	r = hdmi_runtime_get();
	if (r)
		return r;

	dispc_mgr_enable(OMAP_DSS_CHANNEL_DIGIT, 0);

	p = &dssdev->panel.timings;

	DSSDBG("hdmi_power_on x_res= %d y_res = %d\n",
		dssdev->panel.timings.x_res,
		dssdev->panel.timings.y_res);

	code = get_timings_index();
	update_hdmi_timings(&hdmi.ip_data.cfg, p, code);

	phy = p->pixel_clock;

	hdmi_compute_pll(dssdev, phy, &hdmi.ip_data.pll_data);

	hdmi.ip_data.ops->video_enable(&hdmi.ip_data, 0);

	/* config the PLL and PHY hdmi_set_pll_pwrfirst */
	r = hdmi.ip_data.ops->pll_enable(&hdmi.ip_data);
	if (r) {
		DSSDBG("Failed to lock PLL\n");
		goto err;
	}

	r = hdmi.ip_data.ops->phy_enable(&hdmi.ip_data);
	if (r) {
		DSSDBG("Failed to start PHY\n");
		goto err;
	}

	hdmi.ip_data.cfg.cm.mode = hdmi.mode;
	hdmi.ip_data.cfg.cm.code = hdmi.code;
	hdmi.ip_data.ops->video_configure(&hdmi.ip_data);

	/* Make selection of HDMI in DSS */
	dss_select_hdmi_venc_clk_source(DSS_HDMI_M_PCLK);

	/* Select the dispc clock source as PRCM clock, to ensure that it is not
	 * DSI PLL source as the clock selected by DSI PLL might not be
	 * sufficient for the resolution selected / that can be changed
	 * dynamically by user. This can be moved to single location , say
	 * Boardfile.
	 */
	dss_select_dispc_clk_source(dssdev->clocks.dispc.dispc_fclk_src);

	/* bypass TV gamma table */
	dispc_enable_gamma_table(0);

	/* tv size */
	dispc_set_digit_size(dssdev->panel.timings.x_res,
			dssdev->panel.timings.y_res);

	hdmi.ip_data.ops->video_enable(&hdmi.ip_data, 1);

	dispc_mgr_enable(OMAP_DSS_CHANNEL_DIGIT, 1);

	return 0;
err:
	hdmi_runtime_put();
	return -EIO;
}

static void hdmi_power_off(struct omap_dss_device *dssdev)
{
	dispc_mgr_enable(OMAP_DSS_CHANNEL_DIGIT, 0);

	hdmi.ip_data.ops->video_enable(&hdmi.ip_data, 0);
	hdmi.ip_data.ops->phy_disable(&hdmi.ip_data);
	hdmi.ip_data.ops->pll_disable(&hdmi.ip_data);
	hdmi_runtime_put();
}

int omapdss_hdmi_display_check_timing(struct omap_dss_device *dssdev,
					struct omap_video_timings *timings)
{
	struct hdmi_cm cm;

	cm = hdmi_get_code(timings);
	if (cm.code == -1) {
		return -EINVAL;
	}

	return 0;

}

void omapdss_hdmi_display_set_timing(struct omap_dss_device *dssdev)
{
	struct hdmi_cm cm;

	cm = hdmi_get_code(&dssdev->panel.timings);
	hdmi.code = cm.code;
	hdmi.mode = cm.mode;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
		int r;

		hdmi_power_off(dssdev);

		r = hdmi_power_on(dssdev);
		if (r)
			DSSERR("failed to power on device\n");
	}
}

void hdmi_dump_regs(struct seq_file *s)
{
	mutex_lock(&hdmi.lock);

	if (hdmi_runtime_get())
		return;

	hdmi.ip_data.ops->dump_wrapper(&hdmi.ip_data, s);
	hdmi.ip_data.ops->dump_pll(&hdmi.ip_data, s);
	hdmi.ip_data.ops->dump_phy(&hdmi.ip_data, s);
	hdmi.ip_data.ops->dump_core(&hdmi.ip_data, s);

	hdmi_runtime_put();
	mutex_unlock(&hdmi.lock);
}

int omapdss_hdmi_read_edid(u8 *buf, int len)
{
	int r;

	mutex_lock(&hdmi.lock);

	r = hdmi_runtime_get();
	BUG_ON(r);

	r = hdmi.ip_data.ops->read_edid(&hdmi.ip_data, buf, len);

	hdmi_runtime_put();
	mutex_unlock(&hdmi.lock);

	return r;
}

bool omapdss_hdmi_detect(void)
{
	int r;

	mutex_lock(&hdmi.lock);

	r = hdmi_runtime_get();
	BUG_ON(r);

	r = hdmi.ip_data.ops->detect(&hdmi.ip_data);

	hdmi_runtime_put();
	mutex_unlock(&hdmi.lock);

	return r == 1;
}

int omapdss_hdmi_display_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	DSSDBG("ENTER hdmi_display_enable\n");

	mutex_lock(&hdmi.lock);

	if (dssdev->manager == NULL) {
		DSSERR("failed to enable display: no manager\n");
		r = -ENODEV;
		goto err0;
	}

	r = omap_dss_start_device(dssdev);
	if (r) {
		DSSERR("failed to start device\n");
		goto err0;
	}

	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r) {
			DSSERR("failed to enable GPIO's\n");
			goto err1;
		}
	}

	r = hdmi_power_on(dssdev);
	if (r) {
		DSSERR("failed to power on device\n");
		goto err2;
	}

	mutex_unlock(&hdmi.lock);
	return 0;

err2:
	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);
err1:
	omap_dss_stop_device(dssdev);
err0:
	mutex_unlock(&hdmi.lock);
	return r;
}

void omapdss_hdmi_display_disable(struct omap_dss_device *dssdev)
{
	DSSDBG("Enter hdmi_display_disable\n");

	mutex_lock(&hdmi.lock);

	hdmi_power_off(dssdev);

	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	omap_dss_stop_device(dssdev);

	mutex_unlock(&hdmi.lock);
}

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)

static int hdmi_audio_hw_params(struct hdmi_ip_data *ip_data,
					struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct hdmi_audio_format audio_format;
	struct hdmi_audio_dma audio_dma;
	struct hdmi_core_audio_config core_cfg;
	struct hdmi_core_infoframe_audio aud_if_cfg;
	int err, n, cts;
	enum hdmi_core_audio_sample_freq sample_freq;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		core_cfg.i2s_cfg.word_max_length =
			HDMI_AUDIO_I2S_MAX_WORD_20BITS;
		core_cfg.i2s_cfg.word_length = HDMI_AUDIO_I2S_CHST_WORD_16_BITS;
		core_cfg.i2s_cfg.in_length_bits =
			HDMI_AUDIO_I2S_INPUT_LENGTH_16;
		core_cfg.i2s_cfg.justification = HDMI_AUDIO_JUSTIFY_LEFT;
		audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_TWOSAMPLES;
		audio_format.sample_size = HDMI_AUDIO_SAMPLE_16BITS;
		audio_format.justification = HDMI_AUDIO_JUSTIFY_LEFT;
		audio_dma.transfer_size = 0x10;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		core_cfg.i2s_cfg.word_max_length =
			HDMI_AUDIO_I2S_MAX_WORD_24BITS;
		core_cfg.i2s_cfg.word_length = HDMI_AUDIO_I2S_CHST_WORD_24_BITS;
		core_cfg.i2s_cfg.in_length_bits =
			HDMI_AUDIO_I2S_INPUT_LENGTH_24;
		audio_format.samples_per_word = HDMI_AUDIO_ONEWORD_ONESAMPLE;
		audio_format.sample_size = HDMI_AUDIO_SAMPLE_24BITS;
		audio_format.justification = HDMI_AUDIO_JUSTIFY_RIGHT;
		core_cfg.i2s_cfg.justification = HDMI_AUDIO_JUSTIFY_RIGHT;
		audio_dma.transfer_size = 0x20;
		break;
	default:
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 32000:
		sample_freq = HDMI_AUDIO_FS_32000;
		break;
	case 44100:
		sample_freq = HDMI_AUDIO_FS_44100;
		break;
	case 48000:
		sample_freq = HDMI_AUDIO_FS_48000;
		break;
	default:
		return -EINVAL;
	}

	err = hdmi_config_audio_acr(ip_data, params_rate(params), &n, &cts);
	if (err < 0)
		return err;

	/* Audio wrapper config */
	audio_format.stereo_channels = HDMI_AUDIO_STEREO_ONECHANNEL;
	audio_format.active_chnnls_msk = 0x03;
	audio_format.type = HDMI_AUDIO_TYPE_LPCM;
	audio_format.sample_order = HDMI_AUDIO_SAMPLE_LEFT_FIRST;
	/* Disable start/stop signals of IEC 60958 blocks */
	audio_format.en_sig_blk_strt_end = HDMI_AUDIO_BLOCK_SIG_STARTEND_OFF;

	audio_dma.block_size = 0xC0;
	audio_dma.mode = HDMI_AUDIO_TRANSF_DMA;
	audio_dma.fifo_threshold = 0x20; /* in number of samples */

	hdmi_wp_audio_config_dma(ip_data, &audio_dma);
	hdmi_wp_audio_config_format(ip_data, &audio_format);

	/*
	 * I2S config
	 */
	core_cfg.i2s_cfg.en_high_bitrate_aud = false;
	/* Only used with high bitrate audio */
	core_cfg.i2s_cfg.cbit_order = false;
	/* Serial data and word select should change on sck rising edge */
	core_cfg.i2s_cfg.sck_edge_mode = HDMI_AUDIO_I2S_SCK_EDGE_RISING;
	core_cfg.i2s_cfg.vbit = HDMI_AUDIO_I2S_VBIT_FOR_PCM;
	/* Set I2S word select polarity */
	core_cfg.i2s_cfg.ws_polarity = HDMI_AUDIO_I2S_WS_POLARITY_LOW_IS_LEFT;
	core_cfg.i2s_cfg.direction = HDMI_AUDIO_I2S_MSB_SHIFTED_FIRST;
	/* Set serial data to word select shift. See Phillips spec. */
	core_cfg.i2s_cfg.shift = HDMI_AUDIO_I2S_FIRST_BIT_SHIFT;
	/* Enable one of the four available serial data channels */
	core_cfg.i2s_cfg.active_sds = HDMI_AUDIO_I2S_SD0_EN;

	/* Core audio config */
	core_cfg.freq_sample = sample_freq;
	core_cfg.n = n;
	core_cfg.cts = cts;
	if (dss_has_feature(FEAT_HDMI_CTS_SWMODE)) {
		core_cfg.aud_par_busclk = 0;
		core_cfg.cts_mode = HDMI_AUDIO_CTS_MODE_SW;
		core_cfg.use_mclk = false;
	} else {
		core_cfg.aud_par_busclk = (((128 * 31) - 1) << 8);
		core_cfg.cts_mode = HDMI_AUDIO_CTS_MODE_HW;
		core_cfg.use_mclk = true;
		core_cfg.mclk_mode = HDMI_AUDIO_MCLK_128FS;
	}
	core_cfg.layout = HDMI_AUDIO_LAYOUT_2CH;
	core_cfg.en_spdif = false;
	/* Use sample frequency from channel status word */
	core_cfg.fs_override = true;
	/* Enable ACR packets */
	core_cfg.en_acr_pkt = true;
	/* Disable direct streaming digital audio */
	core_cfg.en_dsd_audio = false;
	/* Use parallel audio interface */
	core_cfg.en_parallel_aud_input = true;

	hdmi_core_audio_config(ip_data, &core_cfg);

	/*
	 * Configure packet
	 * info frame audio see doc CEA861-D page 74
	 */
	aud_if_cfg.db1_coding_type = HDMI_INFOFRAME_AUDIO_DB1CT_FROM_STREAM;
	aud_if_cfg.db1_channel_count = 2;
	aud_if_cfg.db2_sample_freq = HDMI_INFOFRAME_AUDIO_DB2SF_FROM_STREAM;
	aud_if_cfg.db2_sample_size = HDMI_INFOFRAME_AUDIO_DB2SS_FROM_STREAM;
	aud_if_cfg.db4_channel_alloc = 0x00;
	aud_if_cfg.db5_downmix_inh = false;
	aud_if_cfg.db5_lsv = 0;

	hdmi_core_audio_infoframe_config(ip_data, &aud_if_cfg);
	return 0;
}

static int hdmi_audio_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	if (!hdmi.mode) {
		pr_err("Current video settings do not support audio.\n");
		return -EIO;
	}
	return 0;
}

static struct snd_soc_codec_driver hdmi_audio_codec_drv = {
};

static struct snd_soc_dai_ops hdmi_audio_codec_ops = {
	.hw_params = hdmi_audio_hw_params,
	.trigger = hdmi_audio_trigger,
	.startup = hdmi_audio_startup,
};

static struct snd_soc_dai_driver hdmi_codec_dai_drv = {
		.name = "hdmi-audio-codec",
		.playback = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &hdmi_audio_codec_ops,
};
#endif

static int hdmi_get_clocks(struct platform_device *pdev)
{
	struct clk *clk;

	clk = clk_get(&pdev->dev, "sys_clk");
	if (IS_ERR(clk)) {
		DSSERR("can't get sys_clk\n");
		return PTR_ERR(clk);
	}

	hdmi.sys_clk = clk;

	return 0;
}

static void hdmi_put_clocks(void)
{
	if (hdmi.sys_clk)
		clk_put(hdmi.sys_clk);
}

/* HDMI HW IP initialisation */
static int omapdss_hdmihw_probe(struct platform_device *pdev)
{
	struct resource *hdmi_mem;
	int r;

	hdmi.pdata = pdev->dev.platform_data;
	hdmi.pdev = pdev;

	mutex_init(&hdmi.lock);

	hdmi_mem = platform_get_resource(hdmi.pdev, IORESOURCE_MEM, 0);
	if (!hdmi_mem) {
		DSSERR("can't get IORESOURCE_MEM HDMI\n");
		return -EINVAL;
	}

	/* Base address taken from platform */
	hdmi.ip_data.base_wp = ioremap(hdmi_mem->start,
						resource_size(hdmi_mem));
	if (!hdmi.ip_data.base_wp) {
		DSSERR("can't ioremap WP\n");
		return -ENOMEM;
	}

	r = hdmi_get_clocks(pdev);
	if (r) {
		iounmap(hdmi.ip_data.base_wp);
		return r;
	}

	pm_runtime_enable(&pdev->dev);

	hdmi.ip_data.core_sys_offset = HDMI_CORE_SYS;
	hdmi.ip_data.core_av_offset = HDMI_CORE_AV;
	hdmi.ip_data.pll_offset = HDMI_PLLCTRL;
	hdmi.ip_data.phy_offset = HDMI_PHY;

	hdmi_panel_init();

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)

	/* Register ASoC codec DAI */
	r = snd_soc_register_codec(&pdev->dev, &hdmi_audio_codec_drv,
					&hdmi_codec_dai_drv, 1);
	if (r) {
		DSSERR("can't register ASoC HDMI audio codec\n");
		return r;
	}
#endif
	return 0;
}

static int omapdss_hdmihw_remove(struct platform_device *pdev)
{
	hdmi_panel_exit();

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
	snd_soc_unregister_codec(&pdev->dev);
#endif

	pm_runtime_disable(&pdev->dev);

	hdmi_put_clocks();

	iounmap(hdmi.ip_data.base_wp);

	return 0;
}

static int hdmi_runtime_suspend(struct device *dev)
{
	clk_disable(hdmi.sys_clk);

	dispc_runtime_put();
	dss_runtime_put();

	return 0;
}

static int hdmi_runtime_resume(struct device *dev)
{
	int r;

	r = dss_runtime_get();
	if (r < 0)
		goto err_get_dss;

	r = dispc_runtime_get();
	if (r < 0)
		goto err_get_dispc;


	clk_enable(hdmi.sys_clk);

	return 0;

err_get_dispc:
	dss_runtime_put();
err_get_dss:
	return r;
}

static const struct dev_pm_ops hdmi_pm_ops = {
	.runtime_suspend = hdmi_runtime_suspend,
	.runtime_resume = hdmi_runtime_resume,
};

static struct platform_driver omapdss_hdmihw_driver = {
	.probe          = omapdss_hdmihw_probe,
	.remove         = omapdss_hdmihw_remove,
	.driver         = {
		.name   = "omapdss_hdmi",
		.owner  = THIS_MODULE,
		.pm	= &hdmi_pm_ops,
	},
};

int hdmi_init_platform_driver(void)
{
	return platform_driver_register(&omapdss_hdmihw_driver);
}

void hdmi_uninit_platform_driver(void)
{
	return platform_driver_unregister(&omapdss_hdmihw_driver);
}
