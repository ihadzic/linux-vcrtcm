/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Author: Bill Katsak <william.katsak@alcatel-lucent.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/delay.h>
#include <linux/prefetch.h>
#include "udlpim.h"
#include "udlpim_vcrtcm.h"

static void udlpim_free_pb(struct udlpim_pcon *pcon, int flag)
{
	int i;

	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;

	for (i = 0; i < 2; i++) {
		if (flag == UDLPIM_ALLOC_PB_FLAG_FB) {
			pbd = pcon->pbd_fb[i];
			pb_mapped_ram = pcon->pb_fb[i];
			pcon->pbd_fb[i] = NULL;
			pcon->pb_fb[i] = NULL;
		} else {
			pbd = pcon->pbd_cursor[i];
			pb_mapped_ram = pcon->pb_cursor[i];
			pcon->pbd_cursor[i] = NULL;
			pcon->pb_cursor[i] = NULL;
		}
		if (pbd) {
			BUG_ON(!pbd->gpu_private);
			BUG_ON(!pb_mapped_ram);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_p_free_pb(pcon->pconid, pbd);
		}
	}
}

struct udlpim_pcon *udlpim_cookie2pcon(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = cookie;

	if (pcon->magic != UDLPIM_PCON_GOOD_MAGIC) {
		VCRTCM_ERROR("bad magic (0x%08x) in cookie (0x%p) for pcon %d\n",
			pcon->magic, cookie, pconid);
		dump_stack();
		return NULL;
	}
	if (pcon->pconid != pconid) {
		VCRTCM_ERROR("mismatching pcon ids (%d vs. %d)\n",
			pconid, pcon->pconid);
		dump_stack();
		return NULL;
	}
	return pcon;
}

int udlpim_attach(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);

	if (!pcon)
		return -ENODEV;
	VCRTCM_INFO("attaching pcon %d\n", pconid);
	pcon->attached = 1;
	return 0;
}

void udlpim_detach_pcon(struct udlpim_pcon *pcon)
{
	udlpim_free_pb(pcon, UDLPIM_ALLOC_PB_FLAG_FB);
	udlpim_free_pb(pcon, UDLPIM_ALLOC_PB_FLAG_CURSOR);
	pcon->attached = 0;
	VCRTCM_INFO("detached pcon %d\n", pcon->pconid);
}

int udlpim_detach(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);

	if (!pcon)
		return -EINVAL;
	udlpim_detach_pcon(pcon);
	return 0;
}

static int udlpim_realloc_pb(struct udlpim_minor *minor,
			     struct udlpim_pcon *pcon,
			     int size, int flag)
{
	int num_pages, r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd0, *pbd1;
	void *pb_mapped_ram0, *pb_mapped_ram1;

	if (flag ==  UDLPIM_ALLOC_PB_FLAG_FB) {
		pbd0 = pcon->pbd_fb[0];
		pbd1 = pcon->pbd_fb[1];
		pb_mapped_ram0 = pcon->pb_fb[0];
		pb_mapped_ram1 = pcon->pb_fb[1];
	} else {
		pbd0 = pcon->pbd_cursor[0];
		pbd1 = pcon->pbd_cursor[1];
		pb_mapped_ram0 = pcon->pb_cursor[0];
		pb_mapped_ram1 = pcon->pb_cursor[1];
	}
	num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE)
		num_pages++;
	if (pb_mapped_ram0) {
		BUG_ON(!pbd0);
		vm_unmap_ram(pb_mapped_ram0, pbd0->num_pages);
		pb_mapped_ram0 = NULL;
	}
	if (pb_mapped_ram1) {
		BUG_ON(!pbd1);
		vm_unmap_ram(pb_mapped_ram1, pbd1->num_pages);
		pb_mapped_ram1 = NULL;
	}
	pbd0 = vcrtcm_p_realloc_pb(pcon->pconid, pbd0,
				   num_pages, GFP_KERNEL | __GFP_HIGHMEM);
	if (IS_ERR(pbd0)) {
		r = PTR_ERR(pbd0);
		goto out_err0;
	}
	pbd1 = vcrtcm_p_realloc_pb(pcon->pconid, pbd1,
				   num_pages, GFP_KERNEL | __GFP_HIGHMEM);
	if (IS_ERR(pbd1)) {
		r = PTR_ERR(pbd1);
		goto out_err1;
	}
	if (pbd0) {
		pb_mapped_ram0 =
			vm_map_ram(pbd0->pages, num_pages, 0, PAGE_KERNEL);
		if (!pb_mapped_ram0) {
			r = -ENOMEM;
			goto out_err2;
		}
	}
	if (pbd1) {
		pb_mapped_ram1 =
			vm_map_ram(pbd1->pages, num_pages, 0, PAGE_KERNEL);
		if (!pb_mapped_ram1) {
			r = -ENOMEM;
			goto out_err3;
		}
	}
	/*
	 * it is OK not to have the buffer at this point, but
	 * we must be consistent
	 */
	BUG_ON((pbd0 && !pbd1) || (!pbd0 && pbd1));
	BUG_ON((pb_mapped_ram0 && !pb_mapped_ram1) ||
	       (!pb_mapped_ram0 && pb_mapped_ram1));
	if (flag ==  UDLPIM_ALLOC_PB_FLAG_FB) {
		pcon->pbd_fb[0] = pbd0;
		pcon->pbd_fb[1] = pbd1;
		pcon->pb_fb[0] = pb_mapped_ram0;
		pcon->pb_fb[1] = pb_mapped_ram1;
	} else {
		pcon->pbd_cursor[0] = pbd0;
		pcon->pbd_cursor[1] = pbd1;
		pcon->pb_cursor[0] = pb_mapped_ram0;
		pcon->pb_cursor[1] = pb_mapped_ram1;
	}
	return 0;
out_err3:
	if (pbd0)
		vm_unmap_ram(pb_mapped_ram0, pbd0->num_pages);
out_err2:
	vcrtcm_p_free_pb(pcon->pconid, pbd1);
out_err1:
	vcrtcm_p_free_pb(pcon->pconid, pbd0);
out_err0:
	if (flag ==  UDLPIM_ALLOC_PB_FLAG_FB) {
		pcon->pbd_fb[0] = NULL;
		pcon->pbd_fb[1] = NULL;
		pcon->pb_fb[0] = NULL;
		pcon->pb_fb[1] = NULL;
	} else {
		pcon->pbd_cursor[0] = NULL;
		pcon->pbd_cursor[1] = NULL;
		pcon->pb_cursor[0] = NULL;
		pcon->pb_cursor[1] = NULL;
	}
	return r;
}

int udlpim_set_fb(int pconid, void *cookie,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int found_mode = 0;
	int r = 0;
	int i = 0;
	int size;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	mutex_lock(&minor->buffer_mutex);
	vcrtcm_p_wait_fb(pconid);
	memcpy(&pcon->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));

	udlpim_build_modelist(minor,
			&udlpim_video_modes, &udlpim_mode_count);

	/* Find a matching video mode and switch the DL device to that mode */
	for (i = 0; i < udlpim_mode_count; i++) {
		UDLPIM_DEBUG("checking %dx%d\n",
				udlpim_video_modes[i].xres,
				udlpim_video_modes[i].yres);
		UDLPIM_DEBUG("against %dx%d\n",
				vcrtcm_fb->hdisplay, vcrtcm_fb->vdisplay);
		if (udlpim_video_modes[i].xres == vcrtcm_fb->hdisplay &&
			udlpim_video_modes[i].yres == vcrtcm_fb->vdisplay) {
			/* If the modes match */
			minor->bpp = vcrtcm_fb->bpp;
			udlpim_setup_screen(minor,
					&udlpim_video_modes[i], vcrtcm_fb);
			found_mode = 1;
			pcon->fb_xmit_allowed = 1;
			break;
		}
	}

	udlpim_free_modelist(minor, udlpim_video_modes);

	if (!found_mode) {
		VCRTCM_ERROR("could not find matching mode...\n");
		pcon->fb_xmit_allowed = 0;
		udlpim_error_screen(minor);
		mutex_unlock(&minor->buffer_mutex);
		return 0;
	}

	size = pcon->vcrtcm_fb.pitch *
		pcon->vcrtcm_fb.vdisplay;
	r = udlpim_realloc_pb(minor, pcon, size,
			      UDLPIM_ALLOC_PB_FLAG_FB);
	mutex_unlock(&minor->buffer_mutex);
	return r;
}

int udlpim_get_fb(int pconid, void *cookie,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	mutex_lock(&minor->buffer_mutex);
	memcpy(vcrtcm_fb, &pcon->vcrtcm_fb, sizeof(struct vcrtcm_fb));
	mutex_unlock(&minor->buffer_mutex);
	return 0;
}

int udlpim_dirty_fb(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	/* just mark the "force" flag, udlpim_do_xmit_fb_pull
	 * does the rest (when called).
	 */
	pcon->fb_dirty = 1;
	return 0;
}

int udlpim_wait_fb(int pconid, void *cookie)
{
	return 0;
}

int udlpim_get_fb_status(int pconid, void *cookie, u32 *status)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	unsigned long flags;

	UDLPIM_DEBUG("\n");
	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	spin_lock_irqsave(&minor->lock, flags);
	if (minor->status & UDLPIM_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&minor->lock, flags);
	*status = tmp_status;
	return 0;
}


int udlpim_set_fps(int pconid, void *cookie, int fps)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);

	if (!pcon)
		return -EINVAL;
	if (fps > 0) {
		pcon->last_xmit_jiffies = jiffies;
		pcon->fb_dirty = 1;
	}
	return 0;
}

int udlpim_set_cursor(int pconid, void *cookie,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;
	int r = 0;
	int size;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	mutex_lock(&minor->buffer_mutex);
	vcrtcm_p_wait_fb(pconid);
	memcpy(&pcon->vcrtcm_cursor, vcrtcm_cursor,
			sizeof(struct vcrtcm_cursor));
	size = pcon->vcrtcm_cursor.height *
		pcon->vcrtcm_cursor.width *
		(pcon->vcrtcm_cursor.bpp >> 3);
	r = udlpim_realloc_pb(minor, pcon, size,
			      UDLPIM_ALLOC_PB_FLAG_CURSOR);
	mutex_unlock(&minor->buffer_mutex);
	return r;
}

int udlpim_get_cursor(int pconid, void *cookie,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	mutex_lock(&minor->buffer_mutex);
	memcpy(vcrtcm_cursor, &pcon->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));
	mutex_unlock(&minor->buffer_mutex);
	return 0;
}

int udlpim_set_dpms(int pconid, void *cookie, int state)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d, state %d\n", minor->minor, state);

	pcon->dpms_state = state;

	if (state == VCRTCM_DPMS_STATE_ON) {
		udlpim_dpms_wakeup(minor);
	} else if (state == VCRTCM_DPMS_STATE_OFF) {
		udlpim_dpms_sleep(minor);
	}

	return 0;
}

int udlpim_get_dpms(int pconid, void *cookie, int *state)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	*state = pcon->dpms_state;

	return 0;
}

int udlpim_connected(int pconid, void *cookie, int *status)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	UDLPIM_DEBUG("");
	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	if (minor->monitor_connected) {
		UDLPIM_DEBUG("...connected\n");
		*status = VCRTCM_PCON_CONNECTED;
	} else {
		UDLPIM_DEBUG("...not connected\n");
		*status = VCRTCM_PCON_DISCONNECTED;
	}
	return 0;
}

int udlpim_get_modes(int pconid, void *cookie,
		     struct vcrtcm_mode **modes, int *count)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;
	struct udlpim_video_mode *udlpim_video_modes;
	struct vcrtcm_mode *vcrtcm_mode_list;
	int udlpim_mode_count = 0;
	int vcrtcm_mode_count = 0;
	int retval = 0;
	int i = 0;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	vcrtcm_mode_list = minor->last_vcrtcm_mode_list;
	*modes = NULL;
	*count = 0;

	UDLPIM_DEBUG("\n");

	retval = udlpim_build_modelist(minor,
			&udlpim_video_modes, &udlpim_mode_count);

	if (retval < 0)
		return retval;

	if (udlpim_mode_count == 0)
		return 0;

	/* If we get this far, we can return modes. */

	/* Erase our old VCRTCM modelist. */
	if (vcrtcm_mode_list) {
		vcrtcm_kfree(vcrtcm_mode_list);
		vcrtcm_mode_list = NULL;
		vcrtcm_mode_count = 0;
	}

	/* Build the new vcrtcm_mode list. */
	vcrtcm_mode_list =
		vcrtcm_kmalloc(sizeof(struct vcrtcm_mode) * udlpim_mode_count,
			GFP_KERNEL, pconid);

	/* Copy the udlpim_video_mode list to the vcrtcm_mode list. */
	for (i = 0; i < udlpim_mode_count; i++) {
		vcrtcm_mode_list[i].w = udlpim_video_modes[i].xres;
		vcrtcm_mode_list[i].h = udlpim_video_modes[i].yres;
		vcrtcm_mode_list[i].refresh = udlpim_video_modes[i].refresh;
	}

	vcrtcm_mode_count = udlpim_mode_count;
	minor->last_vcrtcm_mode_list = vcrtcm_mode_list;

	*modes = vcrtcm_mode_list;
	*count = vcrtcm_mode_count;

	udlpim_free_modelist(minor, udlpim_video_modes);

	return 0;
}

int udlpim_check_mode(int pconid, void *cookie,
		      struct vcrtcm_mode *mode, int *status)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int retval;
	int i;

	UDLPIM_DEBUG("\n");
	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;

	*status = VCRTCM_MODE_BAD;

	retval = udlpim_build_modelist(minor,
			&udlpim_video_modes, &udlpim_mode_count);

	if (retval < 0)
		return retval;

	if (udlpim_mode_count == 0)
		return 0;

	UDLPIM_DEBUG("checking %dx%d@%d\n",
			mode->w, mode->h, mode->refresh);
	for (i = 0; i < udlpim_mode_count; i++) {
		struct udlpim_video_mode *current_mode;
		current_mode = &udlpim_video_modes[i];
		if (current_mode->xres == mode->w &&
			current_mode->yres == mode->h &&
			current_mode->refresh == mode->refresh) {
			*status = VCRTCM_MODE_OK;
			break;
		}
	}

	udlpim_free_modelist(minor, udlpim_video_modes);

	return 0;
}

void udlpim_disable(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	if (!pcon)
		return;
	minor = pcon->minor;
	mutex_lock(&minor->buffer_mutex);
	pcon->fb_xmit_allowed = 0;
	mutex_unlock(&minor->buffer_mutex);
}

int udlpim_vblank(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	if (!pcon)
		return -EINVAL;
	minor = pcon->minor;
	mutex_lock(&minor->buffer_mutex);
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	spin_lock_irqsave(&minor->lock, flags);
	minor->status |= UDLPIM_IN_DO_XMIT;
	spin_unlock_irqrestore(&minor->lock, flags);

	push_buffer_index = pcon->push_buffer_index;

	if (pcon->pbd_fb[push_buffer_index]) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		UDLPIM_DEBUG("no push buffer[%d], transmission skipped\n",
			     push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((pcon->fb_dirty ||
	     time_after(jiffies_snapshot, pcon->last_xmit_jiffies +
			UDLPIM_XMIT_HARD_DEADLINE)) &&
			have_push_buffer &&
			minor->monitor_connected &&
			pcon->fb_xmit_allowed) {
		/* Someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 * We also check to see if we have a monitor connected, and if
		 * we are allowed to transmit.
		 */

		UDLPIM_DEBUG("transmission happening...\n");
		pcon->fb_dirty = 0;
		pcon->fb_xmit_counter++;

		UDLPIM_DEBUG("[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				pcon->vcrtcm_fb.pitch,
				pcon->vcrtcm_fb.width,
				pcon->vcrtcm_fb.height,
				pcon->vcrtcm_fb.bpp);

		UDLPIM_DEBUG("[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				pcon->vcrtcm_fb.viewport_x,
				pcon->vcrtcm_fb.viewport_y,
				pcon->vcrtcm_fb.hdisplay,
				pcon->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&minor->lock, flags);
		minor->status &= ~UDLPIM_IN_DO_XMIT;
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
			 * and mark the buffer for USB transmission in the next
			 * vblank interval; note that call to vcrtcm_p_push only
			 * initiates the push request to GPU; when GPU does it
			 * is up to the GPU and doesn't matter as long as it is
			 * within the frame transmission period (otherwise, we'll
			 * see from frame tearing)
			 * If GPU completes the push before the next vblank
			 * interval, then it is perfectly safe to mark the buffer
			 * ready for transmission now because USB transmission wont
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
		minor->status &= ~UDLPIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&minor->lock, flags);

		vcrtcm_p_emulate_vblank(pcon->pconid);
		UDLPIM_DEBUG("transmission not happening\n");
	}

	if (pcon->pb_needs_xmit[push_buffer_index]) {
		unsigned long jiffies_snapshot;
		UDLPIM_DEBUG("[%d]: initiating USB transfer\n",
				push_buffer_index);

		minor->main_buffer = pcon->pb_fb[push_buffer_index];
		minor->cursor = pcon->pb_cursor[push_buffer_index];

		jiffies_snapshot = jiffies;
		udlpim_transmit_framebuffer(minor);
		UDLPIM_DEBUG("transmit over USB took %u ms\n",
			     jiffies_to_msecs(jiffies - jiffies_snapshot));
		pcon->pb_needs_xmit[push_buffer_index] = 0;
	}

	mutex_unlock(&minor->buffer_mutex);
	return r;
}

struct vcrtcm_p_pcon_funcs udlpim_pcon_funcs = {
	.attach = udlpim_attach,
	.detach = udlpim_detach,
	.set_fb = udlpim_set_fb,
	.get_fb = udlpim_get_fb,
	.dirty_fb = udlpim_dirty_fb,
	.wait_fb = udlpim_wait_fb,
	.get_fb_status = udlpim_get_fb_status,
	.set_fps = udlpim_set_fps,
	.set_cursor = udlpim_set_cursor,
	.get_cursor = udlpim_get_cursor,
	.set_dpms = udlpim_set_dpms,
	.get_dpms = udlpim_get_dpms,
	.connected = udlpim_connected,
	.get_modes = udlpim_get_modes,
	.check_mode = udlpim_check_mode,
	.disable = udlpim_disable,
	.vblank = udlpim_vblank,
};

struct udlpim_pcon *udlpim_create_pcon(int pconid,
	struct udlpim_minor *minor)
{
	struct udlpim_pcon *pcon;

	pcon = vcrtcm_kzalloc(sizeof(struct udlpim_pcon),
			GFP_KERNEL, pconid);
	if (pcon == NULL) {
		VCRTCM_ERROR("create_pcon failed, no memory\n");
		return NULL;
	}
	VCRTCM_INFO("alloced pcon %d (0x%p)\n", pconid, pcon);
	pcon->magic = UDLPIM_PCON_GOOD_MAGIC;
	pcon->pconid = pconid;
	pcon->vcrtcm_cursor.flag = VCRTCM_CURSOR_FLAG_HIDE;
	pcon->minor = minor;
	return pcon;
}

void udlpim_destroy_pcon(struct udlpim_pcon *pcon)
{
	VCRTCM_INFO("destroying pcon %d (0x%p)\n", pcon->pconid, pcon);
	pcon->magic = UDLPIM_PCON_BAD_MAGIC;
	pcon->minor->pcon = NULL;
	vcrtcm_kfree(pcon);
}

int udlpim_instantiate(int pconid, uint32_t hints,
	void **cookie, struct vcrtcm_p_pcon_funcs *funcs,
	enum vcrtcm_xfer_mode *xfer_mode, int *minornum,
	int *vblank_slack, char *description)
{
	struct udlpim_minor *minor;

	list_for_each_entry(minor, &udlpim_minor_list, list) {
		if (!minor->pcon) {
			struct usb_device *usbdev;

			vcrtcm_p_log_alloc_cnts(pconid,
						udlpim_log_pcon_alloc_counts);
			minor->pcon = udlpim_create_pcon(pconid, minor);
			if (!minor->pcon)
				return -ENOMEM;
			usbdev = minor->udev;
			scnprintf(description, PCON_DESC_MAXLEN,
					"%s %s - Serial #%s",
					usbdev->manufacturer,
					usbdev->product, usbdev->serial);
			*minornum = -1;
			*funcs = udlpim_pcon_funcs;
			*xfer_mode = VCRTCM_PUSH_PULL;
			*cookie = minor->pcon;
			*vblank_slack = udlpim_fake_vblank_slack;
			VCRTCM_INFO("udlpim %d now serves pcon %d\n",
				minor->minor, pconid);
			return 0;
		}
	}
	return -ENODEV;
}

void udlpim_destroy(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = udlpim_cookie2pcon(pconid, cookie);
	struct udlpim_minor *minor;

	/* the pim destroy callback can assume that the pcon is detached */
	if (!pcon)
		return;
	minor = pcon->minor;
	udlpim_destroy_pcon(pcon);
	minor->pcon = NULL;
}
