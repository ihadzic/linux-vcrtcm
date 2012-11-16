/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Authors: Hans Christian Woithe <hans.woithe@alcatel-lucent.com>
		Bill Katsak <william.katsak@alcatel-lucent.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include "v4l2pim.h"
#include "v4l2pim_vcrtcm.h"

static void v4l2pim_free_pb(struct v4l2pim_pcon *pcon, int flag)
{
	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;
	int i;

	for (i = 0; i < 2; i++) {
		if (flag == V4L2PIM_ALLOC_PB_FLAG_FB) {
			pbd = pcon->pbd_fb[i];
			pb_mapped_ram = pcon->pb_fb[i];
			pcon->pbd_fb[i] = NULL;
		} else {
			pbd = pcon->pbd_cursor[i];
			pb_mapped_ram = pcon->pb_cursor[i];
			pcon->pbd_cursor[i] = NULL;
		}
		if (pbd) {
			BUG_ON(!pbd->gpu_private);
			BUG_ON(!pb_mapped_ram);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_p_free_pb(pcon->pconid, pbd);
		}
	}
}

static int v4l2pim_alloc_pb(struct v4l2pim_pcon *pcon,
		int num_pages, int flag)
{
	int i;
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd, *old_pbd = NULL;
	void *pb, *old_pb = NULL;

	for (i = 0; i < 2; i++) {
		pbd = vcrtcm_p_alloc_pb(pcon->pconid, num_pages,
					GFP_KERNEL | __GFP_HIGHMEM);
		if (IS_ERR(pbd)) {
			r = PTR_ERR(pbd);
			goto out_err0;
		}
		pb = vm_map_ram(pbd->pages, num_pages, 0, PAGE_KERNEL);
		if (pb == NULL) {
			VCRTCM_ERROR("%s[%d]: vm_map_ram failed\n",
				V4L2PIM_ALLOC_PB_STRING(flag), i);
			r = -ENOMEM;
			goto out_err1;
		}
		if (flag == V4L2PIM_ALLOC_PB_FLAG_FB) {
			pcon->pbd_fb[i] = pbd;
			pcon->pb_fb[i] = pb;
		} else {
			pcon->pbd_cursor[i] = pbd;
			pcon->pb_cursor[i] = pb;
		}
		pcon->pb_needs_xmit[i] = 0;
		old_pbd = pbd;
		old_pb = pb;
	}
	if (!r)
		return r;

out_err1:
	vcrtcm_p_free_pb(pcon->pconid, pbd);
out_err0:
	if (i == 1) {
		/*
		 * allocation failed in the second iteration
		 * of the loop, we must release the buffer
		 * allocated in the first one before returning
		 */
		BUG_ON(!old_pbd || !old_pb);
		vm_unmap_ram(old_pbd->pages, num_pages);
		if (flag == V4L2PIM_ALLOC_PB_FLAG_FB) {
			pcon->pbd_fb[0] = NULL;
			pcon->pb_fb[0] = NULL;
		} else {
			pcon->pbd_cursor[0] = NULL;
			pcon->pb_cursor[0] = NULL;
		}
		vcrtcm_p_free_pb(pcon->pconid, old_pbd);
	}
	return r;
}

int v4l2pim_attach(int pconid, void *cookie)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	VCRTCM_INFO("Attaching vl2pcon %d to pcon %d\n",
		minor->minor, pconid);

	pcon->attached = 1;
	return 0;
}

int v4l2pim_detach_pcon(struct v4l2pim_pcon *pcon)
{
	struct v4l2pim_minor *minor;

	minor = pcon->minor;
	if (atomic_read(&minor->users) > 0) {
		VCRTCM_INFO("cannot detach pcon %d, some process has minor %d open",
			pcon->pconid, minor->minor);
		return -EBUSY;
	}
	if (pcon->attached)
		VCRTCM_INFO("detaching pcon %d\n", pcon->pconid);
	v4l2pim_free_pb(pcon, V4L2PIM_ALLOC_PB_FLAG_FB);
	v4l2pim_free_pb(pcon, V4L2PIM_ALLOC_PB_FLAG_CURSOR);
	pcon->attached = 0;
	return 0;
}

int v4l2pim_detach(int pconid, void *cookie)
{
	struct v4l2pim_pcon *pcon = cookie;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	return v4l2pim_detach_pcon(pcon);
}

static int v4l2pim_realloc_pb(struct v4l2pim_pcon *pcon,
			      int size, int flag)
{
	int num_pages, r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd0, *pbd1;
	int need_shadow_buf = 0;
	struct v4l2pim_minor *minor = pcon->minor;

	if (flag ==  V4L2PIM_ALLOC_PB_FLAG_FB) {
		pbd0 = pcon->pbd_fb[0];
		pbd1 = pcon->pbd_fb[1];
	} else {
		pbd0 = pcon->pbd_cursor[0];
		pbd1 = pcon->pbd_cursor[1];
	}

	num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE)
		num_pages++;

	if (!num_pages) {
		V4L2PIM_DEBUG("%s: zero size requested\n",
			      V4L2PIM_ALLOC_PB_STRING(flag));
		v4l2pim_free_pb(pcon, flag);
	} else if (!pbd0) {
		/* no old buffer present */
		BUG_ON(pbd1);
		r = v4l2pim_alloc_pb(pcon, num_pages, flag);
		need_shadow_buf = (flag == V4L2PIM_ALLOC_PB_FLAG_FB) ? 1 : 0;
	} else if (pbd0->num_pages == num_pages) {
		V4L2PIM_DEBUG("%s: reusing existing push buffer\n",
			      V4L2PIM_ALLOC_PB_STRING(flag));
		BUG_ON(pbd0->num_pages != pbd1->num_pages);
	} else {
		/* size changed */
		V4L2PIM_DEBUG("%s: allocating push buffer "
			      "size=%d, num_pages=%d\n",
			      V4L2PIM_ALLOC_PB_STRING(flag), size, num_pages);
		BUG_ON(pbd0->num_pages != pbd1->num_pages);
		v4l2pim_free_pb(pcon, flag);
		r = v4l2pim_alloc_pb(pcon, num_pages, flag);
		need_shadow_buf = (flag == V4L2PIM_ALLOC_PB_FLAG_FB) ? 1 : 0;
	}
	if (need_shadow_buf) {
		uint32_t w, h, sb_size;

		/* this should get freed later */
		w = pcon->vcrtcm_fb.hdisplay;
		h = pcon->vcrtcm_fb.vdisplay;
		sb_size = w * h * (pcon->vcrtcm_fb.bpp >> 3);
		mutex_lock(&minor->sb_lock);
		v4l2pim_alloc_shadowbuf(minor, sb_size);
		mutex_unlock(&minor->sb_lock);
	}
	return r;
}

int v4l2pim_set_fb(int pconid, void *cookie,
		   struct vcrtcm_fb *vcrtcm_fb)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;
	int r = 0;
	int size;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("minor %d.\n", minor->minor);
	mutex_lock(&minor->buffer_mutex);
	memcpy(&pcon->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));
	size = pcon->vcrtcm_fb.pitch * pcon->vcrtcm_fb.vdisplay;
	r = v4l2pim_realloc_pb(pcon, size,
			       V4L2PIM_ALLOC_PB_FLAG_FB);
	pcon->fb_xmit_allowed = 1;
	mutex_unlock(&minor->buffer_mutex);
	return r;
}

int v4l2pim_get_fb(int pconid, void *cookie,
		   struct vcrtcm_fb *vcrtcm_fb)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("minor %d\n", minor->minor);
	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&minor->buffer_mutex);
	memcpy(vcrtcm_fb, &pcon->vcrtcm_fb, sizeof(struct vcrtcm_fb));
	mutex_unlock(&minor->buffer_mutex);
	return 0;
}

int v4l2pim_dirty_fb(int pconid, void *cookie,
		     struct drm_crtc *drm_crtc)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("minor %d\n", minor->minor);

	/* just mark the "force" flag, v4l2pim_do_xmit_fb_pull
	 * does the rest (when called).
	 */
	pcon->fb_dirty = 1;
	return 0;
}

int v4l2pim_wait_fb(int pconid, void *cookie,
		    struct drm_crtc *drm_crtc)
{
	return 0;
}

int v4l2pim_get_fb_status(int pconid, void *cookie,
			  struct drm_crtc *drm_crtc, u32 *status)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	unsigned long flags;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("\n");

	spin_lock_irqsave(&minor->lock, flags);
	if (minor->status & V4L2PIM_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&minor->lock, flags);

	*status = tmp_status;

	return 0;
}

int v4l2pim_set_fps(int pconid, void *cookie, int fps)
{
	struct v4l2pim_pcon *pcon = cookie;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	if (fps > 0) {
		pcon->last_xmit_jiffies = jiffies;
		pcon->fb_dirty = 1;
	}
	pcon->fps = fps;
	return 0;
}

int v4l2pim_set_cursor(int pconid, void *cookie,
		       struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;
	int r = 0;
	int size;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("minor %d\n", minor->minor);
	mutex_lock(&minor->buffer_mutex);
	memcpy(&pcon->vcrtcm_cursor, vcrtcm_cursor,
			sizeof(struct vcrtcm_cursor));
	size = pcon->vcrtcm_cursor.height *
		pcon->vcrtcm_cursor.width *
		(pcon->vcrtcm_cursor.bpp >> 3);
	r = v4l2pim_realloc_pb(pcon, size,
			       V4L2PIM_ALLOC_PB_FLAG_CURSOR);
	mutex_unlock(&minor->buffer_mutex);
	return r;
}

int v4l2pim_get_cursor(int pconid, void *cookie,
		       struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("minor %d\n", minor->minor);
	mutex_lock(&minor->buffer_mutex);
	memcpy(vcrtcm_cursor, &pcon->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));
	mutex_unlock(&minor->buffer_mutex);
	return 0;
}

int v4l2pim_set_dpms(int pconid, void *cookie, int state)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("minor %d, state %d\n", minor->minor, state);
	pcon->dpms_state = state;
	return 0;
}

int v4l2pim_get_dpms(int pconid, void *cookie, int *state)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	V4L2PIM_DEBUG("minor %d\n", minor->minor);
	*state = pcon->dpms_state;
	return 0;
}

void v4l2pim_disable(int pconid, void *cookie)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return;
	}
	minor = pcon->minor;
	mutex_lock(&minor->buffer_mutex);
	pcon->fb_xmit_allowed = 0;
	mutex_unlock(&minor->buffer_mutex);
}

int v4l2pim_vblank(int pconid, void *cookie)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	mutex_lock(&minor->buffer_mutex);
	V4L2PIM_DEBUG("in v4l2pim_do_xmit_fb_push, minor %d\n",
		      minor->minor);

	spin_lock_irqsave(&minor->lock, flags);
	minor->status |= V4L2PIM_IN_DO_XMIT;
	spin_unlock_irqrestore(&minor->lock, flags);

	push_buffer_index = pcon->push_buffer_index;

	if (pcon->pbd_fb) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		VCRTCM_WARNING("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((pcon->fb_dirty ||
	     time_after(jiffies_snapshot, pcon->last_xmit_jiffies +
			V4L2PIM_XMIT_HARD_DEADLINE)) && have_push_buffer &&
	    pcon->fb_xmit_allowed) {
		/* someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 */

		V4L2PIM_DEBUG("transmission happening...\n");
		pcon->fb_dirty = 0;
		pcon->fb_xmit_counter++;

		V4L2PIM_DEBUG("v4l2pim_do_xmit_fb_push[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				pcon->vcrtcm_fb.pitch,
				pcon->vcrtcm_fb.width,
				pcon->vcrtcm_fb.height,
				pcon->vcrtcm_fb.bpp);

		V4L2PIM_DEBUG("v4l2pim_do_xmit_fb_push[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				pcon->vcrtcm_fb.viewport_x,
				pcon->vcrtcm_fb.viewport_y,
				pcon->vcrtcm_fb.hdisplay,
				pcon->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&minor->lock, flags);
		minor->status &= ~V4L2PIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&minor->lock, flags);

		r = vcrtcm_p_push(pcon->pconid,
				  pcon->pbd_fb[push_buffer_index],
				  pcon->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_p_emulate_vblank(pcon->pconid);
		} else {
			/* if push successed, then we need to swap push buffers
			 * and mark the buffer for transmission in the next
			 * vblank interval; note that call to vcrtcm_p_push only
			 * initiates the push request to GPU; when GPU does it
			 * is up to the GPU and doesn't matter as long as it is
			 * within the frame transmission period (otherwise, we'll
			 * see from frame tearing)
			 * If GPU completes the push before the next vblank
			 * interval, then it is perfectly safe to mark the buffer
			 * ready for transmission now because transmission wont
			 * look at it until push is complete.
			 */

			pcon->last_xmit_jiffies = jiffies;
			pcon->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			pcon->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		spin_lock_irqsave(&minor->lock, flags);
		minor->status &= ~V4L2PIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&minor->lock, flags);

		vcrtcm_p_emulate_vblank(pcon->pconid);
		V4L2PIM_DEBUG("transmission not happening\n");
	}

	if ((pcon->pb_needs_xmit[push_buffer_index]) &&
	    (!pcon->pbd_fb[push_buffer_index]->virgin)) {
		struct vcrtcm_cursor *cursor;
		unsigned int hpixels, vpixels;
		unsigned int vp_offset, hlen, p, vpx, vpy, Bpp;
		unsigned int i, j;
		char *mb, *sb;

		hpixels = pcon->vcrtcm_fb.hdisplay;
		vpixels = pcon->vcrtcm_fb.vdisplay;
		p = pcon->vcrtcm_fb.pitch;
		vpx = pcon->vcrtcm_fb.viewport_x;
		vpy = pcon->vcrtcm_fb.viewport_y;
		Bpp = pcon->vcrtcm_fb.bpp >> 3;
		vp_offset = p * vpy + vpx * Bpp;
		hlen = hpixels * Bpp;

		minor->main_buffer = pcon->pb_fb[push_buffer_index];
		minor->cursor = pcon->pb_cursor[push_buffer_index];

		/* Overlay the cursor on the framebuffer */
		cursor = &pcon->vcrtcm_cursor;
		if ((cursor->flag != VCRTCM_CURSOR_FLAG_HIDE) &&
		    (!pcon->pbd_cursor[push_buffer_index]->virgin)) {
			uint32_t *fb_end;
			int clip_y = 0;
			mb = minor->main_buffer + vp_offset;
			fb_end = (uint32_t *) (mb + p * (vpixels - 1));
			fb_end += hpixels;
			if (cursor->location_y < 0)
				clip_y = -cursor->location_y;
			/* loop for each line of the framebuffer. */
			for (i = clip_y; i < cursor->height; i++) {
				uint32_t *cursor_pixel;
				uint32_t *fb_pixel;
				uint32_t *fb_line_end;
				int clip_x = 0;

				if (cursor->location_x < 0)
					clip_x = -cursor->location_x;
				cursor_pixel = (uint32_t *) minor->cursor;
				cursor_pixel += i * cursor->width;
				cursor_pixel += clip_x;

				fb_pixel = (uint32_t *) (mb + p * (cursor->location_y + i));
				fb_line_end = fb_pixel + hpixels;
				fb_pixel += cursor->location_x + clip_x;

				for (j = clip_x; j < cursor->width; j++) {
					if (fb_pixel >= fb_end || fb_pixel >= fb_line_end)
						break;

					if (*cursor_pixel >> 24 > 0)
						*fb_pixel = *cursor_pixel;

					cursor_pixel++;
					fb_pixel++;
				}
			}
		}

		V4L2PIM_DEBUG("v4l2pim_do_xmit_fb_push[%d]: initiating copy\n",
				push_buffer_index);
		jiffies_snapshot = jiffies;
		mutex_lock(&minor->sb_lock);
		if (minor->shadowbuf) {
			minor->jshadowbuf = jiffies;
			mb = minor->main_buffer + vp_offset;
			sb = minor->shadowbuf;
			for (i = 0; i < vpixels; i++) {
				memcpy(sb, mb, hlen);
				mb += p;
				sb += hlen;
			}
		}
		mutex_unlock(&minor->sb_lock);
		V4L2PIM_DEBUG("copy took %u ms\n",
			      jiffies_to_msecs(jiffies - jiffies_snapshot));
		pcon->pb_needs_xmit[push_buffer_index] = 0;
	}

	mutex_unlock(&minor->buffer_mutex);
	return r;
}

static struct vcrtcm_pcon_funcs v4l2pim_vcrtcm_pcon_funcs = {
	.attach = v4l2pim_attach,
	.detach = v4l2pim_detach,
	.set_fb = v4l2pim_set_fb,
	.get_fb = v4l2pim_get_fb,
	.dirty_fb = v4l2pim_dirty_fb,
	.wait_fb = v4l2pim_wait_fb,
	.get_fb_status = v4l2pim_get_fb_status,
	.set_fps = v4l2pim_set_fps,
	.set_cursor = v4l2pim_set_cursor,
	.get_cursor = v4l2pim_get_cursor,
	.set_dpms = v4l2pim_set_dpms,
	.get_dpms = v4l2pim_get_dpms,
	.disable = v4l2pim_disable,
	.vblank = v4l2pim_vblank,
};

struct v4l2pim_pcon *v4l2pim_create_pcon(int pconid, struct v4l2pim_minor *minor)
{
	struct v4l2pim_pcon *pcon;

	pcon = vcrtcm_kzalloc(sizeof(struct v4l2pim_pcon),
			GFP_KERNEL, pconid);
	if (pcon == NULL)
		return NULL;
	pcon->pconid = pconid;
	pcon->minor = minor;
	pcon->vcrtcm_cursor.flag = VCRTCM_CURSOR_FLAG_HIDE;
	return pcon;
}

void v4l2pim_destroy_pcon(struct v4l2pim_pcon *pcon)
{
	VCRTCM_INFO("waiting for push completion on pcon %d\n", pcon->pconid);
	vcrtcm_p_wait_fb(pcon->pconid);
	VCRTCM_INFO("destroying pcon %d\n", pcon->pconid);
	pcon->minor->pcon = NULL;
	vcrtcm_kfree(pcon);
}

int v4l2pim_instantiate(int pconid, uint32_t hints,
	void **cookie, struct vcrtcm_pcon_funcs *funcs,
	enum vcrtcm_xfer_mode *xfer_mode, int *minornum,
	int *vblank_slack, char *description)
{
	struct v4l2pim_minor *minor;

	minor = v4l2pim_create_minor();
	if (!minor)
		return -ENODEV;
	minor->pcon = v4l2pim_create_pcon(pconid, minor);
	if (!minor->pcon) {
		v4l2pim_destroy_minor(minor);
		return -ENOMEM;
	}
	scnprintf(description, PCON_DESC_MAXLEN,
			"Video4Linux2 PCON - minor %i", minor->minor);
	*minornum = minor->minor;
	*funcs = v4l2pim_vcrtcm_pcon_funcs;
	*xfer_mode = VCRTCM_PUSH_PULL;
	*cookie = minor->pcon;
	*vblank_slack = v4l2pim_fake_vblank_slack;
	VCRTCM_INFO("v4l2pim %d now serves pcon %d\n",
			minor->minor, pconid);
	return 0;
}

void v4l2pim_destroy(int pconid, void *cookie)
{
	struct v4l2pim_pcon *pcon = cookie;
	struct v4l2pim_minor *minor;

	/* the pim destroy callback can assume that the pcon is detached */
	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return;
	}
	minor = pcon->minor;
	v4l2pim_destroy_pcon(pcon);
	v4l2pim_destroy_minor(minor);
}
