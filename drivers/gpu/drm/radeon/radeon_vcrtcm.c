/*
 * Copyright 2010 Alcatel-Lucent Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author: Ilija Hadzic <ihadzic@research.bell-labs.com>
 */

#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_virtual_crtc.h"
#include "radeon_vcrtcm_kernel.h"

/*
 * NB: This function is called in atomic context.  Hence, it must
 * call the *nonlocking* variant of vcrtcm_g_page_flip().  That
 * vcrtcm function is specially implemented to allow calls to the
 * nonlocking variant even when the pcon is not already locked.
 */
int radeon_vcrtcm_page_flip(struct radeon_crtc *radeon_crtc,
			    uint64_t fb_location)
{
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	unsigned int tmp;
	u32 ioaddr;

	if (radeon_crtc->pconid >= 0) {
		tmp = fb_location - rdev->mc.vram_start;
		ioaddr = rdev->mc.aper_base + tmp;
		return vcrtcm_g_page_flip(radeon_crtc->pconid, ioaddr);
	}
	return 0;
}

int radeon_vcrtcm_set_fb(struct radeon_crtc *radeon_crtc,
			 int x, int y,
			 struct drm_framebuffer *fb,
			 uint64_t fb_location, int pcon_locked)
{
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	struct vcrtcm_fb vcrtcm_fb;
	unsigned int tmp;

	if (radeon_crtc->pconid >= 0) {
		enum vcrtcm_xfer_mode xfer_mode =
			radeon_crtc->xfer_mode;
		DRM_INFO("crtc %d PCON attached, calling vcrtcm_g_set_fb\n",
			 radeon_crtc->crtc_id);
		radeon_crtc->vcrtcm_push_fb = fb;
		/*
		 * tell the PCON the address and geometry of the
		 * frame buffer associated with this CRTC
		 */
		tmp = fb_location - rdev->mc.vram_start;
		vcrtcm_fb.ioaddr = rdev->mc.aper_base + tmp;
		DRM_INFO("frame buffer I/O address 0x%08x\n",
			 (unsigned int)vcrtcm_fb.ioaddr);
		vcrtcm_fb.bpp = fb->bits_per_pixel;
		vcrtcm_fb.width = fb->width;
		vcrtcm_fb.pitch = fb->pitches[0];
		vcrtcm_fb.height = fb->height;
		vcrtcm_fb.viewport_x = x;
		/* in pull mode, clipping is done by the PCON, in push mode
		 * GPU does the clipping along Y axis, so viewport_y
		 * is zero (REVISIT: this will change when we cut our
		 * own blit function that does the full clipping
		 * it also may change when we start doing tiling and if
		 * we need to align to the tile boundary
		 */
		if (xfer_mode == VCRTCM_PEER_PULL)
			vcrtcm_fb.viewport_y = y;
		else
			vcrtcm_fb.viewport_y = 0;
		vcrtcm_fb.hdisplay = crtc->mode.hdisplay;
		vcrtcm_fb.vdisplay = crtc->mode.vdisplay;
		if (pcon_locked)
			return vcrtcm_g_set_fb(radeon_crtc->pconid, &vcrtcm_fb);
		else
			return vcrtcm_g_set_fb_l(radeon_crtc->pconid,
				&vcrtcm_fb);
	}
	return 0;
}

int radeon_vcrtcm_wait(struct drm_device *dev,
		       struct drm_mode_group *mode_group)
{
	int i;

	for (i = 0; i < mode_group->num_crtcs; i++) {
		struct drm_mode_object *obj =
			drm_mode_object_find(dev, mode_group->id_list[i],
					     DRM_MODE_OBJECT_CRTC);
		struct drm_crtc *crtc = obj_to_crtc(obj);
		struct radeon_crtc *rcrtc = to_radeon_crtc(crtc);

		if ((rcrtc->pconid >= 0) && (rcrtc->enabled)) {
			int r;

			r = vcrtcm_g_wait_fb_l(rcrtc->pconid);
			if (r)
				return r;
		}
	}
	return 0;
}

void radeon_vcrtcm_xmit(struct drm_device *dev,
			struct drm_mode_group *mode_group)
{
	int i;

	for (i = 0; i < mode_group->num_crtcs; i++) {
		struct drm_mode_object *obj =
			drm_mode_object_find(dev, mode_group->id_list[i],
					     DRM_MODE_OBJECT_CRTC);
		struct drm_crtc *crtc = obj_to_crtc(obj);
		struct radeon_crtc *rcrtc = to_radeon_crtc(crtc);

		if ((rcrtc->pconid >= 0) && (rcrtc->enabled))
			vcrtcm_g_dirty_fb_l(rcrtc->pconid);
	}
}

void radeon_emulate_vblank_core(struct radeon_device *rdev,
				struct radeon_crtc *radeon_crtc)
{
	struct drm_device *ddev = rdev->ddev;

	radeon_virtual_crtc_set_emulated_vblank_time(radeon_crtc);
	radeon_crtc->emulated_vblank_counter++;
	if (radeon_crtc->vblank_emulation_enabled) {
		DRM_DEBUG("emulating vblank interrupt on virtual crtc %d\n",
			  radeon_crtc->crtc_id);
		drm_handle_vblank(ddev, radeon_crtc->crtc_id);
		rdev->pm.vblank_sync = true;
		wake_up(&rdev->irq.vblank_queue);
	} else
		DRM_DEBUG("vblank emulation for virtual crtc %d disabled\n",
			  radeon_crtc->crtc_id);

	if (radeon_crtc->pflip_emulation_enabled) {
		DRM_DEBUG("emulating page flip interrupt on virtual crtc %d\n",
			  radeon_crtc->crtc_id);
		radeon_crtc->emulated_pflip_counter++;
		radeon_virtual_crtc_handle_flip(rdev, radeon_crtc->crtc_id);
	}
}

static void radeon_emulate_vblank(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *ddev = radeon_crtc->base.dev;
	struct radeon_device *rdev = ddev->dev_private;

	radeon_emulate_vblank_core(rdev, radeon_crtc);
}

static void radeon_wait_fb_callback(struct drm_crtc *crtc)
{
	struct radeon_crtc *rcrtc = to_radeon_crtc(crtc);

	DRM_DEBUG("crtc_id %d\n", rcrtc->crtc_id);
	if (rcrtc->last_push_fence_c) {
		radeon_fence_wait(rcrtc->last_push_fence_c, false);
		radeon_fence_unref(&rcrtc->last_push_fence_c);
	}
	if (rcrtc->last_push_fence_fb) {
		radeon_fence_wait(rcrtc->last_push_fence_fb, false);
		radeon_fence_unref(&rcrtc->last_push_fence_fb);
	}
}

void radeon_vblank_emulation_work_func(struct work_struct *work)
{
	struct radeon_crtc *rcrtc = container_of(work, struct radeon_crtc,
						 vblank_emulation_work);
	struct radeon_device *rdev =
		(struct radeon_device *)rcrtc->base.dev->dev_private;

	if (rcrtc->last_push_fence_fb)
		radeon_fence_wait(rcrtc->last_push_fence_fb, false);
	rcrtc->vcrtcm_push_in_progress = 0;
	if (rcrtc->crtc_id >= rdev->num_crtc) {
		radeon_virtual_crtc_set_emulated_vblank_time(rcrtc);
		radeon_emulate_vblank(&rcrtc->base);
	}
}

/*
 * NB: This function is a vcrtcm-registered callback.  Because vcrtcm
 * always calls callbacks with the pcon already locked, this function
 * must call the *nonlocking* variants of the vcrtcm api functions.
 */
static int radeon_vcrtcm_push(struct drm_crtc *scrtc,
			      struct drm_gem_object *dbuf_fb,
			      struct drm_gem_object *dbuf_cursor)
{
	struct radeon_device *rdev = scrtc->dev->dev_private;
	int ridx = radeon_copy_dma_ring_index(rdev);
	struct radeon_crtc *srcrtc = to_radeon_crtc(scrtc);
	struct drm_framebuffer *sfb = srcrtc->vcrtcm_push_fb;
	struct drm_gem_object *scbo = srcrtc->cursor_bo;
	struct radeon_fence *fence_c = srcrtc->last_push_fence_c;
	struct radeon_fence *fence_fb = srcrtc->last_push_fence_fb;
	struct drm_gem_object *sfbbo;
	struct radeon_framebuffer *srfb;
	struct radeon_bo *src_rbo, *dst_rbo;
	uint64_t saddr, daddr;
	unsigned num_pages, size_in_bytes;

	/* bail out if we don't have the frame buffer */
	if (!sfb)
		return -ENOENT;

	/* bail out if we are already transmitting */
	if (srcrtc->vcrtcm_push_in_progress)
		return -EBUSY;

	srcrtc->vcrtcm_push_in_progress = 1;
	srfb = to_radeon_framebuffer(sfb);
	sfbbo = srfb->obj;

	/* copy the mouse cursor first (if we have one) */
	if (dbuf_cursor && scbo) {
		/* calculate gpu addresses: both buffers should be already */
		/* pinned dst_rbo has been pinned at allocation time; dst_rbo */
		/* has been pinned at cursor_set time */
		dst_rbo = gem_to_radeon_bo(dbuf_cursor);
		daddr = radeon_bo_gpu_offset(dst_rbo);
		src_rbo = gem_to_radeon_bo(scbo);
		saddr = radeon_bo_gpu_offset(src_rbo);
		size_in_bytes = srcrtc->cursor_width * srcrtc->cursor_height *
			sfb->bits_per_pixel >> 3;
		num_pages = size_in_bytes / RADEON_GPU_PAGE_SIZE;
		if (size_in_bytes % RADEON_GPU_PAGE_SIZE)
			num_pages++;
		if (num_pages) {
			DRM_DEBUG("pushing cursor: %d pages "
				  "from %llx to %llx\n",
				  num_pages, saddr, daddr);
			if (!rdev->ring[ridx].ready)
				DRM_ERROR("copy ring not ready (cursor)\n");
			else if (!fence_c)
				radeon_copy_dma(rdev, saddr, daddr,
						num_pages, &fence_c);
			else if (radeon_fence_signaled(fence_c)) {
				radeon_fence_unref(&fence_c);
				radeon_copy_dma(rdev, saddr, daddr,
						num_pages, &fence_c);
			} else
				DRM_DEBUG("overlapped push (cursor)\n");
			srcrtc->last_push_fence_c = fence_c;
		}
	}

	/* now copy the frame buffer */
	/* cacluate gpu addresses: both buffers should be already pinned */
	dst_rbo = gem_to_radeon_bo(dbuf_fb);
	daddr = radeon_bo_gpu_offset(dst_rbo);

	/* calculate saddr and adjust it for crtc offset */
	/* FIXME: this will change once we cut our own blit copy */
	/* that can carve out CRTC window precisely; also this offset */
	/* adjustment probably doesn't work for tiled buffers */
	src_rbo = gem_to_radeon_bo(sfbbo);
	saddr = radeon_bo_gpu_offset(src_rbo);
	saddr += sfb->pitches[0] * scrtc->y;

	/* calculate number of pages we need to transfer */
	/* FIXME: this will also change once we cut our own blit copy */
	size_in_bytes = sfb->pitches[0] * scrtc->mode.vdisplay;
	num_pages = size_in_bytes / RADEON_GPU_PAGE_SIZE;
	if (size_in_bytes % RADEON_GPU_PAGE_SIZE)
		num_pages++;

	DRM_DEBUG("pushing framebuffer: %d pages from %llx to %llx\n",
		  num_pages, saddr, daddr);
	if (!rdev->ring[ridx].ready)
		DRM_ERROR("copy ring not ready (framebuffer)\n");
	else if (!fence_fb)
		radeon_copy_dma(rdev, saddr, daddr, num_pages, &fence_fb);
	else if (radeon_fence_signaled(fence_fb)) {
		radeon_fence_unref(&fence_fb);
		radeon_copy_dma(rdev, saddr, daddr, num_pages, &fence_fb);
	} else
		DRM_DEBUG("overlapped push (framebuffer)\n");
	srcrtc->last_push_fence_fb = fence_fb;
	/*
	 * we are done as soon as we have scheduled the copy
	 * vblank emulation will occur vblank_emulation_work_func
	 * when copy completes
	 */
	schedule_work(&srcrtc->vblank_emulation_work);
	return 0;
}

static void radeon_vcrtcm_hotplug(struct drm_crtc *crtc)
{
	struct radeon_device *rdev =
		(struct radeon_device *)crtc->dev->dev_private;
	schedule_work(&rdev->hotplug_work);
}

int radeon_vcrtcm_detach(struct radeon_crtc *radeon_crtc)
{
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int r = -EINVAL;

	if (radeon_crtc->pconid >= 0) {
		r = vcrtcm_g_detach_l(radeon_crtc->pconid);
		radeon_crtc->pconid = -1;
		if (radeon_crtc->crtc_id >= rdev->num_crtc)
			schedule_work(&rdev->hotplug_work);
	}
	return r;
}

static void radeon_detach_callback(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	if (radeon_crtc->pconid >= 0) {
		radeon_crtc->pconid = -1;
		if (radeon_crtc->crtc_id >= rdev->num_crtc)
			schedule_work(&rdev->hotplug_work);
	}
}

struct vcrtcm_g_pcon_funcs physical_crtc_gpu_funcs = {
	.detach = radeon_detach_callback,
	.vblank = NULL, /* no vblank emulation for real CRTC */
	.wait_fb = radeon_wait_fb_callback,
	.push = radeon_vcrtcm_push,
	.hotplug = NULL /* real CRTC has its own hotplug */
};

struct vcrtcm_g_pcon_funcs virtual_crtc_gpu_funcs = {
	.detach = radeon_detach_callback,
	.vblank = radeon_emulate_vblank,
	.wait_fb = radeon_wait_fb_callback,
	.push = radeon_vcrtcm_push,
	.hotplug = radeon_vcrtcm_hotplug
};

static struct vcrtcm_g_drmdev_funcs vcrtcm_drmdev_funcs = {
};

int radeon_vcrtcm_register_drmdev(struct drm_device *dev)
{
	return vcrtcm_g_register_drmdev(dev, &vcrtcm_drmdev_funcs);
}

int radeon_vcrtcm_unregister_drmdev(struct drm_device *dev)
{
	return vcrtcm_g_unregister_drmdev(dev);
}

static int radeon_vcrtcm_attach(struct radeon_crtc *radeon_crtc,
					int pconid)
{
	int r;
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct radeon_device *rdev =
	    (struct radeon_device *)crtc->dev->dev_private;

	if (radeon_crtc->pconid >= 0)
		return -EBUSY;

	if (radeon_crtc->crtc_id < rdev->num_crtc)
		r = vcrtcm_g_attach_l(pconid, crtc,
				  &physical_crtc_gpu_funcs,
				  &radeon_crtc->xfer_mode);
	else
		r = vcrtcm_g_attach_l(pconid, crtc,
				  &virtual_crtc_gpu_funcs,
				  &radeon_crtc->xfer_mode);

	if (r) {
		radeon_crtc->pconid = -1;
		return r;
	}
	radeon_crtc->pconid = pconid;

	/* if the attach was successful, we also need to send the crtc */
	/* FB address and geometry to VCRTCM module. This is because */
	/* the attach can happed after the displays have been created */
	/* (e.g. X has already started); the application in that case */
	/* won't call set_base helper function and VCRTCM module won't */
	/* have the FB information. So we do it here (we use */
	/* atomic==1 because if this CRTC has the frame buffer */
	/* bound, it should presumably be already pinned */
	if (crtc->fb) {
		DRM_INFO("radeon_vcrtcm_g_attach: frame buffer exists\n");
		r = radeon_virtual_crtc_do_set_base(crtc, crtc->fb, crtc->x,
						    crtc->y, 1, 0);
		if (r) {
			vcrtcm_g_detach_l(radeon_crtc->pconid);
			radeon_crtc->pconid = -1;
			return r;
		}
		/* we also need to set the cursor */
		if (radeon_crtc->cursor_bo) {
			struct vcrtcm_cursor vcrtcm_cursor;
			struct radeon_device *rdev =
				(struct radeon_device *)crtc->dev->dev_private;
			struct radeon_bo *rbo;
			uint64_t cursor_gpuaddr;

			DRM_INFO("radeon_vcrtcm_g_attach: cursor exists w=%d, h=%d\n",
				 radeon_crtc->cursor_width, radeon_crtc->cursor_height);
			vcrtcm_cursor.flag = 0x0;
			vcrtcm_cursor.width = radeon_crtc->cursor_width;
			vcrtcm_cursor.height = radeon_crtc->cursor_height;
			vcrtcm_cursor.bpp = crtc->fb->bits_per_pixel;

			/* REVISIT: we don't have any other place to put it */
			/* (radeon_crtc does not maintain cursor location) */
			/* so we just set it to zero; it will be updated to */
			/* the right value at the first cursor move */
			vcrtcm_cursor.location_x = 0;
			vcrtcm_cursor.location_y = 0;

			/* cursor object should be pinned at this point so */
			/* we just go for its address */
			rbo = gem_to_radeon_bo(radeon_crtc->cursor_bo);
			r = radeon_bo_reserve(rbo, false);
			if (unlikely(r)) {
				vcrtcm_g_detach_l(radeon_crtc->pconid);
				radeon_crtc->pconid = -1;
				return r;
			}
			cursor_gpuaddr = radeon_bo_gpu_offset(rbo);
			radeon_bo_unreserve(rbo);

			vcrtcm_cursor.ioaddr =
				rdev->mc.aper_base + (cursor_gpuaddr - rdev->mc.vram_start);
			DRM_INFO("radeon_vcrtcm_attach: cursor i/o address 0x%08x\n",
				 vcrtcm_cursor.ioaddr);
			r = vcrtcm_g_set_cursor_l(radeon_crtc->pconid,
					      &vcrtcm_cursor);
			if (r) {
				vcrtcm_g_detach_l(radeon_crtc->pconid);
				radeon_crtc->pconid = -1;
				return r;
			}
		}
	}
	/* and we need to update the DPMS state */
	if (radeon_crtc->enabled)
		vcrtcm_g_set_dpms_l(radeon_crtc->pconid,
				VCRTCM_DPMS_STATE_ON);
	else
		vcrtcm_g_set_dpms_l(radeon_crtc->pconid,
				VCRTCM_DPMS_STATE_OFF);

	if (radeon_crtc->crtc_id >= rdev->num_crtc)
		schedule_work(&rdev->hotplug_work);

	return r;

}

static int radeon_vcrtcm_force(struct radeon_crtc *radeon_crtc)
{
	if (radeon_crtc->pconid >= 0)
		return vcrtcm_g_dirty_fb_l(radeon_crtc->pconid);
	else
		return -EINVAL;
}

static struct radeon_crtc
*display_index_to_radeon_crtc(struct radeon_device *rdev, int display_index)
{
	struct virtual_crtc *virtual_crtc;

	if (display_index < rdev->num_crtc)
		return rdev->mode_info.crtcs[display_index];
	virtual_crtc = radeon_virtual_crtc_lookup(rdev, display_index);
	if (virtual_crtc)
		return virtual_crtc->radeon_crtc;
	return NULL;
}

static inline int radeon_vcrtcm_set_fps(struct radeon_crtc *radeon_crtc,
					int fps)
{
	if (radeon_crtc->pconid >= 0)
		return vcrtcm_g_set_fps_l(radeon_crtc->pconid, fps);
	else
		return -EINVAL;
}

int radeon_vcrtcm_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv)
{

	radeon_vcrtcm_ctl_descriptor_t *rvcd = data;
	struct radeon_device *rdev = dev->dev_private;
	int display_index = rvcd->display_index;
	int op_code = rvcd->op_code;
	struct radeon_crtc *radeon_crtc;
	int r;

	if (file_priv->minor->type == DRM_MINOR_RENDER)
		return -EPERM;

	DRM_DEBUG("display_index %d\n", display_index);
	if (!ASIC_IS_VCRTC_CAPABLE(rdev)) {
		DRM_ERROR("GPU too old for VCRTCM\n");
		return -ENOTSUPP;
	}
	/* validate the display index */
	if (display_index >= rdev->num_crtc + rdev->num_virtual_crtc) {
		DRM_ERROR("bad display index\n");
		return -EINVAL;
	}

	radeon_crtc = display_index_to_radeon_crtc(rdev, display_index);
	if (!radeon_crtc) {
		DRM_ERROR("invalid crtc pointer\n");
		return -EINVAL;
	}

	switch (op_code) {

	case RADEON_VCRTCM_CTL_OP_CODE_NOP:
		DRM_DEBUG("nop\n");
		r = 0;
		break;

	case RADEON_VCRTCM_CTL_OP_CODE_SET_RATE:
		DRM_DEBUG("set fps\n");
		r = radeon_vcrtcm_set_fps(radeon_crtc,
					  rvcd->arg1.fps);
		break;

	case RADEON_VCRTCM_CTL_OP_CODE_ATTACH:
		DRM_DEBUG("attach\n");
		r = radeon_vcrtcm_attach(radeon_crtc,
					 rvcd->arg1.pconid);
		break;

	case RADEON_VCRTCM_CTL_OP_CODE_DETACH:
		DRM_DEBUG("detach\n");
		r = radeon_vcrtcm_detach(radeon_crtc);
		break;

	case RADEON_VCRTCM_CTL_OP_CODE_XMIT:
		DRM_DEBUG("force\n");
		r = radeon_vcrtcm_force(radeon_crtc);
		break;

	default:
		DRM_ERROR("invalid op code\n");
		r = -EINVAL;
		break;
	}
	return r;
}
