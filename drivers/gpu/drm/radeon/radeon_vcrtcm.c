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

/* note: intended to be called from ISR (atomic context), so
   no mutex/semaphore holding allowed */
int radeon_vcrtcm_page_flip(struct radeon_crtc *radeon_crtc,
			    uint64_t fb_location)
{
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	unsigned int tmp;
	u32 ioaddr;

	if (radeon_crtc->vcrtcm_pcon_info) {
		tmp = fb_location - rdev->mc.vram_start;
		ioaddr = rdev->mc.aper_base + tmp;
		return vcrtcm_gpu_page_flip(radeon_crtc->vcrtcm_pcon_info, ioaddr);
	}
	return 0;
}

int radeon_vcrtcm_set_fb(struct radeon_crtc *radeon_crtc,
			 int x, int y,
			 struct drm_framebuffer *fb,
			 uint64_t fb_location)
{
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	struct vcrtcm_fb vcrtcm_fb;
	unsigned int tmp;

	if (radeon_crtc->vcrtcm_pcon_info) {
		struct vcrtcm_pcon_props *pcon_props =
			&radeon_crtc->vcrtcm_pcon_info->props;
		DRM_INFO("crtc %d has vcrtcm HAL, calling vcrtcm_gpu_set_fb\n",
			 radeon_crtc->crtc_id);
		radeon_crtc->vcrtcm_push_fb = fb;
		/* tell the vcrtcm HAL the address and geometry of the */
		/* frame buffer associated with this CRTC */
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
		if (pcon_props->xfer_mode == VCRTCM_PEER_PULL)
			vcrtcm_fb.viewport_y = y;
		else
			vcrtcm_fb.viewport_y = 0;
		vcrtcm_fb.hdisplay = crtc->mode.hdisplay;
		vcrtcm_fb.vdisplay = crtc->mode.vdisplay;
		return vcrtcm_gpu_set_fb(radeon_crtc->vcrtcm_pcon_info, &vcrtcm_fb);
	}
	return 0;
}

int radeon_vcrtcm_wait(struct radeon_device *rdev)
{
	struct virtual_crtc *virtual_crtc;
	int i, r;

	/* REVISIT: this is extremely conservative (we wait for every */
	/* HAL of every CRTC, but we have no choice */
	/* once we start supporting multiple VMs, we will wait only */
	/* for those HALs that are serving CRTCs of the VM that sent this */
	/* batch of IBs. */

	/* first wait for all real CRTCs */
	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->mode_info.crtcs[i]->vcrtcm_pcon_info) {
			r = vcrtcm_gpu_wait_fb(rdev->mode_info.crtcs[i]->
					   vcrtcm_pcon_info);
			if (r)
				return r;
		}
	}

	/* now do the same kind of wait for all virtual CRTCs */
	list_for_each_entry(virtual_crtc, &rdev->mode_info.virtual_crtcs, list) {
		if (virtual_crtc->radeon_crtc->vcrtcm_pcon_info) {
			r = vcrtcm_gpu_wait_fb(virtual_crtc->radeon_crtc->
					   vcrtcm_pcon_info);
			if (r)
				return r;
		}
	}

	return 0;

}

void radeon_vcrtcm_xmit(struct radeon_device *rdev)
{
	int i;
	struct radeon_crtc *radeon_crtc = NULL;
	struct virtual_crtc *virtual_crtc;

	/* loop through CRTCs and mark each CRTC for transmission */
	/* back-end will evaluate the flag in the work function */
	/* and deal with the PCON transmission */

	/* REVISIT: we can (in theory) get smarter if we knew */
	/* what rendering activity belonged to what CRTC/framebuffer */
	/* however we don't since that would require that we interpret */
	/* the rendering commands in radeon_cs; maybe one day we will */
	/* have the necessary information and make this smarter */
	for (i = 0; i < rdev->num_crtc; i++) {
		radeon_crtc = rdev->mode_info.crtcs[i];
		if ((radeon_crtc->vcrtcm_pcon_info) && (radeon_crtc->enabled))
			vcrtcm_gpu_xmit_fb(radeon_crtc->vcrtcm_pcon_info);
	}

	list_for_each_entry(virtual_crtc, &rdev->mode_info.virtual_crtcs, list) {
		radeon_crtc = virtual_crtc->radeon_crtc;
		if ((radeon_crtc->vcrtcm_pcon_info) && (radeon_crtc->enabled))
			vcrtcm_gpu_xmit_fb(radeon_crtc->vcrtcm_pcon_info);
	}
}

static void radeon_detach_callback(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	DRM_INFO("dereferencing HAL pointer from crtc_id %d\n",
		 radeon_crtc->crtc_id);
	radeon_crtc->vcrtcm_pcon_info = NULL;
}

void radeon_emulate_vblank_locked(struct radeon_device *rdev,
				  struct radeon_crtc *radeon_crtc)
{
	struct drm_device *ddev = rdev->ddev;

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
	unsigned long flags;

	spin_lock_irqsave(&rdev->ih.lock, flags);
	radeon_emulate_vblank_locked(rdev, radeon_crtc);
	spin_unlock_irqrestore(&rdev->ih.lock, flags);
}

static void radeon_sync_callback(struct drm_crtc *crtc)
{
	struct drm_device *ddev;
	struct radeon_device *rdev;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int i;

	DRM_DEBUG("crtc_id %d\n", radeon_crtc->crtc_id);
	ddev = radeon_crtc->base.dev;
	rdev = ddev->dev_private;
	mutex_lock(&rdev->ring_lock);
	for (i = 0; i < RADEON_NUM_RINGS; i++)
		radeon_fence_wait_empty_locked(rdev, i);
	mutex_unlock(&rdev->ring_lock);
}

static int
radeon_vcrtcm_push_buffer_alloc(struct drm_device *dev,
				struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_bo *rbo;
	struct ttm_tt *ttm;
	int size, r;
	uint64_t addr;

	/* get the requested size and do some sanity check */
	size = PAGE_SIZE * pbd->num_pages;
	if (!size)
		return -EINVAL;

	/* create an object and pin it */
	r = radeon_bo_create(rdev, size, PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_GTT, NULL, &rbo);
	if (r)
		return r;
	r = radeon_bo_reserve(rbo, false);
	if (unlikely(r)) {
		radeon_bo_unref(&rbo);
		return r;
	}
	/* NB: we don't store GPU address of the bo because that's */
	/* radeon-specific stuff and we don't want to expose the PCON */
	/* to it; push callback (that actually does the copy) */
	/* will have to re-retrieve it each time it is called */
	r = radeon_bo_pin(rbo, RADEON_GEM_DOMAIN_GTT, &addr);
	DRM_DEBUG("vcrtcm push buffer allocated name=%d, gpu_addr=0x%llx\n",
		  rbo->gem_base.name, addr);
	if (r) {
		radeon_bo_unreserve(rbo);
		radeon_bo_unref(&rbo);
		return r;
	}

	ttm = rbo->tbo.ttm;
	radeon_bo_unreserve(rbo);

	/* extract information that the PCON cares about */
	/* so that it can get to it without calling any DRM/TTM/GEM funcs */
	pbd->gpu_private = &rbo->gem_base;
	pbd->num_pages = ttm->num_pages;
	pbd->pages = ttm->pages;

	return 0;
}

static void radeon_vcrtcm_push_buffer_free(struct drm_gem_object *obj)
{
	struct radeon_bo *rbo = gem_to_radeon_bo(obj);

	DRM_DEBUG("vcrtcm push buffer freed name=%d\n", obj->name);
	radeon_bo_reserve(rbo, false);
	radeon_bo_unpin(rbo);
	radeon_bo_unreserve(rbo);
	radeon_bo_unref(&rbo);
}

static int radeon_vcrtcm_push(struct drm_crtc *scrtc,
			      struct drm_gem_object *dbuf_fb,
			      struct drm_gem_object *dbuf_cursor)
{
	struct radeon_device *rdev = scrtc->dev->dev_private;
	struct radeon_fence *fence_c, *fence_fb;
	struct radeon_crtc *srcrtc = to_radeon_crtc(scrtc);
	struct drm_framebuffer *sfb = srcrtc->vcrtcm_push_fb;
	struct drm_gem_object *scbo = srcrtc->cursor_bo;
	struct push_vblank_pending *push_vblank_pending = NULL;
	struct drm_gem_object *sfbbo;
	struct radeon_framebuffer *srfb;
	struct radeon_bo *src_rbo, *dst_rbo;
	uint64_t saddr, daddr;
	unsigned num_pages, size_in_bytes;
	int r;

	/* bail out if we don't have the frame buffer */
	if (!sfb)
		return -ENOENT;
	srfb = to_radeon_framebuffer(sfb);
	sfbbo = srfb->obj;

	r = radeon_fence_create(rdev, &fence_c,
				radeon_copy_dma_ring_index(rdev));
	if (r)
		return r;
	r = radeon_fence_create(rdev, &fence_fb,
				radeon_copy_dma_ring_index(rdev));
	if (r) {
		radeon_fence_unref(&fence_c);
		return r;
	}

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
			radeon_copy(rdev, saddr, daddr, num_pages, fence_c);
		}
	}
	/* if we are dealing with a virtual CRTC, we'll need to emulate */
	/* vblank, so we need a pending vblank queue element */
	if ((srcrtc->crtc_id >= rdev->num_crtc) && radeon_vbl_emu_async) {
		push_vblank_pending =
			kmalloc(sizeof(struct push_vblank_pending), GFP_KERNEL);
		if (!push_vblank_pending)
			return -ENOMEM;
		push_vblank_pending->radeon_fence = fence_fb;
		push_vblank_pending->vblank_sent = 0;
		push_vblank_pending->radeon_crtc = srcrtc;
		push_vblank_pending->start_jiffies = jiffies;
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
	radeon_copy(rdev, saddr, daddr, num_pages, fence_fb);

	if (radeon_vbl_emu_async) {
		if (srcrtc->crtc_id >= rdev->num_crtc) {
			unsigned long flags;
			BUG_ON(!fence_fb);
			BUG_ON(!push_vblank_pending);
			/* we don't wait for fence here, but we put it in */
			/* a queue; when the fence is signaled, the queue */
			/* is checked and if the fence is there, vblank */
			/* emulation is called; not used for physical crtcs */
			spin_lock_irqsave(&rdev->vbl_emu_drv.pending_queue_lock,
					  flags);
			list_add_tail(&push_vblank_pending->list,
				      &rdev->vbl_emu_drv.pending_queue);
			spin_unlock_irqrestore(&rdev->vbl_emu_drv.pending_queue_lock,
					       flags);
		}
	} else {
		/* if we are not using asyncrhonous vblank emulation */
		/* then we have no choice but to "speculatively" */
		/* emulate the vblank here; copy has probably not */
		/* completed yet, but it is still safe to signal the */
		/* vblank because any subsequent rendering will */
		/* be pipelined into the GPU's queue after the copy */
		/* and will thus happen after the copy completes */
		/* there should be no frame tearing and things should */
		/* still work; n.b.: since we don't add anything */
		/* to vbl_emu_drv.pending_queue, nobody will look */
		/* at it, ISR is safe without checking the */
		/* radeon_vbl_emu_async parameter */
		if (srcrtc->vcrtcm_pcon_info)
			vcrtcm_gpu_set_vblank_time(srcrtc->vcrtcm_pcon_info);
		radeon_emulate_vblank(scrtc);
		radeon_fence_unref(&fence_c);
		radeon_fence_unref(&fence_fb);
	}
	return 0;
}

static void radeon_vcrtcm_hotplug(struct drm_crtc *crtc)
{
	struct radeon_device *rdev =
		(struct radeon_device *)crtc->dev->dev_private;
	schedule_work(&rdev->hotplug_work);
}

struct vcrtcm_gpu_funcs physical_crtc_gpu_funcs = {
	.detach = radeon_detach_callback,
	.vblank = NULL, /* no vblank emulation for real CRTC */
	.sync = radeon_sync_callback,
	.pb_alloc = radeon_vcrtcm_push_buffer_alloc,
	.pb_free = radeon_vcrtcm_push_buffer_free,
	.push = radeon_vcrtcm_push,
	.hotplug = NULL /* real CRTC has its own hotplug */
};

struct vcrtcm_gpu_funcs virtual_crtc_gpu_funcs = {
	.detach = radeon_detach_callback,
	.vblank = radeon_emulate_vblank,
	.sync = radeon_sync_callback,
	.pb_alloc = radeon_vcrtcm_push_buffer_alloc,
	.pb_free = radeon_vcrtcm_push_buffer_free,
	.push = radeon_vcrtcm_push,
	.hotplug = radeon_vcrtcm_hotplug
};

static int radeon_vcrtcm_gpu_attach(struct radeon_crtc *radeon_crtc, int major,
				int minor, int flow)
{
	int r;
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct radeon_device *rdev =
	    (struct radeon_device *)crtc->dev->dev_private;

	if (radeon_crtc->vcrtcm_pcon_info)
		return -EBUSY;

	if (radeon_crtc->crtc_id < rdev->num_crtc)
		r = vcrtcm_gpu_attach(major, minor, flow, crtc,
				  &physical_crtc_gpu_funcs,
				  &radeon_crtc->vcrtcm_pcon_info);
	else
		r = vcrtcm_gpu_attach(major, minor, flow, crtc,
				  &virtual_crtc_gpu_funcs,
				  &radeon_crtc->vcrtcm_pcon_info);

	if (r)
		return r;

	/* if the attach was successful, we also need to send the crtc */
	/* FB address and geometry to VCRTCM module. This is because */
	/* the attach can happed after the displays have been created */
	/* (e.g. X has already started); the application in that case */
	/* won't call set_base helper function and VCRTCM module won't */
	/* have the FB information. So we do it here (we use */
	/* atomic==1 because if this CRTC has the frame buffer */
	/* bound, it should presumably be already pinned */
	if (crtc->fb) {
		DRM_INFO("radeon_vcrtcm_gpu_attach: frame buffer exists\n");
		r = radeon_virtual_crtc_do_set_base(crtc, crtc->fb, crtc->x,
						    crtc->y, 1);
		if (r) {
			vcrtcm_gpu_detach(radeon_crtc->vcrtcm_pcon_info);
			return r;
		}
		/* we also need to set the cursor */
		if (radeon_crtc->cursor_bo) {
			struct vcrtcm_cursor vcrtcm_cursor;
			struct radeon_device *rdev =
				(struct radeon_device *)crtc->dev->dev_private;
			struct radeon_bo *rbo;
			uint64_t cursor_gpuaddr;

			DRM_INFO("radeon_vcrtcm_gpu_attach: cursor exists w=%d, h=%d\n",
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
				vcrtcm_gpu_detach(radeon_crtc->vcrtcm_pcon_info);
				return r;
			}
			cursor_gpuaddr = radeon_bo_gpu_offset(rbo);
			radeon_bo_unreserve(rbo);

			vcrtcm_cursor.ioaddr =
				rdev->mc.aper_base + (cursor_gpuaddr - rdev->mc.vram_start);
			DRM_INFO("radeon_vcrtcm_gpu_attach: cursor i/o address 0x%08x\n",
				 vcrtcm_cursor.ioaddr);
			r = vcrtcm_gpu_set_cursor(radeon_crtc->vcrtcm_pcon_info,
					      &vcrtcm_cursor);
			if (r) {
				vcrtcm_gpu_detach(radeon_crtc->vcrtcm_pcon_info);
				return r;
			}
		}
	}
	/* and we need to update the DPMS state */
	if (radeon_crtc->enabled)
		vcrtcm_gpu_set_dpms(radeon_crtc->vcrtcm_pcon_info,
				VCRTCM_DPMS_STATE_ON);
	else
		vcrtcm_gpu_set_dpms(radeon_crtc->vcrtcm_pcon_info,
				VCRTCM_DPMS_STATE_OFF);

	if (radeon_crtc->crtc_id >= rdev->num_crtc)
		schedule_work(&rdev->hotplug_work);

	return r;

}

int radeon_vcrtcm_detach(struct radeon_crtc *radeon_crtc)
{
	struct drm_crtc *crtc = &radeon_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int r = -EINVAL;

	if (radeon_crtc->vcrtcm_pcon_info) {
		r = vcrtcm_gpu_detach(radeon_crtc->vcrtcm_pcon_info);
		if (radeon_crtc->crtc_id >= rdev->num_crtc)
			schedule_work(&rdev->hotplug_work);
	}
	return r;
}

static int radeon_vcrtcm_force(struct radeon_crtc *radeon_crtc)
{
	if (radeon_crtc->vcrtcm_pcon_info)
		return vcrtcm_gpu_xmit_fb(radeon_crtc->vcrtcm_pcon_info);
	else
		return -EINVAL;
}

static struct radeon_crtc
*display_index_to_radeon_crtc(struct radeon_device *rdev, int display_index)
{
	struct radeon_crtc *radeon_crtc;
	struct virtual_crtc *virtual_crtc;

	radeon_crtc = NULL;

	if (display_index < rdev->num_crtc) {
		radeon_crtc = rdev->mode_info.crtcs[display_index];
	}

	list_for_each_entry(virtual_crtc, &rdev->mode_info.virtual_crtcs, list) {
		if (virtual_crtc->radeon_crtc->crtc_id == display_index) {
			radeon_crtc = virtual_crtc->radeon_crtc;
			break;
		}
	}
	return radeon_crtc;
}

static inline int radeon_vcrtcm_gpu_set_fps(struct radeon_crtc *radeon_crtc,
					int fps)
{
	if (radeon_crtc->vcrtcm_pcon_info)
		return vcrtcm_gpu_set_fps(radeon_crtc->vcrtcm_pcon_info, fps);
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

	radeon_mutex_lock(&rdev->cs_mutex);
	switch (op_code) {

	case RADEON_VCRTCM_CTL_OP_CODE_NOP:
		DRM_DEBUG("nop\n");
		r = 0;
		break;

	case RADEON_VCRTCM_CTL_OP_CODE_SET_RATE:
		DRM_DEBUG("set fps\n");
		r = radeon_vcrtcm_gpu_set_fps(radeon_crtc,
					  rvcd->arg1.fps);
		break;

	case RADEON_VCRTCM_CTL_OP_CODE_ATTACH:
		DRM_DEBUG("attach\n");
		r = radeon_vcrtcm_gpu_attach(radeon_crtc,
					 rvcd->arg1.major,
					 rvcd->arg2.minor,
					 rvcd->arg3.flow);
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
	radeon_mutex_unlock(&rdev->cs_mutex);
	return r;
}
