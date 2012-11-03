/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Author: Bill Katsak <william.katsak@alcatel-lucent.com>

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

#include <vcrtcm/vcrtcm_pcon.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
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
		} else {
			pbd = pcon->pbd_cursor[i];
			pb_mapped_ram = pcon->pb_cursor[i];
			pcon->pbd_cursor[i] = NULL;
		}
		if (pbd) {
			BUG_ON(!pbd->gpu_private);
			BUG_ON(!pb_mapped_ram);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_p_free_pb(pcon->pconid, pbd,
					 &pcon->minor->kmalloc_track,
					 &pcon->minor->page_track);
		}
	}
}

int udlpim_attach(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -ENODEV;
	}
	minor = pcon->minor;
	VCRTCM_INFO("Attaching udlpim %d to pcon %d\n",
		pcon->minor->minor, pconid);
	pcon->attached = 1;
	return 0;
}

int udlpim_detach(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	VCRTCM_INFO("Detaching udlpim %d from pcon %d\n",
		minor->minor, pconid);

	vcrtcm_p_wait_fb(pconid);

	cancel_delayed_work_sync(&minor->fake_vblank_work);
	cancel_delayed_work_sync(&minor->query_edid_work);

	if (pcon->pconid == pconid) {
		UDLPIM_DEBUG("Found descriptor that should be removed.\n");

		pcon->attached = 0;
	}
	return 0;
}

static int udlpim_realloc_pb(struct udlpim_minor *udlpim_minor,
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
				   num_pages, GFP_KERNEL | __GFP_HIGHMEM,
				   &udlpim_minor->kmalloc_track,
				   &udlpim_minor->page_track);
	if (IS_ERR(pbd0)) {
		r = PTR_ERR(pbd0);
		goto out_err0;
	}
	pbd1 = vcrtcm_p_realloc_pb(pcon->pconid, pbd1,
				   num_pages, GFP_KERNEL | __GFP_HIGHMEM,
				   &udlpim_minor->kmalloc_track,
				   &udlpim_minor->page_track);
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
	vcrtcm_p_free_pb(pcon->pconid, pbd1,
			 &udlpim_minor->kmalloc_track,
			 &udlpim_minor->page_track);
out_err1:
	vcrtcm_p_free_pb(pcon->pconid, pbd0,
			 &udlpim_minor->kmalloc_track,
			 &udlpim_minor->page_track);
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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int found_mode = 0;
	int r = 0;
	int i = 0;
	int size;

	/* TODO: Do we need this? */
	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	mutex_lock(&minor->buffer_mutex);
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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	mutex_lock(&minor->buffer_mutex);
	memcpy(vcrtcm_fb, &pcon->vcrtcm_fb, sizeof(struct vcrtcm_fb));
	mutex_unlock(&minor->buffer_mutex);
	return 0;
}

int udlpim_dirty_fb(int pconid, void *cookie,
		    struct drm_crtc *drm_crtc)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	/* just mark the "force" flag, udlpim_do_xmit_fb_pull
	 * does the rest (when called).
	 */
	pcon->fb_force_xmit = 1;
	return 0;
}

int udlpim_wait_fb(int pconid, void *cookie,
		   struct drm_crtc *drm_crtc)
{
	return 0;
}

int udlpim_get_fb_status(int pconid, void *cookie,
			 struct drm_crtc *drm_crtc, u32 *status)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	unsigned long flags;

	UDLPIM_DEBUG("\n");
	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	spin_lock_irqsave(&minor->udlpim_lock, flags);
	if (minor->status & UDLPIM_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&minor->udlpim_lock, flags);
	*status = tmp_status;
	return 0;
}


int udlpim_set_fps(int pconid, void *cookie, int fps)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;
	unsigned long jiffies_snapshot;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	UDLPIM_DEBUG("fps %d\n", fps);

	if (fps > UDLPIM_FPS_HARD_LIMIT) {
		VCRTCM_ERROR("Frame rate above the hard limit\n");
		return -EINVAL;
	}

	if (fps <= 0) {
		pcon->fb_xmit_period_jiffies = 0;
		VCRTCM_INFO("Transmission disabled, (negative or zero fps).\n");
	} else {
		pcon->fps = fps;
		pcon->fb_xmit_period_jiffies = HZ / fps;
		jiffies_snapshot = jiffies;
		pcon->last_xmit_jiffies = jiffies_snapshot;
		pcon->fb_force_xmit = 1;
		pcon->next_vblank_jiffies =
			jiffies_snapshot + pcon->fb_xmit_period_jiffies;

		VCRTCM_INFO("Frame transmission period set to %d jiffies\n",
			HZ / fps);
	}

	/* Schedule initial fake vblank */
	/*schedule_delayed_work(&minor->fake_vblank_work, 0);*/
	queue_delayed_work(minor->workqueue, &minor->fake_vblank_work, 0);

	return 0;
}

int udlpim_get_fps(int pconid, void *cookie, int *fps)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	UDLPIM_DEBUG("\n");
	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;

	if (pcon->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		VCRTCM_INFO
		("Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ / pcon->fb_xmit_period_jiffies;
		return 0;
	}
}

int udlpim_set_cursor(int pconid, void *cookie,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;
	int r = 0;
	int size;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	mutex_lock(&minor->buffer_mutex);
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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	*state = pcon->dpms_state;

	return 0;
}

int udlpim_connected(int pconid, void *cookie, int *status)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	UDLPIM_DEBUG("");
	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;
	struct udlpim_video_mode *udlpim_video_modes;
	struct vcrtcm_mode *vcrtcm_mode_list;
	int udlpim_mode_count = 0;
	int vcrtcm_mode_count = 0;
	int retval = 0;
	int i = 0;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
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
		vcrtcm_kfree(vcrtcm_mode_list, &minor->kmalloc_track);
		vcrtcm_mode_list = NULL;
		vcrtcm_mode_count = 0;
	}

	/* Build the new vcrtcm_mode list. */
	vcrtcm_mode_list =
		vcrtcm_kmalloc(sizeof(struct vcrtcm_mode) * udlpim_mode_count,
			GFP_KERNEL, &minor->kmalloc_track);

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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int retval;
	int i;

	UDLPIM_DEBUG("\n");
	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
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
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return;
	}
	minor = pcon->minor;
	mutex_lock(&minor->buffer_mutex);
	pcon->fb_xmit_allowed = 0;
	mutex_unlock(&minor->buffer_mutex);
}

void udlpim_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct udlpim_minor *udlpim_minor =
		container_of(delayed_work, struct udlpim_minor, fake_vblank_work);
	struct udlpim_pcon *pcon;
	/*static long last_snapshot = 0;*/

	unsigned long jiffies_snapshot = 0;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int udlpim_fake_vblank_slack_sane = 0;

	UDLPIM_DEBUG("minor=%d\n", udlpim_minor->minor);
	udlpim_fake_vblank_slack_sane =
			(udlpim_fake_vblank_slack_sane <= 0) ? 0 : udlpim_fake_vblank_slack;

	if (!udlpim_minor) {
		VCRTCM_ERROR("Cannot find udlpim_minor\n");
		return;
	}

	pcon = udlpim_minor->pcon;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return;
	}

	jiffies_snapshot = jiffies;

	if (pcon->attached && pcon->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + udlpim_fake_vblank_slack_sane,
				pcon->next_vblank_jiffies)) {
			pcon->next_vblank_jiffies +=
					pcon->fb_xmit_period_jiffies;

			mutex_lock(&udlpim_minor->buffer_mutex);
			udlpim_do_xmit_fb_push(pcon);
			mutex_unlock(&udlpim_minor->buffer_mutex);
		}

		if (!next_vblank_jiffies_valid) {
			next_vblank_jiffies = pcon->next_vblank_jiffies;
			next_vblank_jiffies_valid = 1;
		} else {
			if (time_after_eq(next_vblank_jiffies,
					pcon->next_vblank_jiffies)) {
				next_vblank_jiffies = pcon->next_vblank_jiffies;
			}
		}
	}

	if (next_vblank_jiffies_valid) {
		next_vblank_delay =
			(int)next_vblank_jiffies - (int)jiffies_snapshot;
		if (next_vblank_delay <= udlpim_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		if (!queue_delayed_work(udlpim_minor->workqueue,
					&udlpim_minor->fake_vblank_work,
					next_vblank_delay))
			VCRTCM_WARNING("dup fake vblank, minor %d\n",
				udlpim_minor->minor);
	} else
		UDLPIM_DEBUG("Next fake vblank not scheduled\n");

	return;
}

int udlpim_do_xmit_fb_push(struct udlpim_pcon *pcon)
{
	struct udlpim_minor *minor;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	minor = pcon->minor;
	UDLPIM_DEBUG("minor %d\n", minor->minor);

	spin_lock_irqsave(&minor->udlpim_lock, flags);
	minor->status |= UDLPIM_IN_DO_XMIT;
	spin_unlock_irqrestore(&minor->udlpim_lock, flags);

	push_buffer_index = pcon->push_buffer_index;

	if (pcon->pbd_fb) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		VCRTCM_WARNING("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((pcon->fb_force_xmit ||
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
		pcon->fb_force_xmit = 0;
		pcon->last_xmit_jiffies = jiffies;
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

		spin_lock_irqsave(&minor->udlpim_lock, flags);
		minor->status &= ~UDLPIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&minor->udlpim_lock, flags);

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

			pcon->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			pcon->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		spin_lock_irqsave(&minor->udlpim_lock, flags);
		minor->status &= ~UDLPIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&minor->udlpim_lock, flags);

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

	return r;
}

static int udlpim_get_properties(int pconid, void *cookie,
				 struct vcrtcm_pcon_properties *props)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}
	minor = pcon->minor;
	props->fps = pcon->fps;
	props->attached = pcon->attached;
	return 0;
}

struct vcrtcm_pcon_funcs udlpim_vcrtcm_pcon_funcs = {
	.attach = udlpim_attach,
	.detach = udlpim_detach,
	.set_fb = udlpim_set_fb,
	.get_fb = udlpim_get_fb,
	.get_properties = udlpim_get_properties,
	.dirty_fb = udlpim_dirty_fb,
	.wait_fb = udlpim_wait_fb,
	.get_fb_status = udlpim_get_fb_status,
	.set_fps = udlpim_set_fps,
	.get_fps = udlpim_get_fps,
	.set_cursor = udlpim_set_cursor,
	.get_cursor = udlpim_get_cursor,
	.set_dpms = udlpim_set_dpms,
	.get_dpms = udlpim_get_dpms,
	.connected = udlpim_connected,
	.get_modes = udlpim_get_modes,
	.check_mode = udlpim_check_mode,
	.disable = udlpim_disable
};

int udlpim_instantiate(int pconid, uint32_t hints,
	void **cookie, struct vcrtcm_pcon_funcs *funcs,
	enum vcrtcm_xfer_mode *xfer_mode, int *minornum,
	char *description)
{
	struct udlpim_minor *minor;
	struct usb_device *usbdev;

	list_for_each_entry(minor, &udlpim_minor_list, list) {
		if (!minor->pcon) {
			struct udlpim_pcon *pcon;

			usbdev = minor->udev;
			scnprintf(description, PCON_DESC_MAXLEN,
					"%s %s - Serial #%s",
					usbdev->manufacturer,
					usbdev->product,
					usbdev->serial);
			*minornum = -1;
			*funcs = udlpim_vcrtcm_pcon_funcs;
			*xfer_mode = VCRTCM_PUSH_PULL;
			pcon = vcrtcm_kzalloc(sizeof(struct udlpim_pcon),
					GFP_KERNEL, &minor->kmalloc_track);
			if (pcon == NULL) {
				VCRTCM_ERROR("attach: no memory\n");
				return -ENOMEM;
			}
			*cookie = pcon;
			pcon->minor = minor;
			pcon->pconid = pconid;
			pcon->vcrtcm_cursor.flag = VCRTCM_CURSOR_FLAG_HIDE;
			minor->pcon = pcon;

			/* Do an initial query of the EDID */
			udlpim_query_edid_core(minor);

			/* Start the EDID query thread */
			queue_delayed_work(minor->workqueue,
						&minor->query_edid_work, 0);

			VCRTCM_INFO("udlpim %d now serves pcon %d\n",
				minor->minor, pconid);
			return 0;
		}
	}

	return -ENODEV;
}

void udlpim_destroy(int pconid, void *cookie)
{
	struct udlpim_pcon *pcon = cookie;
	struct udlpim_minor *minor;

	if (!pcon) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return;
	}
	minor = pcon->minor;
	udlpim_free_pb(pcon, UDLPIM_ALLOC_PB_FLAG_FB);
	udlpim_free_pb(pcon, UDLPIM_ALLOC_PB_FLAG_CURSOR);
	minor->pcon = NULL;
	vcrtcm_kfree(pcon, &minor->kmalloc_track);
}
