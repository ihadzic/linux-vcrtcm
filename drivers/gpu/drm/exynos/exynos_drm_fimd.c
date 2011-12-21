/* exynos_drm_fimd.c
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors:
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include "drmP.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <drm/exynos_drm.h>
#include <plat/regs-fb-v4.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_crtc.h"

/*
 * FIMD is stand for Fully Interactive Mobile Display and
 * as a display controller, it transfers contents drawn on memory
 * to a LCD Panel through Display Interfaces such as RGB or
 * CPU Interface.
 */

/* position control register for hardware window 0, 2 ~ 4.*/
#define VIDOSD_A(win)		(VIDOSD_BASE + 0x00 + (win) * 16)
#define VIDOSD_B(win)		(VIDOSD_BASE + 0x04 + (win) * 16)
/* size control register for hardware window 0. */
#define VIDOSD_C_SIZE_W0	(VIDOSD_BASE + 0x08)
/* alpha control register for hardware window 1 ~ 4. */
#define VIDOSD_C(win)		(VIDOSD_BASE + 0x18 + (win) * 16)
/* size control register for hardware window 1 ~ 4. */
#define VIDOSD_D(win)		(VIDOSD_BASE + 0x0C + (win) * 16)

#define VIDWx_BUF_START(win, buf)	(VIDW_BUF_START(buf) + (win) * 8)
#define VIDWx_BUF_END(win, buf)		(VIDW_BUF_END(buf) + (win) * 8)
#define VIDWx_BUF_SIZE(win, buf)	(VIDW_BUF_SIZE(buf) + (win) * 4)

/* color key control register for hardware window 1 ~ 4. */
#define WKEYCON0_BASE(x)		((WKEYCON0 + 0x140) + (x * 8))
/* color key value register for hardware window 1 ~ 4. */
#define WKEYCON1_BASE(x)		((WKEYCON1 + 0x140) + (x * 8))

/* FIMD has totally five hardware windows. */
#define WINDOWS_NR	5

#define get_fimd_context(dev)	platform_get_drvdata(to_platform_device(dev))

struct fimd_win_data {
	unsigned int		offset_x;
	unsigned int		offset_y;
	unsigned int		ovl_width;
	unsigned int		ovl_height;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		bpp;
	dma_addr_t		dma_addr;
	void __iomem		*vaddr;
	unsigned int		buf_offsize;
	unsigned int		line_size;	/* bytes */
};

struct fimd_context {
	struct exynos_drm_subdrv	subdrv;
	int				irq;
	struct drm_crtc			*crtc;
	struct clk			*bus_clk;
	struct clk			*lcd_clk;
	struct resource			*regs_res;
	void __iomem			*regs;
	struct fimd_win_data		win_data[WINDOWS_NR];
	unsigned int			clkdiv;
	unsigned int			default_win;
	unsigned long			irq_flags;
	u32				vidcon0;
	u32				vidcon1;

	struct fb_videomode		*timing;
};

static bool fimd_display_is_connected(struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return true;
}

static void *fimd_get_timing(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return ctx->timing;
}

static int fimd_check_timing(struct device *dev, void *timing)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return 0;
}

static int fimd_display_power_on(struct device *dev, int mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return 0;
}

static struct exynos_drm_display_ops fimd_display_ops = {
	.type = EXYNOS_DISPLAY_TYPE_LCD,
	.is_connected = fimd_display_is_connected,
	.get_timing = fimd_get_timing,
	.check_timing = fimd_check_timing,
	.power_on = fimd_display_power_on,
};

static void fimd_commit(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fb_videomode *timing = ctx->timing;
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* setup polarity values from machine code. */
	writel(ctx->vidcon1, ctx->regs + VIDCON1);

	/* setup vertical timing values. */
	val = VIDTCON0_VBPD(timing->upper_margin - 1) |
	       VIDTCON0_VFPD(timing->lower_margin - 1) |
	       VIDTCON0_VSPW(timing->vsync_len - 1);
	writel(val, ctx->regs + VIDTCON0);

	/* setup horizontal timing values.  */
	val = VIDTCON1_HBPD(timing->left_margin - 1) |
	       VIDTCON1_HFPD(timing->right_margin - 1) |
	       VIDTCON1_HSPW(timing->hsync_len - 1);
	writel(val, ctx->regs + VIDTCON1);

	/* setup horizontal and vertical display size. */
	val = VIDTCON2_LINEVAL(timing->yres - 1) |
	       VIDTCON2_HOZVAL(timing->xres - 1);
	writel(val, ctx->regs + VIDTCON2);

	/* setup clock source, clock divider, enable dma. */
	val = ctx->vidcon0;
	val &= ~(VIDCON0_CLKVAL_F_MASK | VIDCON0_CLKDIR);

	if (ctx->clkdiv > 1)
		val |= VIDCON0_CLKVAL_F(ctx->clkdiv - 1) | VIDCON0_CLKDIR;
	else
		val &= ~VIDCON0_CLKDIR;	/* 1:1 clock */

	/*
	 * fields of register with prefix '_F' would be updated
	 * at vsync(same as dma start)
	 */
	val |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(val, ctx->regs + VIDCON0);
}

static void fimd_disable(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct exynos_drm_subdrv *subdrv = &ctx->subdrv;
	struct drm_device *drm_dev = subdrv->drm_dev;
	struct exynos_drm_manager *manager = &subdrv->manager;
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* fimd dma off */
	val = readl(ctx->regs + VIDCON0);
	val &= ~(VIDCON0_ENVID | VIDCON0_ENVID_F);
	writel(val, ctx->regs + VIDCON0);

	/*
	 * if vblank is enabled status with dma off then
	 * it disables vsync interrupt.
	 */
	if (drm_dev->vblank_enabled[manager->pipe] &&
		atomic_read(&drm_dev->vblank_refcount[manager->pipe])) {
		drm_vblank_put(drm_dev, manager->pipe);

		/*
		 * if vblank_disable_allowed is 0 then disable
		 * vsync interrupt right now else the vsync interrupt
		 * would be disabled by drm timer once a current process
		 * gives up ownershop of vblank event.
		 */
		if (!drm_dev->vblank_disable_allowed)
			drm_vblank_off(drm_dev, manager->pipe);
	}
}

static int fimd_enable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!test_and_set_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val |= VIDINTCON0_INT_ENABLE;
		val |= VIDINTCON0_INT_FRAME;

		val &= ~VIDINTCON0_FRAMESEL0_MASK;
		val |= VIDINTCON0_FRAMESEL0_VSYNC;
		val &= ~VIDINTCON0_FRAMESEL1_MASK;
		val |= VIDINTCON0_FRAMESEL1_NONE;

		writel(val, ctx->regs + VIDINTCON0);
	}

	return 0;
}

static void fimd_disable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (test_and_clear_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val &= ~VIDINTCON0_INT_FRAME;
		val &= ~VIDINTCON0_INT_ENABLE;

		writel(val, ctx->regs + VIDINTCON0);
	}
}

static struct exynos_drm_manager_ops fimd_manager_ops = {
	.commit = fimd_commit,
	.disable = fimd_disable,
	.enable_vblank = fimd_enable_vblank,
	.disable_vblank = fimd_disable_vblank,
};

static void fimd_win_mode_set(struct device *dev,
			      struct exynos_drm_overlay *overlay)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	unsigned long offset;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!overlay) {
		dev_err(dev, "overlay is NULL\n");
		return;
	}

	offset = overlay->fb_x * (overlay->bpp >> 3);
	offset += overlay->fb_y * overlay->pitch;

	DRM_DEBUG_KMS("offset = 0x%lx, pitch = %x\n", offset, overlay->pitch);

	win_data = &ctx->win_data[ctx->default_win];

	win_data->offset_x = overlay->crtc_x;
	win_data->offset_y = overlay->crtc_y;
	win_data->ovl_width = overlay->crtc_width;
	win_data->ovl_height = overlay->crtc_height;
	win_data->fb_width = overlay->fb_width;
	win_data->fb_height = overlay->fb_height;
	win_data->dma_addr = overlay->dma_addr + offset;
	win_data->vaddr = overlay->vaddr + offset;
	win_data->bpp = overlay->bpp;
	win_data->buf_offsize = (overlay->fb_width - overlay->crtc_width) *
				(overlay->bpp >> 3);
	win_data->line_size = overlay->crtc_width * (overlay->bpp >> 3);

	DRM_DEBUG_KMS("offset_x = %d, offset_y = %d\n",
			win_data->offset_x, win_data->offset_y);
	DRM_DEBUG_KMS("ovl_width = %d, ovl_height = %d\n",
			win_data->ovl_width, win_data->ovl_height);
	DRM_DEBUG_KMS("paddr = 0x%lx, vaddr = 0x%lx\n",
			(unsigned long)win_data->dma_addr,
			(unsigned long)win_data->vaddr);
	DRM_DEBUG_KMS("fb_width = %d, crtc_width = %d\n",
			overlay->fb_width, overlay->crtc_width);
}

static void fimd_win_set_pixfmt(struct device *dev, unsigned int win)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data = &ctx->win_data[win];
	unsigned long val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	val = WINCONx_ENWIN;

	switch (win_data->bpp) {
	case 1:
		val |= WINCON0_BPPMODE_1BPP;
		val |= WINCONx_BITSWP;
		val |= WINCONx_BURSTLEN_4WORD;
		break;
	case 2:
		val |= WINCON0_BPPMODE_2BPP;
		val |= WINCONx_BITSWP;
		val |= WINCONx_BURSTLEN_8WORD;
		break;
	case 4:
		val |= WINCON0_BPPMODE_4BPP;
		val |= WINCONx_BITSWP;
		val |= WINCONx_BURSTLEN_8WORD;
		break;
	case 8:
		val |= WINCON0_BPPMODE_8BPP_PALETTE;
		val |= WINCONx_BURSTLEN_8WORD;
		val |= WINCONx_BYTSWP;
		break;
	case 16:
		val |= WINCON0_BPPMODE_16BPP_565;
		val |= WINCONx_HAWSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case 24:
		val |= WINCON0_BPPMODE_24BPP_888;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case 32:
		val |= WINCON1_BPPMODE_28BPP_A4888
			| WINCON1_BLD_PIX | WINCON1_ALPHA_SEL;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	default:
		DRM_DEBUG_KMS("invalid pixel size so using unpacked 24bpp.\n");

		val |= WINCON0_BPPMODE_24BPP_888;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	}

	DRM_DEBUG_KMS("bpp = %d\n", win_data->bpp);

	writel(val, ctx->regs + WINCON(win));
}

static void fimd_win_set_colkey(struct device *dev, unsigned int win)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	unsigned int keycon0 = 0, keycon1 = 0;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	keycon0 = ~(WxKEYCON0_KEYBL_EN | WxKEYCON0_KEYEN_F |
			WxKEYCON0_DIRCON) | WxKEYCON0_COMPKEY(0);

	keycon1 = WxKEYCON1_COLVAL(0xffffffff);

	writel(keycon0, ctx->regs + WKEYCON0_BASE(win));
	writel(keycon1, ctx->regs + WKEYCON1_BASE(win));
}

static void fimd_win_commit(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int win = ctx->default_win;
	unsigned long val, alpha, size;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];

	/*
	 * SHADOWCON register is used for enabling timing.
	 *
	 * for example, once only width value of a register is set,
	 * if the dma is started then fimd hardware could malfunction so
	 * with protect window setting, the register fields with prefix '_F'
	 * wouldn't be updated at vsync also but updated once unprotect window
	 * is set.
	 */

	/* protect windows */
	val = readl(ctx->regs + SHADOWCON);
	val |= SHADOWCON_WINx_PROTECT(win);
	writel(val, ctx->regs + SHADOWCON);

	/* buffer start address */
	val = (unsigned long)win_data->dma_addr;
	writel(val, ctx->regs + VIDWx_BUF_START(win, 0));

	/* buffer end address */
	size = win_data->fb_width * win_data->ovl_height * (win_data->bpp >> 3);
	val = (unsigned long)(win_data->dma_addr + size);
	writel(val, ctx->regs + VIDWx_BUF_END(win, 0));

	DRM_DEBUG_KMS("start addr = 0x%lx, end addr = 0x%lx, size = 0x%lx\n",
			(unsigned long)win_data->dma_addr, val, size);
	DRM_DEBUG_KMS("ovl_width = %d, ovl_height = %d\n",
			win_data->ovl_width, win_data->ovl_height);

	/* buffer size */
	val = VIDW_BUF_SIZE_OFFSET(win_data->buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH(win_data->line_size);
	writel(val, ctx->regs + VIDWx_BUF_SIZE(win, 0));

	/* OSD position */
	val = VIDOSDxA_TOPLEFT_X(win_data->offset_x) |
		VIDOSDxA_TOPLEFT_Y(win_data->offset_y);
	writel(val, ctx->regs + VIDOSD_A(win));

	val = VIDOSDxB_BOTRIGHT_X(win_data->offset_x +
					win_data->ovl_width - 1) |
		VIDOSDxB_BOTRIGHT_Y(win_data->offset_y +
					win_data->ovl_height - 1);
	writel(val, ctx->regs + VIDOSD_B(win));

	DRM_DEBUG_KMS("osd pos: tx = %d, ty = %d, bx = %d, by = %d\n",
			win_data->offset_x, win_data->offset_y,
			win_data->offset_x + win_data->ovl_width - 1,
			win_data->offset_y + win_data->ovl_height - 1);

	/* hardware window 0 doesn't support alpha channel. */
	if (win != 0) {
		/* OSD alpha */
		alpha = VIDISD14C_ALPHA1_R(0xf) |
			VIDISD14C_ALPHA1_G(0xf) |
			VIDISD14C_ALPHA1_B(0xf);

		writel(alpha, ctx->regs + VIDOSD_C(win));
	}

	/* OSD size */
	if (win != 3 && win != 4) {
		u32 offset = VIDOSD_D(win);
		if (win == 0)
			offset = VIDOSD_C_SIZE_W0;
		val = win_data->ovl_width * win_data->ovl_height;
		writel(val, ctx->regs + offset);

		DRM_DEBUG_KMS("osd size = 0x%x\n", (unsigned int)val);
	}

	fimd_win_set_pixfmt(dev, win);

	/* hardware window 0 doesn't support color key. */
	if (win != 0)
		fimd_win_set_colkey(dev, win);

	/* Enable DMA channel and unprotect windows */
	val = readl(ctx->regs + SHADOWCON);
	val |= SHADOWCON_CHx_ENABLE(win);
	val &= ~SHADOWCON_WINx_PROTECT(win);
	writel(val, ctx->regs + SHADOWCON);
}

static void fimd_win_disable(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	int win = ctx->default_win;
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (win < 0 || win > WINDOWS_NR)
		return;

	/* protect windows */
	val = readl(ctx->regs + SHADOWCON);
	val |= SHADOWCON_WINx_PROTECT(win);
	writel(val, ctx->regs + SHADOWCON);

	/* wincon */
	val = readl(ctx->regs + WINCON(win));
	val &= ~WINCONx_ENWIN;
	writel(val, ctx->regs + WINCON(win));

	/* unprotect windows */
	val = readl(ctx->regs + SHADOWCON);
	val &= ~SHADOWCON_CHx_ENABLE(win);
	val &= ~SHADOWCON_WINx_PROTECT(win);
	writel(val, ctx->regs + SHADOWCON);
}

static struct exynos_drm_overlay_ops fimd_overlay_ops = {
	.mode_set = fimd_win_mode_set,
	.commit = fimd_win_commit,
	.disable = fimd_win_disable,
};

static void fimd_finish_pageflip(struct drm_device *drm_dev, int crtc)
{
	struct exynos_drm_private *dev_priv = drm_dev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	struct timeval now;
	unsigned long flags;
	bool is_checked = false;

	spin_lock_irqsave(&drm_dev->event_lock, flags);

	list_for_each_entry_safe(e, t, &dev_priv->pageflip_event_list,
			base.link) {
		/* if event's pipe isn't same as crtc then ignore it. */
		if (crtc != e->pipe)
			continue;

		is_checked = true;

		do_gettimeofday(&now);
		e->event.sequence = 0;
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;

		list_move_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}

	if (is_checked)
		drm_vblank_put(drm_dev, crtc);

	spin_unlock_irqrestore(&drm_dev->event_lock, flags);
}

static irqreturn_t fimd_irq_handler(int irq, void *dev_id)
{
	struct fimd_context *ctx = (struct fimd_context *)dev_id;
	struct exynos_drm_subdrv *subdrv = &ctx->subdrv;
	struct drm_device *drm_dev = subdrv->drm_dev;
	struct exynos_drm_manager *manager = &subdrv->manager;
	u32 val;

	val = readl(ctx->regs + VIDINTCON1);

	if (val & VIDINTCON1_INT_FRAME)
		/* VSYNC interrupt */
		writel(VIDINTCON1_INT_FRAME, ctx->regs + VIDINTCON1);

	/*
	 * in case that vblank_disable_allowed is 1, it could induce
	 * the problem that manager->pipe could be -1 because with
	 * disable callback, vsync interrupt isn't disabled and at this moment,
	 * vsync interrupt could occur. the vsync interrupt would be disabled
	 * by timer handler later.
	 */
	if (manager->pipe == -1)
		return IRQ_HANDLED;

	drm_handle_vblank(drm_dev, manager->pipe);
	fimd_finish_pageflip(drm_dev, manager->pipe);

	return IRQ_HANDLED;
}

static int fimd_subdrv_probe(struct drm_device *drm_dev, struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = 1, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	drm_dev->irq_enabled = 1;

	return 0;
}

static void fimd_subdrv_remove(struct drm_device *drm_dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */
}

static int fimd_calc_clkdiv(struct fimd_context *ctx,
			    struct fb_videomode *timing)
{
	unsigned long clk = clk_get_rate(ctx->lcd_clk);
	u32 retrace;
	u32 clkdiv;
	u32 best_framerate = 0;
	u32 framerate;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	retrace = timing->left_margin + timing->hsync_len +
				timing->right_margin + timing->xres;
	retrace *= timing->upper_margin + timing->vsync_len +
				timing->lower_margin + timing->yres;

	/* default framerate is 60Hz */
	if (!timing->refresh)
		timing->refresh = 60;

	clk /= retrace;

	for (clkdiv = 1; clkdiv < 0x100; clkdiv++) {
		int tmp;

		/* get best framerate */
		framerate = clk / clkdiv;
		tmp = timing->refresh - framerate;
		if (tmp < 0) {
			best_framerate = framerate;
			continue;
		} else {
			if (!best_framerate)
				best_framerate = framerate;
			else if (tmp < (best_framerate - framerate))
				best_framerate = framerate;
			break;
		}
	}

	return clkdiv;
}

static void fimd_clear_win(struct fimd_context *ctx, int win)
{
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	writel(0, ctx->regs + WINCON(win));
	writel(0, ctx->regs + VIDOSD_A(win));
	writel(0, ctx->regs + VIDOSD_B(win));
	writel(0, ctx->regs + VIDOSD_C(win));

	if (win == 1 || win == 2)
		writel(0, ctx->regs + VIDOSD_D(win));

	val = readl(ctx->regs + SHADOWCON);
	val &= ~SHADOWCON_WINx_PROTECT(win);
	writel(val, ctx->regs + SHADOWCON);
}

static int __devinit fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx;
	struct exynos_drm_subdrv *subdrv;
	struct exynos_drm_fimd_pdata *pdata;
	struct fb_videomode *timing;
	struct resource *res;
	int win;
	int ret = -EINVAL;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(dev, "no platform data specified\n");
		return -EINVAL;
	}

	timing = &pdata->timing;
	if (!timing) {
		dev_err(dev, "timing is null.\n");
		return -EINVAL;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->bus_clk = clk_get(dev, "fimd");
	if (IS_ERR(ctx->bus_clk)) {
		dev_err(dev, "failed to get bus clock\n");
		ret = PTR_ERR(ctx->bus_clk);
		goto err_clk_get;
	}

	clk_enable(ctx->bus_clk);

	ctx->lcd_clk = clk_get(dev, "sclk_fimd");
	if (IS_ERR(ctx->lcd_clk)) {
		dev_err(dev, "failed to get lcd clock\n");
		ret = PTR_ERR(ctx->lcd_clk);
		goto err_bus_clk;
	}

	clk_enable(ctx->lcd_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to find registers\n");
		ret = -ENOENT;
		goto err_clk;
	}

	ctx->regs_res = request_mem_region(res->start, resource_size(res),
					   dev_name(dev));
	if (!ctx->regs_res) {
		dev_err(dev, "failed to claim register region\n");
		ret = -ENOENT;
		goto err_clk;
	}

	ctx->regs = ioremap(res->start, resource_size(res));
	if (!ctx->regs) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_req_region_io;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "irq request failed.\n");
		goto err_req_region_irq;
	}

	ctx->irq = res->start;

	for (win = 0; win < WINDOWS_NR; win++)
		fimd_clear_win(ctx, win);

	ret = request_irq(ctx->irq, fimd_irq_handler, 0, "drm_fimd", ctx);
	if (ret < 0) {
		dev_err(dev, "irq request failed.\n");
		goto err_req_irq;
	}

	ctx->clkdiv = fimd_calc_clkdiv(ctx, timing);
	ctx->vidcon0 = pdata->vidcon0;
	ctx->vidcon1 = pdata->vidcon1;
	ctx->default_win = pdata->default_win;
	ctx->timing = timing;

	timing->pixclock = clk_get_rate(ctx->lcd_clk) / ctx->clkdiv;

	DRM_DEBUG_KMS("pixel clock = %d, clkdiv = %d\n",
			timing->pixclock, ctx->clkdiv);

	subdrv = &ctx->subdrv;

	subdrv->probe = fimd_subdrv_probe;
	subdrv->remove = fimd_subdrv_remove;
	subdrv->manager.pipe = -1;
	subdrv->manager.ops = &fimd_manager_ops;
	subdrv->manager.overlay_ops = &fimd_overlay_ops;
	subdrv->manager.display_ops = &fimd_display_ops;
	subdrv->manager.dev = dev;

	platform_set_drvdata(pdev, ctx);
	exynos_drm_subdrv_register(subdrv);

	return 0;

err_req_irq:
err_req_region_irq:
	iounmap(ctx->regs);

err_req_region_io:
	release_resource(ctx->regs_res);
	kfree(ctx->regs_res);

err_clk:
	clk_disable(ctx->lcd_clk);
	clk_put(ctx->lcd_clk);

err_bus_clk:
	clk_disable(ctx->bus_clk);
	clk_put(ctx->bus_clk);

err_clk_get:
	kfree(ctx);
	return ret;
}

static int __devexit fimd_remove(struct platform_device *pdev)
{
	struct fimd_context *ctx = platform_get_drvdata(pdev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_drm_subdrv_unregister(&ctx->subdrv);

	clk_disable(ctx->lcd_clk);
	clk_disable(ctx->bus_clk);
	clk_put(ctx->lcd_clk);
	clk_put(ctx->bus_clk);

	iounmap(ctx->regs);
	release_resource(ctx->regs_res);
	kfree(ctx->regs_res);
	free_irq(ctx->irq, ctx);

	kfree(ctx);

	return 0;
}

static struct platform_driver fimd_driver = {
	.probe		= fimd_probe,
	.remove		= __devexit_p(fimd_remove),
	.driver		= {
		.name	= "exynos4-fb",
		.owner	= THIS_MODULE,
	},
};

static int __init fimd_init(void)
{
	return platform_driver_register(&fimd_driver);
}

static void __exit fimd_exit(void)
{
	platform_driver_unregister(&fimd_driver);
}

module_init(fimd_init);
module_exit(fimd_exit);

MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung DRM FIMD Driver");
MODULE_LICENSE("GPL");
