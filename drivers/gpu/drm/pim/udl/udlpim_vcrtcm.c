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

#include "udlpim.h"
#include "udlpim_vcrtcm.h"

static void udlpim_free_pb(struct udlpim_info *udlpim_info,
		struct udlpim_flow_info *flow_info, int flag)
{
	int i;

	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;

	for (i = 0; i < 2; i++) {
		if (flag == UDLPIM_ALLOC_PB_FLAG_FB) {
			pbd = flow_info->pbd_fb[i];
			pb_mapped_ram = flow_info->pb_fb[i];
			flow_info->pbd_fb[i] = NULL;
		} else {
			pbd = flow_info->pbd_cursor[i];
			pb_mapped_ram = flow_info->pb_cursor[i];
			flow_info->pbd_cursor[i] = NULL;
		}
		if (pbd) {
			BUG_ON(!pbd->gpu_private);
			BUG_ON(!pb_mapped_ram);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_p_free_pb(flow_info->pconid, pbd,
					 &udlpim_info->kmalloc_track,
					 &udlpim_info->page_track);
		}
	}
}

int udlpim_attach(int pconid, void *cookie)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;

	VCRTCM_INFO("Attaching udlpim %d to pcon %d\n",
		udlpim_info->minor, pconid);

	if (udlpim_info->flow_info) {
		VCRTCM_ERROR("attach: minor already served\n");
		return -EBUSY;
	} else {
		struct udlpim_flow_info *flow_info =
			vcrtcm_kzalloc(sizeof(struct udlpim_flow_info),
				GFP_KERNEL, &udlpim_info->kmalloc_track);
		if (flow_info == NULL) {
			VCRTCM_ERROR("attach: no memory\n");
			return -ENOMEM;
		}

		flow_info->udlpim_info = udlpim_info;
		flow_info->pconid = pconid;
		flow_info->fps = 0;
		flow_info->fb_force_xmit = 0;
		flow_info->fb_xmit_allowed = 0;
		flow_info->fb_xmit_counter = 0;
		flow_info->fb_xmit_period_jiffies = 0;
		flow_info->next_vblank_jiffies = 0;
		flow_info->push_buffer_index = 0;
		flow_info->pb_needs_xmit[0] = 0;
		flow_info->pb_needs_xmit[1] = 0;
		flow_info->pbd_fb[0] = NULL;
		flow_info->pbd_fb[1] = NULL;
		flow_info->pbd_cursor[0] = NULL;
		flow_info->pbd_cursor[1] = NULL;
		flow_info->pb_fb[0] = NULL;
		flow_info->pb_fb[1] = NULL;
		flow_info->pb_cursor[0] = NULL;
		flow_info->pb_cursor[1] = NULL;

		flow_info->vcrtcm_cursor.flag = VCRTCM_CURSOR_FLAG_HIDE;

		udlpim_info->flow_info = flow_info;

		/* Do an initial query of the EDID */
		udlpim_query_edid_core(udlpim_info);

		/* Start the EDID query thread */
		queue_delayed_work(udlpim_info->workqueue,
					&udlpim_info->query_edid_work, 0);

		VCRTCM_INFO("udlpim %d now serves pcon %d\n",
			udlpim_info->minor, pconid);

		return 0;
	}
}

int udlpim_detach(int pconid, void *cookie)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;

	VCRTCM_INFO("Detaching udlpim %d from pcon %d\n",
		udlpim_info->minor, pconid);

	vcrtcm_p_wait_fb(pconid);
	flow_info = udlpim_info->flow_info;

	cancel_delayed_work_sync(&udlpim_info->fake_vblank_work);
	cancel_delayed_work_sync(&udlpim_info->query_edid_work);

	if (flow_info->pconid == pconid) {
		UDLPIM_DEBUG("Found descriptor that should be removed.\n");

		udlpim_free_pb(udlpim_info, flow_info, UDLPIM_ALLOC_PB_FLAG_FB);
		udlpim_free_pb(udlpim_info, flow_info, UDLPIM_ALLOC_PB_FLAG_CURSOR);

		udlpim_info->flow_info = NULL;
		vcrtcm_kfree(flow_info, &udlpim_info->kmalloc_track);
	}
	return 0;
}

static int udlpim_realloc_pb(struct udlpim_info *udlpim_info,
			     struct udlpim_flow_info *flow_info,
			     int size, int flag)
{
	int num_pages, r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd0, *pbd1;
	void *pb_mapped_ram0, *pb_mapped_ram1;

	if (flag ==  UDLPIM_ALLOC_PB_FLAG_FB) {
		pbd0 = flow_info->pbd_fb[0];
		pbd1 = flow_info->pbd_fb[1];
		pb_mapped_ram0 = flow_info->pb_fb[0];
		pb_mapped_ram1 = flow_info->pb_fb[1];
	} else {
		pbd0 = flow_info->pbd_cursor[0];
		pbd1 = flow_info->pbd_cursor[1];
		pb_mapped_ram0 = flow_info->pb_cursor[0];
		pb_mapped_ram1 = flow_info->pb_cursor[1];
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
	pbd0 = vcrtcm_p_realloc_pb(flow_info->pconid, pbd0,
				   num_pages, GFP_KERNEL | __GFP_HIGHMEM,
				   &udlpim_info->kmalloc_track,
				   &udlpim_info->page_track);
	if (IS_ERR(pbd0)) {
		r = PTR_ERR(pbd0);
		goto out_err0;
	}
	pbd1 = vcrtcm_p_realloc_pb(flow_info->pconid, pbd1,
				   num_pages, GFP_KERNEL | __GFP_HIGHMEM,
				   &udlpim_info->kmalloc_track,
				   &udlpim_info->page_track);
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
		flow_info->pbd_fb[0] = pbd0;
		flow_info->pbd_fb[1] = pbd1;
		flow_info->pb_fb[0] = pb_mapped_ram0;
		flow_info->pb_fb[1] = pb_mapped_ram1;
	} else {
		flow_info->pbd_cursor[0] = pbd0;
		flow_info->pbd_cursor[1] = pbd1;
		flow_info->pb_cursor[0] = pb_mapped_ram0;
		flow_info->pb_cursor[1] = pb_mapped_ram1;
	}
	return 0;
out_err3:
	if (pbd0)
		vm_unmap_ram(pb_mapped_ram0, pbd0->num_pages);
out_err2:
	vcrtcm_p_free_pb(flow_info->pconid, pbd1,
			 &udlpim_info->kmalloc_track,
			 &udlpim_info->page_track);
out_err1:
	vcrtcm_p_free_pb(flow_info->pconid, pbd0,
			 &udlpim_info->kmalloc_track,
			 &udlpim_info->page_track);
out_err0:
	if (flag ==  UDLPIM_ALLOC_PB_FLAG_FB) {
		flow_info->pbd_fb[0] = NULL;
		flow_info->pbd_fb[1] = NULL;
		flow_info->pb_fb[0] = NULL;
		flow_info->pb_fb[1] = NULL;
	} else {
		flow_info->pbd_cursor[0] = NULL;
		flow_info->pbd_cursor[1] = NULL;
		flow_info->pb_cursor[0] = NULL;
		flow_info->pb_cursor[1] = NULL;
	}
	return r;
}

int udlpim_set_fb(int pconid, void *cookie,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int found_mode = 0;
	int r = 0;
	int i = 0;
	int size;

	UDLPIM_DEBUG("minor %d.\n", udlpim_info->minor);

	flow_info = udlpim_info->flow_info;

	/* TODO: Do we need this? */
	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpim_info->buffer_mutex);
	memcpy(&flow_info->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));

	udlpim_build_modelist(udlpim_info,
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
			udlpim_info->bpp = vcrtcm_fb->bpp;
			udlpim_setup_screen(udlpim_info,
					&udlpim_video_modes[i], vcrtcm_fb);
			found_mode = 1;
			flow_info->fb_xmit_allowed = 1;
			break;
		}
	}

	udlpim_free_modelist(udlpim_info, udlpim_video_modes);

	if (!found_mode) {
		VCRTCM_ERROR("could not find matching mode...\n");
		flow_info->fb_xmit_allowed = 0;
		udlpim_error_screen(udlpim_info);
		mutex_unlock(&udlpim_info->buffer_mutex);
		return 0;
	}

	size = flow_info->vcrtcm_fb.pitch *
		flow_info->vcrtcm_fb.vdisplay;
	r = udlpim_realloc_pb(udlpim_info, flow_info, size,
			      UDLPIM_ALLOC_PB_FLAG_FB);
	mutex_unlock(&udlpim_info->buffer_mutex);
	return r;
}

int udlpim_get_fb(int pconid, void *cookie,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;

	UDLPIM_DEBUG("minor %d.\n", udlpim_info->minor);
	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpim_info->buffer_mutex);
	memcpy(vcrtcm_fb, &flow_info->vcrtcm_fb, sizeof(struct vcrtcm_fb));
	mutex_unlock(&udlpim_info->buffer_mutex);
	return 0;
}

int udlpim_dirty_fb(int pconid, void *cookie,
		    struct drm_crtc *drm_crtc)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;

	UDLPIM_DEBUG("minor %d\n", udlpim_info->minor);

	/* just mark the "force" flag, udlpim_do_xmit_fb_pull
	 * does the rest (when called).
	 */

	flow_info = udlpim_info->flow_info;

	if (flow_info)
		flow_info->fb_force_xmit = 1;

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
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	unsigned long flags;

	UDLPIM_DEBUG("\n");

	spin_lock_irqsave(&udlpim_info->udlpim_lock, flags);
	if (udlpim_info->status & UDLPIM_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

	*status = tmp_status;

	return 0;
}


int udlpim_set_fps(int pconid, void *cookie, int fps)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;
	unsigned long jiffies_snapshot;

	UDLPIM_DEBUG("fps %d.\n", fps);

	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (fps > UDLPIM_FPS_HARD_LIMIT) {
		VCRTCM_ERROR("Frame rate above the hard limit\n");
		return -EINVAL;
	}

	if (fps <= 0) {
		flow_info->fb_xmit_period_jiffies = 0;
		VCRTCM_INFO("Transmission disabled, (negative or zero fps).\n");
	} else {
		flow_info->fps = fps;
		flow_info->fb_xmit_period_jiffies = HZ / fps;
		jiffies_snapshot = jiffies;
		flow_info->last_xmit_jiffies = jiffies_snapshot;
		flow_info->fb_force_xmit = 1;
		flow_info->next_vblank_jiffies =
			jiffies_snapshot + flow_info->fb_xmit_period_jiffies;

		VCRTCM_INFO("Frame transmission period set to %d jiffies\n",
			HZ / fps);
	}

	/* Schedule initial fake vblank */
	/*schedule_delayed_work(&udlpim_info->fake_vblank_work, 0);*/
	queue_delayed_work(udlpim_info->workqueue, &udlpim_info->fake_vblank_work, 0);

	return 0;
}

int udlpim_get_fps(int pconid, void *cookie, int *fps)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;

	UDLPIM_DEBUG("\n");

	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (flow_info->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		VCRTCM_INFO
		("Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ / flow_info->fb_xmit_period_jiffies;
		return 0;
	}
}

int udlpim_set_cursor(int pconid, void *cookie,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;
	int r = 0;
	int size;

	UDLPIM_DEBUG("minor %d\n", udlpim_info->minor);

	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpim_info->buffer_mutex);
	memcpy(&flow_info->vcrtcm_cursor, vcrtcm_cursor,
			sizeof(struct vcrtcm_cursor));
	size = flow_info->vcrtcm_cursor.height *
		flow_info->vcrtcm_cursor.width *
		(flow_info->vcrtcm_cursor.bpp >> 3);
	r = udlpim_realloc_pb(udlpim_info, flow_info, size,
			      UDLPIM_ALLOC_PB_FLAG_CURSOR);
	mutex_unlock(&udlpim_info->buffer_mutex);
	return r;
}

int udlpim_get_cursor(int pconid, void *cookie,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;

	UDLPIM_DEBUG("minor %d\n", udlpim_info->minor);

	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpim_info->buffer_mutex);
	memcpy(vcrtcm_cursor, &flow_info->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));
	mutex_unlock(&udlpim_info->buffer_mutex);
	return 0;
}

int udlpim_set_dpms(int pconid, void *cookie, int state)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;

	UDLPIM_DEBUG("minor %d, state %d\n", udlpim_info->minor, state);

	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	flow_info->dpms_state = state;

	if (state == VCRTCM_DPMS_STATE_ON) {
		udlpim_dpms_wakeup(udlpim_info);
	} else if (state == VCRTCM_DPMS_STATE_OFF) {
		udlpim_dpms_sleep(udlpim_info);
	}

	return 0;
}

int udlpim_get_dpms(int pconid, void *cookie, int *state)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info;

	UDLPIM_DEBUG("minor %d\n", udlpim_info->minor);

	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	*state = flow_info->dpms_state;

	return 0;
}

int udlpim_connected(int pconid, void *cookie, int *status)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	UDLPIM_DEBUG("udlpim_info %p\n", udlpim_info);

	if (udlpim_info->monitor_connected) {
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
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_video_mode *udlpim_video_modes;
	struct vcrtcm_mode *vcrtcm_mode_list =
			udlpim_info->last_vcrtcm_mode_list;
	int udlpim_mode_count = 0;
	int vcrtcm_mode_count = 0;
	int retval = 0;
	int i = 0;

	*modes = NULL;
	*count = 0;

	UDLPIM_DEBUG("\n");

	retval = udlpim_build_modelist(udlpim_info,
			&udlpim_video_modes, &udlpim_mode_count);

	if (retval < 0)
		return retval;

	if (udlpim_mode_count == 0)
		return 0;

	/* If we get this far, we can return modes. */

	/* Erase our old VCRTCM modelist. */
	if (vcrtcm_mode_list) {
		vcrtcm_kfree(vcrtcm_mode_list, &udlpim_info->kmalloc_track);
		vcrtcm_mode_list = NULL;
		vcrtcm_mode_count = 0;
	}

	/* Build the new vcrtcm_mode list. */
	vcrtcm_mode_list =
		vcrtcm_kmalloc(sizeof(struct vcrtcm_mode) * udlpim_mode_count,
			GFP_KERNEL, &udlpim_info->kmalloc_track);

	/* Copy the udlpim_video_mode list to the vcrtcm_mode list. */
	for (i = 0; i < udlpim_mode_count; i++) {
		vcrtcm_mode_list[i].w = udlpim_video_modes[i].xres;
		vcrtcm_mode_list[i].h = udlpim_video_modes[i].yres;
		vcrtcm_mode_list[i].refresh = udlpim_video_modes[i].refresh;
	}

	vcrtcm_mode_count = udlpim_mode_count;
	udlpim_info->last_vcrtcm_mode_list = vcrtcm_mode_list;

	*modes = vcrtcm_mode_list;
	*count = vcrtcm_mode_count;

	udlpim_free_modelist(udlpim_info, udlpim_video_modes);

	return 0;
}

int udlpim_check_mode(int pconid, void *cookie,
		      struct vcrtcm_mode *mode, int *status)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int retval;
	int i;

	UDLPIM_DEBUG("\n");

	*status = VCRTCM_MODE_BAD;

	retval = udlpim_build_modelist(udlpim_info,
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

	udlpim_free_modelist(udlpim_info, udlpim_video_modes);

	return 0;
}

void udlpim_disable(int pconid, void *cookie)
{
	struct udlpim_info *udlpim_info =
		(struct udlpim_info *)cookie;
	struct udlpim_flow_info *flow_info =
			udlpim_info->flow_info;

	mutex_lock(&udlpim_info->buffer_mutex);
	flow_info->fb_xmit_allowed = 0;
	mutex_unlock(&udlpim_info->buffer_mutex);
}

void udlpim_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct udlpim_info *udlpim_info =
		container_of(delayed_work, struct udlpim_info, fake_vblank_work);
	struct udlpim_flow_info *flow_info;
	/*static long last_snapshot = 0;*/

	unsigned long jiffies_snapshot = 0;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int udlpim_fake_vblank_slack_sane = 0;

	UDLPIM_DEBUG("minor=%d\n", udlpim_info->minor);
	udlpim_fake_vblank_slack_sane =
			(udlpim_fake_vblank_slack_sane <= 0) ? 0 : udlpim_fake_vblank_slack;

	if (!udlpim_info) {
		VCRTCM_ERROR("Cannot find udlpim_info\n");
		return;
	}

	flow_info = udlpim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return;
	}

	jiffies_snapshot = jiffies;

	if (flow_info->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + udlpim_fake_vblank_slack_sane,
				flow_info->next_vblank_jiffies)) {
			flow_info->next_vblank_jiffies +=
					flow_info->fb_xmit_period_jiffies;

			mutex_lock(&udlpim_info->buffer_mutex);
			udlpim_do_xmit_fb_push(flow_info);
			mutex_unlock(&udlpim_info->buffer_mutex);
		}

		if (!next_vblank_jiffies_valid) {
			next_vblank_jiffies = flow_info->next_vblank_jiffies;
			next_vblank_jiffies_valid = 1;
		} else {
			if (time_after_eq(next_vblank_jiffies,
					flow_info->next_vblank_jiffies)) {
				next_vblank_jiffies = flow_info->next_vblank_jiffies;
			}
		}
	}

	if (next_vblank_jiffies_valid) {
		next_vblank_delay =
			(int)next_vblank_jiffies - (int)jiffies_snapshot;
		if (next_vblank_delay <= udlpim_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		if (!queue_delayed_work(udlpim_info->workqueue,
					&udlpim_info->fake_vblank_work,
					next_vblank_delay))
			VCRTCM_WARNING("dup fake vblank, minor %d\n",
				udlpim_info->minor);
	} else
		UDLPIM_DEBUG("Next fake vblank not scheduled\n");

	return;
}

int udlpim_do_xmit_fb_push(struct udlpim_flow_info *flow_info)
{
	struct udlpim_info *udlpim_info = flow_info->udlpim_info;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	UDLPIM_DEBUG("minor %d\n", udlpim_info->minor);

	spin_lock_irqsave(&udlpim_info->udlpim_lock, flags);
	udlpim_info->status |= UDLPIM_IN_DO_XMIT;
	spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

	push_buffer_index = flow_info->push_buffer_index;

	if (flow_info->pbd_fb) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		VCRTCM_WARNING("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((flow_info->fb_force_xmit ||
	     time_after(jiffies_snapshot, flow_info->last_xmit_jiffies +
			UDLPIM_XMIT_HARD_DEADLINE)) &&
			have_push_buffer &&
			udlpim_info->monitor_connected &&
			flow_info->fb_xmit_allowed) {
		/* Someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 * We also check to see if we have a monitor connected, and if
		 * we are allowed to transmit.
		 */

		UDLPIM_DEBUG("transmission happening...\n");
		flow_info->fb_force_xmit = 0;
		flow_info->last_xmit_jiffies = jiffies;
		flow_info->fb_xmit_counter++;

		UDLPIM_DEBUG("[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				flow_info->vcrtcm_fb.pitch,
				flow_info->vcrtcm_fb.width,
				flow_info->vcrtcm_fb.height,
				flow_info->vcrtcm_fb.bpp);

		UDLPIM_DEBUG("[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				flow_info->vcrtcm_fb.viewport_x,
				flow_info->vcrtcm_fb.viewport_y,
				flow_info->vcrtcm_fb.hdisplay,
				flow_info->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&udlpim_info->udlpim_lock, flags);
		udlpim_info->status &= ~UDLPIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

		r = vcrtcm_p_push(flow_info->pconid,
				  flow_info->pbd_fb[push_buffer_index],
				  flow_info->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_p_emulate_vblank(flow_info->pconid);
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

			flow_info->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			flow_info->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		spin_lock_irqsave(&udlpim_info->udlpim_lock, flags);
		udlpim_info->status &= ~UDLPIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

		vcrtcm_p_emulate_vblank(flow_info->pconid);
		UDLPIM_DEBUG("transmission not happening\n");
	}

	if (flow_info->pb_needs_xmit[push_buffer_index]) {
		unsigned long jiffies_snapshot;
		UDLPIM_DEBUG("[%d]: initiating USB transfer\n",
				push_buffer_index);

		udlpim_info->main_buffer = flow_info->pb_fb[push_buffer_index];
		udlpim_info->cursor = flow_info->pb_cursor[push_buffer_index];

		jiffies_snapshot = jiffies;
		udlpim_transmit_framebuffer(udlpim_info);
		UDLPIM_DEBUG("transmit over USB took %u ms\n",
			     jiffies_to_msecs(jiffies - jiffies_snapshot));
		flow_info->pb_needs_xmit[push_buffer_index] = 0;
	}

	return r;
}
