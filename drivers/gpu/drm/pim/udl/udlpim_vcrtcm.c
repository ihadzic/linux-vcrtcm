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
#include "udlpim_utils.h"


static void udlpim_free_pb(struct udlpim_info *udlpim_info,
		struct udlpim_flow_info *flow_info, int flag)
{
	int i;

	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;

	for (i = 0; i < 2; i++) {
		if (flag == UDLPCON_ALLOC_PB_FLAG_FB) {
			pbd = &flow_info->pbd_fb[i];
			pb_mapped_ram = flow_info->pb_fb[i];
		} else {
			pbd = &flow_info->pbd_cursor[i];
			pb_mapped_ram = flow_info->pb_cursor[i];
		}

		if (pbd->num_pages) {
			BUG_ON(!pbd->gpu_private);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_p_push_buffer_free(flow_info->vcrtcm_pcon_info, pbd);
		}
	}
}

static int udlpim_alloc_pb(struct udlpim_info *udlpim_info,
		struct udlpim_flow_info *flow_info,
		int requested_num_pages, int flag)
{
	int i;
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd = NULL;

	for (i = 0; i < 2; i++) {
		if (flag == UDLPCON_ALLOC_PB_FLAG_FB)
			pbd = &flow_info->pbd_fb[i];
		else
			pbd = &flow_info->pbd_cursor[i];

		pbd->num_pages = requested_num_pages;
		r = vcrtcm_p_push_buffer_alloc(flow_info->vcrtcm_pcon_info, pbd);
		if (r) {
			PR_ERR("%s[%d]: push buffer alloc_failed\n",
					UDLPCON_ALLOC_PB_STRING(flag), i);
			memset(pbd, 0,
				sizeof(struct vcrtcm_push_buffer_descriptor));
			break;
		}

		if (pbd->num_pages != requested_num_pages) {
			PR_ERR("%s[%d]: incorrect size allocated\n",
					UDLPCON_ALLOC_PB_STRING(flag), i);
			vcrtcm_p_push_buffer_free(flow_info->vcrtcm_pcon_info, pbd);
			/* incorrect size in most cases means too few pages */
			/* so it makes sense to return ENOMEM here */
			r = -ENOMEM;
			break;
		}

		flow_info->pb_needs_xmit[i] = 0;
		PR_DEBUG("%s[%d]: allocated %lu pages\n",
				UDLPCON_ALLOC_PB_STRING(flag),
				i, pbd->num_pages);

		/* we have the buffer, now we need to map it */
		/* (and that can fail too) */
		if (flag == UDLPCON_ALLOC_PB_FLAG_FB) {
			flow_info->pb_fb[i] =
				vm_map_ram(flow_info->pbd_fb[i].pages,
					flow_info->pbd_fb[i].num_pages,
					0, PAGE_KERNEL);

			if (flow_info->pb_fb[i] == NULL) {
				/* If we couldn't map it, we need to */
				/* free the buffer */
				vcrtcm_p_push_buffer_free(flow_info->vcrtcm_pcon_info,
							&flow_info->pbd_fb[i]);
				/* TODO: Is this right to return ENOMEM? */
				r = -ENOMEM;
				break;
			}
		} else {
			flow_info->pb_cursor[i] =
				vm_map_ram(flow_info->pbd_cursor[i].pages,
					flow_info->pbd_cursor[i].num_pages,
					0, PAGE_KERNEL);

			if (flow_info->pb_cursor[i] == NULL) {
				/* If we couldn't map it, we need to */
				/* free the buffer */
				vcrtcm_p_push_buffer_free(flow_info->vcrtcm_pcon_info,
							&flow_info->pbd_cursor[i]);
				/* TODO: Is this right to return ENOMEM? */
				r = -ENOMEM;
				break;
			}
		}
	}

	if (r && (i == 1)) {
		/* allocation failed in the second iteration */
		/* of the loop, we must release the buffer */
		/* allocated in the first one before returning*/
		if (flag == UDLPCON_ALLOC_PB_FLAG_FB) {
			vm_unmap_ram(flow_info->pbd_fb[0].pages,
				flow_info->pbd_fb[0].num_pages);
			vcrtcm_p_push_buffer_free(flow_info->vcrtcm_pcon_info,
						&flow_info->pbd_fb[0]);
		} else {
			vm_unmap_ram(flow_info->pbd_cursor[0].pages,
					flow_info->pbd_cursor[0].num_pages);
			vcrtcm_p_push_buffer_free(flow_info->vcrtcm_pcon_info,
						&flow_info->pbd_cursor[0]);
		}
	}

	return r;
}

int udlpim_attach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;

	PR_INFO("Attaching udlpim %d to pcon %p\n",
		udlpim_info->minor, vcrtcm_pcon_info);

	if (udlpim_info->udlpim_flow_info) {
		PR_ERR("attach: minor already served\n");
		return -EBUSY;
	} else {
		struct udlpim_flow_info
			*flow_info = udlpim_kzalloc(udlpim_info,
				sizeof(struct udlpim_flow_info),
				GFP_KERNEL);
		if (flow_info == NULL) {
			PR_ERR("attach: no memory\n");
			return -ENOMEM;
		}

		flow_info->udlpim_info = udlpim_info;
		flow_info->vcrtcm_pcon_info = vcrtcm_pcon_info;
		flow_info->fb_force_xmit = 0;
		flow_info->fb_xmit_allowed = 0;
		flow_info->fb_xmit_counter = 0;
		flow_info->fb_xmit_period_jiffies = 0;
		flow_info->next_vblank_jiffies = 0;
		flow_info->push_buffer_index = 0;
		flow_info->pb_needs_xmit[0] = 0;
		flow_info->pb_needs_xmit[1] = 0;
		memset(&flow_info->pbd_fb, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		memset(&flow_info->pbd_cursor, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		flow_info->pb_fb[0] = 0;
		flow_info->pb_fb[1] = 0;
		flow_info->pb_cursor[0] = 0;
		flow_info->pb_cursor[1] = 0;

		flow_info->vcrtcm_cursor.flag =
			VCRTCM_CURSOR_FLAG_HIDE;

		udlpim_info->udlpim_flow_info =
			flow_info;

		/* Do an initial query of the EDID */
		udlpim_query_edid_core(udlpim_info);

		/* Start the EDID query thread */
		queue_delayed_work(udlpim_info->workqueue,
					&udlpim_info->query_edid_work, 0);

		PR_INFO("udlpim %d now serves pcon %p\n", udlpim_info->minor,
			vcrtcm_pcon_info);

		return 0;
	}
}

void udlpim_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;

	PR_INFO("Detaching udlpim %d from pcon %p\n",
		udlpim_info->minor, vcrtcm_pcon_info);

	vcrtcm_p_gpu_sync(vcrtcm_pcon_info);
	flow_info = udlpim_info->udlpim_flow_info;

	cancel_delayed_work_sync(&udlpim_info->fake_vblank_work);
	cancel_delayed_work_sync(&udlpim_info->query_edid_work);

	if (flow_info->vcrtcm_pcon_info == vcrtcm_pcon_info) {
		PR_DEBUG("Found descriptor that should be removed.\n");

		udlpim_free_pb(udlpim_info, flow_info, UDLPCON_ALLOC_PB_FLAG_FB);
		udlpim_free_pb(udlpim_info, flow_info, UDLPCON_ALLOC_PB_FLAG_CURSOR);

		udlpim_info->udlpim_flow_info = NULL;
		udlpim_kfree(udlpim_info, flow_info);

		udlpim_info->main_buffer = NULL;
		udlpim_info->cursor = NULL;
		udlpim_info->hline_16 = NULL;
		udlpim_info->hline_8 = NULL;
	}
}

int udlpim_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *udlpim_info_,
			int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int found_mode = 0;
	int r = 0;
	int i = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In udlpim_set_fb, minor %d.\n", udlpim_info->minor);

	flow_info = udlpim_info->udlpim_flow_info;

	/* TODO: Do we need this? */
	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpim_info->buffer_mutex);
	memcpy(&flow_info->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));

	udlpim_build_modelist(udlpim_info,
			&udlpim_video_modes, &udlpim_mode_count);

	/* Find a matching video mode and switch the DL device to that mode */
	for (i = 0; i < udlpim_mode_count; i++) {
		PR_DEBUG("set_fb checking %dx%d\n",
				udlpim_video_modes[i].xres,
				udlpim_video_modes[i].yres);
		PR_DEBUG("against %dx%d\n",
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
		PR_ERR("could not find matching mode...\n");
		flow_info->fb_xmit_allowed = 0;
		udlpim_error_screen(udlpim_info);
		mutex_unlock(&udlpim_info->buffer_mutex);
		return 0;
	}

	size_in_bytes = flow_info->vcrtcm_fb.pitch *
			flow_info->vcrtcm_fb.vdisplay;

	requested_num_pages = size_in_bytes / PAGE_SIZE;
	if (size_in_bytes % PAGE_SIZE)
		requested_num_pages++;

	BUG_ON(flow_info->pbd_fb[0].num_pages != flow_info->pbd_fb[1].num_pages);

	if (!requested_num_pages) {
		PR_DEBUG("framebuffer: zero size requested\n");
		udlpim_free_pb(udlpim_info, flow_info,
				UDLPCON_ALLOC_PB_FLAG_FB);
	} else if (flow_info->pbd_fb[0].num_pages == requested_num_pages) {
		/* we can safely check index 0 num_pages against */
		/* index 1 num_pages because we checked that they */
		/* are equal above. */
		PR_DEBUG("framebuffer: "
			"reusing existing push buffer\n");
	} else {
		/* if we are here, we either have no push buffer */
		/* or we have the wrong size (i.e. mode changed) */
		PR_INFO("framebuffer: allocating push buffer, "
				"size=%d, num_pages=%d\n",
				size_in_bytes, requested_num_pages);
		udlpim_free_pb(udlpim_info, flow_info,
				UDLPCON_ALLOC_PB_FLAG_FB);

		r = udlpim_alloc_pb(udlpim_info, flow_info,
				requested_num_pages,
				UDLPCON_ALLOC_PB_FLAG_FB);
	}
	mutex_unlock(&udlpim_info->buffer_mutex);
	return r;
}

int udlpim_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *udlpim_info_,
			int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;

	PR_DEBUG("In udlpim_get_fb, minor %d.\n", udlpim_info->minor);
	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_fb, &flow_info->vcrtcm_fb, sizeof(struct vcrtcm_fb));

	return 0;
}

int udlpim_dirty_fb(struct drm_crtc *drm_crtc, void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;

	PR_DEBUG("in udlpim_dirty_fb, minor %d\n", udlpim_info->minor);

	/* just mark the "force" flag, udlpim_do_xmit_fb_pull
	 * does the rest (when called).
	 */

	flow_info = udlpim_info->udlpim_flow_info;

	if (flow_info)
		flow_info->fb_force_xmit = 1;

	return 0;
}

int udlpim_wait_fb(struct drm_crtc *drm_crtc, void *udlpim_info_, int flow)
{
	return 0;
}

int udlpim_get_fb_status(struct drm_crtc *drm_crtc,
		void *udlpim_info_, int flow, u32 *status)
{
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	unsigned long flags;

	PR_DEBUG("Queried for status\n");

	spin_lock_irqsave(&udlpim_info->udlpim_lock, flags);
	if (udlpim_info->status & UDLPCON_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

	*status = tmp_status;

	return 0;
}


int udlpim_set_fps(int fps, void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;
	unsigned long jiffies_snapshot;

	PR_DEBUG("udlpim_set_fps, fps %d.\n", fps);

	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (fps > UDLPCON_FPS_HARD_LIMIT) {
		PR_ERR("Frame rate above the hard limit\n");
		return -EINVAL;
	}

	if (fps <= 0) {
		flow_info->fb_xmit_period_jiffies = 0;
		PR_INFO("Transmission disabled, (negative or zero fps).\n");
	} else {
		flow_info->fb_xmit_period_jiffies = HZ / fps;
		jiffies_snapshot = jiffies;
		flow_info->last_xmit_jiffies = jiffies_snapshot;
		flow_info->fb_force_xmit = 1;
		flow_info->next_vblank_jiffies =
			jiffies_snapshot + flow_info->fb_xmit_period_jiffies;

		PR_INFO("Frame transmission period set to %d jiffies\n",
			HZ / fps);
	}

	/* Schedule initial fake vblank */
	/*schedule_delayed_work(&udlpim_info->fake_vblank_work, 0);*/
	queue_delayed_work(udlpim_info->workqueue, &udlpim_info->fake_vblank_work, 0);

	return 0;
}

int udlpim_get_fps(int *fps, void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;

	PR_DEBUG("udlpim_get_fps.\n");

	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (flow_info->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		PR_INFO
		("Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ / flow_info->fb_xmit_period_jiffies;
		return 0;
	}
}

int udlpim_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;
	int r = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In udlpim_set_cursor, minor %d\n", udlpim_info->minor);

	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpim_info->buffer_mutex);
	memcpy(&flow_info->vcrtcm_cursor, vcrtcm_cursor,
			sizeof(struct vcrtcm_cursor));

	/* calculate the push buffer size for cursor */
	size_in_bytes =
			flow_info->vcrtcm_cursor.height *
			flow_info->vcrtcm_cursor.width *
			(flow_info->vcrtcm_cursor.bpp >> 3);

	requested_num_pages = size_in_bytes / PAGE_SIZE;
	if (size_in_bytes % PAGE_SIZE)
			requested_num_pages++;

	BUG_ON(flow_info->pbd_cursor[0].num_pages !=
			flow_info->pbd_cursor[1].num_pages);

	if (!requested_num_pages) {
		PR_DEBUG("cursor: zero size requested\n");
		udlpim_free_pb(udlpim_info, flow_info,
				UDLPCON_ALLOC_PB_FLAG_CURSOR);
	} else if (flow_info->pbd_cursor[0].num_pages ==
					requested_num_pages) {
		PR_DEBUG("cursor : reusing existing push buffer\n");
	} else {
		/* if we got here, then we either dont have the */
		/* push buffer or we ahve one of the wrong size */
		/* (i.e. cursor size changed) */
		PR_INFO("cursor: allocating push buffer size=%d, "
				"num_pages=%d\n",
				size_in_bytes, requested_num_pages);

		udlpim_free_pb(udlpim_info, flow_info,
				UDLPCON_ALLOC_PB_FLAG_CURSOR);

		r = udlpim_alloc_pb(udlpim_info, flow_info,
				requested_num_pages,
				UDLPCON_ALLOC_PB_FLAG_CURSOR);
	}
	mutex_unlock(&udlpim_info->buffer_mutex);
	return r;
}

int udlpim_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;

	PR_DEBUG("In udlpim_set_cursor, minor %d\n", udlpim_info->minor);

	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_cursor, &flow_info->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));

	return 0;
}

int udlpim_set_dpms(int state, void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;

	PR_DEBUG("in udlpim_set_dpms, minor %d, state %d\n",
			udlpim_info->minor, state);

	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
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

int udlpim_get_dpms(int *state, void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info;

	PR_DEBUG("in udlpim_get_dpms, minor %d\n",
			udlpim_info->minor);

	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	*state = flow_info->dpms_state;

	return 0;
}

int udlpim_connected(void *udlpim_info_, int flow, int *status)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	PR_DEBUG("connected: udlpim_info %p\n", udlpim_info);

	if (udlpim_info->monitor_connected) {
		PR_DEBUG("...connected\n");
		*status = VCRTCM_PCON_CONNECTED;
	} else {
		PR_DEBUG("...not connected\n");
		*status = VCRTCM_PCON_DISCONNECTED;
	}
	return 0;
}

int udlpim_get_modes(void *udlpim_info_, int flow, struct vcrtcm_mode **modes,
		     int *count)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_video_mode *udlpim_video_modes;
	struct vcrtcm_mode *vcrtcm_mode_list =
			udlpim_info->last_vcrtcm_mode_list;
	int udlpim_mode_count = 0;
	int vcrtcm_mode_count = 0;
	int retval = 0;
	int i = 0;

	*modes = NULL;
	*count = 0;

	PR_DEBUG("In udlpim_get_modes\n");

	retval = udlpim_build_modelist(udlpim_info,
			&udlpim_video_modes, &udlpim_mode_count);

	if (retval < 0)
		return retval;

	if (udlpim_mode_count == 0)
		return 0;

	/* If we get this far, we can return modes. */

	/* Erase our old VCRTCM modelist. */
	if (vcrtcm_mode_list) {
		udlpim_kfree(udlpim_info, vcrtcm_mode_list);
		vcrtcm_mode_list = NULL;
		vcrtcm_mode_count = 0;
	}

	/* Build the new vcrtcm_mode list. */
	vcrtcm_mode_list = udlpim_kmalloc(udlpim_info,
				sizeof(struct vcrtcm_mode) * udlpim_mode_count,
				GFP_KERNEL);

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

int udlpim_check_mode(void *udlpim_info_, int flow,
		      struct vcrtcm_mode *mode, int *status)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_video_mode *udlpim_video_modes;
	int udlpim_mode_count = 0;
	int retval;
	int i;

	PR_DEBUG("In udlpim_check_mode\n");

	*status = VCRTCM_MODE_BAD;

	retval = udlpim_build_modelist(udlpim_info,
			&udlpim_video_modes, &udlpim_mode_count);

	if (retval < 0)
		return retval;

	if (udlpim_mode_count == 0)
		return 0;

	PR_DEBUG("udlpim_check_mode: checking %dx%d@%d\n",
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

void udlpim_disable(void *udlpim_info_, int flow)
{
	struct udlpim_info *udlpim_info = (struct udlpim_info *) udlpim_info_;
	struct udlpim_flow_info *flow_info =
			udlpim_info->udlpim_flow_info;

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

	PR_DEBUG("vblank fake, minor=%d\n", udlpim_info->minor);
	udlpim_fake_vblank_slack_sane =
			(udlpim_fake_vblank_slack_sane <= 0) ? 0 : udlpim_fake_vblank_slack;

	if (!udlpim_info) {
		PR_ERR("udlpim_fake_vblank: Cannot find udlpim_info\n");
		return;
	}

	flow_info = udlpim_info->udlpim_flow_info;

	if (!flow_info) {
		PR_ERR("udlpim_fake_vblank: Cannot find pcon descriptor\n");
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
			PR_WARN("dup fake vblank, minor %d\n",
				udlpim_info->minor);
	} else
		PR_DEBUG("Next fake vblank not scheduled\n");

	return;
}

int udlpim_do_xmit_fb_push(struct udlpim_flow_info *flow_info)
{
	struct udlpim_info *udlpim_info = flow_info->udlpim_info;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	PR_DEBUG("in udlpim_do_xmit_fb_push, minor %d\n", udlpim_info->minor);

	spin_lock_irqsave(&udlpim_info->udlpim_lock, flags);
	udlpim_info->status |= UDLPCON_IN_DO_XMIT;
	spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

	push_buffer_index = flow_info->push_buffer_index;

	if (flow_info->pbd_fb[push_buffer_index].num_pages) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		PR_WARN("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((flow_info->fb_force_xmit ||
	     time_after(jiffies_snapshot, flow_info->last_xmit_jiffies +
			UDLPCON_XMIT_HARD_DEADLINE)) &&
			have_push_buffer &&
			udlpim_info->monitor_connected &&
			flow_info->fb_xmit_allowed) {
		/* Someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 * We also check to see if we have a monitor connected, and if
		 * we are allowed to transmit.
		 */

		PR_DEBUG("transmission happening...\n");
		flow_info->fb_force_xmit = 0;
		flow_info->last_xmit_jiffies = jiffies;
		flow_info->fb_xmit_counter++;

		PR_DEBUG("udlpim_do_xmit_fb_push[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				flow_info->vcrtcm_fb.pitch,
				flow_info->vcrtcm_fb.width,
				flow_info->vcrtcm_fb.height,
				flow_info->vcrtcm_fb.bpp);

		PR_DEBUG("udlpim_do_xmit_fb_push[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				flow_info->vcrtcm_fb.viewport_x,
				flow_info->vcrtcm_fb.viewport_y,
				flow_info->vcrtcm_fb.hdisplay,
				flow_info->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&udlpim_info->udlpim_lock, flags);
		udlpim_info->status &= ~UDLPCON_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

		r = vcrtcm_p_push(flow_info->vcrtcm_pcon_info,
				&flow_info->pbd_fb[push_buffer_index],
				&flow_info->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_p_emulate_vblank(flow_info->vcrtcm_pcon_info);
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
		udlpim_info->status &= ~UDLPCON_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlpim_info->udlpim_lock, flags);

		vcrtcm_p_emulate_vblank(flow_info->vcrtcm_pcon_info);
		PR_DEBUG("transmission not happening\n");
	}

	if (flow_info->pb_needs_xmit[push_buffer_index]) {
		unsigned long jiffies_snapshot;
		PR_DEBUG("udlpim_do_xmit_fb_push[%d]: initiating USB transfer\n",
				push_buffer_index);

		udlpim_info->main_buffer = flow_info->pb_fb[push_buffer_index];
		udlpim_info->cursor = flow_info->pb_cursor[push_buffer_index];

		jiffies_snapshot = jiffies;
		udlpim_transmit_framebuffer(udlpim_info);
		PR_DEBUG("transmit over USB took %u ms\n", jiffies_to_msecs(jiffies - jiffies_snapshot));

		flow_info->pb_needs_xmit[push_buffer_index] = 0;
	}

	return r;
}
