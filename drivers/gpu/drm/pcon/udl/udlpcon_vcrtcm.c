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

#include "udlpcon.h"
#include "udlpcon_vcrtcm.h"
#include "udlpcon_utils.h"


static void udlpcon_free_pb(struct udlpcon_info *udlpcon_info,
		struct udlpcon_vcrtcm_hal_descriptor *uvhd, int flag)
{
	int i;

	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;

	for (i = 0; i < 2; i++) {
		if (flag == UDLPCON_ALLOC_PB_FLAG_FB) {
			pbd = &uvhd->pbd_fb[i];
			pb_mapped_ram = uvhd->pb_fb[i];
		} else {
			pbd = &uvhd->pbd_cursor[i];
			pb_mapped_ram = uvhd->pb_cursor[i];
		}

		if (pbd->num_pages) {
			BUG_ON(!pbd->gpu_private);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_push_buffer_free(uvhd->vcrtcm_pcon_info, pbd);
		}
	}
}

static int udlpcon_alloc_pb(struct udlpcon_info *udlpcon_info,
		struct udlpcon_vcrtcm_hal_descriptor *uvhd,
		int requested_num_pages, int flag)
{
	int i;
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd = NULL;

	for (i = 0; i < 2; i++) {
		if (flag == UDLPCON_ALLOC_PB_FLAG_FB)
			pbd = &uvhd->pbd_fb[i];
		else
			pbd = &uvhd->pbd_cursor[i];

		pbd->num_pages = requested_num_pages;
		r = vcrtcm_push_buffer_alloc(uvhd->vcrtcm_pcon_info, pbd);
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
			vcrtcm_push_buffer_free(uvhd->vcrtcm_pcon_info, pbd);
			/* incorrect size in most cases means too few pages */
			/* so it makes sense to return ENOMEM here */
			r = -ENOMEM;
			break;
		}

		uvhd->pb_needs_xmit[i] = 0;
		PR_DEBUG("%s[%d]: allocated %lu pages\n",
				UDLPCON_ALLOC_PB_STRING(flag),
				i, pbd->num_pages);

		/* we have the buffer, now we need to map it */
		/* (and that can fail too) */
		if (flag == UDLPCON_ALLOC_PB_FLAG_FB) {
			uvhd->pb_fb[i] =
				vm_map_ram(uvhd->pbd_fb[i].pages,
					uvhd->pbd_fb[i].num_pages,
					0, PAGE_KERNEL);

			if (uvhd->pb_fb[i] == NULL) {
				/* If we couldn't map it, we need to */
				/* free the buffer */
				vcrtcm_push_buffer_free(uvhd->vcrtcm_pcon_info,
							&uvhd->pbd_fb[i]);
				/* TODO: Is this right to return ENOMEM? */
				r = -ENOMEM;
				break;
			}
		} else {
			uvhd->pb_cursor[i] =
				vm_map_ram(uvhd->pbd_cursor[i].pages,
					uvhd->pbd_cursor[i].num_pages,
					0, PAGE_KERNEL);

			if (uvhd->pb_cursor[i] == NULL) {
				/* If we couldn't map it, we need to */
				/* free the buffer */
				vcrtcm_push_buffer_free(uvhd->vcrtcm_pcon_info,
							&uvhd->pbd_cursor[i]);
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
			vm_unmap_ram(uvhd->pbd_fb[0].pages,
				uvhd->pbd_fb[0].num_pages);
			vcrtcm_push_buffer_free(uvhd->vcrtcm_pcon_info,
						&uvhd->pbd_fb[0]);
		} else {
			vm_unmap_ram(uvhd->pbd_cursor[0].pages,
					uvhd->pbd_cursor[0].num_pages);
			vcrtcm_push_buffer_free(uvhd->vcrtcm_pcon_info,
						&uvhd->pbd_cursor[0]);
		}
	}

	return r;
}

int udlpcon_attach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;

	PR_INFO("Attaching udlpcon %d to pcon %p\n",
		udlpcon_info->minor, vcrtcm_pcon_info);

	if (udlpcon_info->udlpcon_vcrtcm_hal_descriptor) {
		PR_ERR("attach: minor already served\n");
		return -EBUSY;
	} else {
		struct udlpcon_vcrtcm_hal_descriptor
			*uvhd = udlpcon_kzalloc(udlpcon_info,
				sizeof(struct udlpcon_vcrtcm_hal_descriptor),
				GFP_KERNEL);
		if (uvhd == NULL) {
			PR_ERR("attach: no memory\n");
			return -ENOMEM;
		}

		uvhd->udlpcon_info = udlpcon_info;
		uvhd->vcrtcm_pcon_info = vcrtcm_pcon_info;
		uvhd->fb_force_xmit = 0;
		uvhd->fb_xmit_allowed = 0;
		uvhd->fb_xmit_counter = 0;
		uvhd->fb_xmit_period_jiffies = 0;
		uvhd->next_vblank_jiffies = 0;
		uvhd->push_buffer_index = 0;
		uvhd->pb_needs_xmit[0] = 0;
		uvhd->pb_needs_xmit[1] = 0;
		memset(&uvhd->pbd_fb, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		memset(&uvhd->pbd_cursor, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		uvhd->pb_fb[0] = 0;
		uvhd->pb_fb[1] = 0;
		uvhd->pb_cursor[0] = 0;
		uvhd->pb_cursor[1] = 0;

		uvhd->vcrtcm_cursor.flag =
			VCRTCM_CURSOR_FLAG_HIDE;

		udlpcon_info->udlpcon_vcrtcm_hal_descriptor =
			uvhd;

		/* Do an initial query of the EDID */
		udlpcon_query_edid_core(udlpcon_info);

		/* Start the EDID query thread */
		queue_delayed_work(udlpcon_info->workqueue,
					&udlpcon_info->query_edid_work, 0);

		PR_INFO("udlpcon %d now serves pcon %p\n", udlpcon_info->minor,
			vcrtcm_pcon_info);

		return 0;
	}
}

void udlpcon_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;

	PR_INFO("Detaching udlpcon %d from pcon %p\n",
		udlpcon_info->minor, vcrtcm_pcon_info);

	vcrtcm_gpu_sync(vcrtcm_pcon_info);
	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	cancel_delayed_work_sync(&udlpcon_info->fake_vblank_work);
	cancel_delayed_work_sync(&udlpcon_info->query_edid_work);

	if (uvhd->vcrtcm_pcon_info == vcrtcm_pcon_info) {
		PR_DEBUG("Found descriptor that should be removed.\n");

		udlpcon_free_pb(udlpcon_info, uvhd, UDLPCON_ALLOC_PB_FLAG_FB);
		udlpcon_free_pb(udlpcon_info, uvhd, UDLPCON_ALLOC_PB_FLAG_CURSOR);

		udlpcon_info->udlpcon_vcrtcm_hal_descriptor = NULL;
		udlpcon_kfree(udlpcon_info, uvhd);

		udlpcon_info->main_buffer = NULL;
		udlpcon_info->cursor = NULL;
		udlpcon_info->hline_16 = NULL;
		udlpcon_info->hline_8 = NULL;
	}
}

int udlpcon_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;
	struct udlpcon_video_mode *udlpcon_video_modes;
	int udlpcon_mode_count = 0;
	int found_mode = 0;
	int r = 0;
	int i = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In udlpcon_set_fb, minor %d.\n", udlpcon_info->minor);

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	/* TODO: Do we need this? */
	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpcon_info->buffer_mutex);
	memcpy(&uvhd->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));

	udlpcon_build_modelist(udlpcon_info,
			&udlpcon_video_modes, &udlpcon_mode_count);

	/* Find a matching video mode and switch the DL device to that mode */
	for (i = 0; i < udlpcon_mode_count; i++) {
		PR_DEBUG("set_fb checking %dx%d\n",
				udlpcon_video_modes[i].xres,
				udlpcon_video_modes[i].yres);
		PR_DEBUG("against %dx%d\n",
				vcrtcm_fb->hdisplay, vcrtcm_fb->vdisplay);
		if (udlpcon_video_modes[i].xres == vcrtcm_fb->hdisplay &&
			udlpcon_video_modes[i].yres == vcrtcm_fb->vdisplay) {
			/* If the modes match */
			udlpcon_info->bpp = vcrtcm_fb->bpp;
			udlpcon_setup_screen(udlpcon_info,
					&udlpcon_video_modes[i], vcrtcm_fb);
			found_mode = 1;
			uvhd->fb_xmit_allowed = 1;
			break;
		}
	}

	udlpcon_free_modelist(udlpcon_info, udlpcon_video_modes);

	if (!found_mode) {
		PR_ERR("could not find matching mode...\n");
		uvhd->fb_xmit_allowed = 0;
		udlpcon_error_screen(udlpcon_info);
		mutex_unlock(&udlpcon_info->buffer_mutex);
		return 0;
	}

	size_in_bytes = uvhd->vcrtcm_fb.pitch *
			uvhd->vcrtcm_fb.vdisplay;

	requested_num_pages = size_in_bytes / PAGE_SIZE;
	if (size_in_bytes % PAGE_SIZE)
		requested_num_pages++;

	BUG_ON(uvhd->pbd_fb[0].num_pages != uvhd->pbd_fb[1].num_pages);

	if (!requested_num_pages) {
		PR_DEBUG("framebuffer: zero size requested\n");
		udlpcon_free_pb(udlpcon_info, uvhd,
				UDLPCON_ALLOC_PB_FLAG_FB);
	} else if (uvhd->pbd_fb[0].num_pages == requested_num_pages) {
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
		udlpcon_free_pb(udlpcon_info, uvhd,
				UDLPCON_ALLOC_PB_FLAG_FB);

		r = udlpcon_alloc_pb(udlpcon_info, uvhd,
				requested_num_pages,
				UDLPCON_ALLOC_PB_FLAG_FB);
	}
	mutex_unlock(&udlpcon_info->buffer_mutex);
	return r;
}

int udlpcon_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("In udlpcon_get_fb, minor %d.\n", udlpcon_info->minor);
	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_fb, &uvhd->vcrtcm_fb, sizeof(struct vcrtcm_fb));

	return 0;
}

int udlpcon_xmit_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("in udlpcon_xmit_fb, minor %d\n", udlpcon_info->minor);

	/* just mark the "force" flag, udlpcon_do_xmit_fb_pull
	 * does the rest (when called).
	 */

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (uvhd)
		uvhd->fb_force_xmit = 1;

	return 0;
}

int udlpcon_wait_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	return 0;
}

int udlpcon_get_fb_status(struct drm_crtc *drm_crtc,
		void *hw_drv_info, int flow, u32 *status)
{
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	unsigned long flags;

	PR_DEBUG("Queried for status\n");

	spin_lock_irqsave(&udlpcon_info->udlpcon_lock, flags);
	if (udlpcon_info->status & UDLPCON_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&udlpcon_info->udlpcon_lock, flags);

	*status = tmp_status;

	return 0;
}


int udlpcon_set_fps(int fps, void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;
	unsigned long jiffies_snapshot;

	PR_DEBUG("udlpcon_set_fps, fps %d.\n", fps);

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (fps > UDLPCON_FPS_HARD_LIMIT) {
		PR_ERR("Frame rate above the hard limit\n");
		return -EINVAL;
	}

	if (fps <= 0) {
		uvhd->fb_xmit_period_jiffies = 0;
		PR_INFO("Transmission disabled, (negative or zero fps).\n");
	} else {
		uvhd->fb_xmit_period_jiffies = HZ / fps;
		jiffies_snapshot = jiffies;
		uvhd->last_xmit_jiffies = jiffies_snapshot;
		uvhd->fb_force_xmit = 1;
		uvhd->next_vblank_jiffies =
			jiffies_snapshot + uvhd->fb_xmit_period_jiffies;

		PR_INFO("Frame transmission period set to %d jiffies\n",
			HZ / fps);
	}

	/* Schedule initial fake vblank */
	/*schedule_delayed_work(&udlpcon_info->fake_vblank_work, 0);*/
	queue_delayed_work(udlpcon_info->workqueue, &udlpcon_info->fake_vblank_work, 0);

	return 0;
}

int udlpcon_get_fps(int *fps, void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("udlpcon_get_fps.\n");

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (uvhd->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		PR_INFO
		("Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ / uvhd->fb_xmit_period_jiffies;
		return 0;
	}
}

int udlpcon_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;
	int r = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In udlpcon_set_cursor, minor %d\n", udlpcon_info->minor);

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlpcon_info->buffer_mutex);
	memcpy(&uvhd->vcrtcm_cursor, vcrtcm_cursor,
			sizeof(struct vcrtcm_cursor));

	/* calculate the push buffer size for cursor */
	size_in_bytes =
			uvhd->vcrtcm_cursor.height *
			uvhd->vcrtcm_cursor.width *
			(uvhd->vcrtcm_cursor.bpp >> 3);

	requested_num_pages = size_in_bytes / PAGE_SIZE;
	if (size_in_bytes % PAGE_SIZE)
			requested_num_pages++;

	BUG_ON(uvhd->pbd_cursor[0].num_pages !=
			uvhd->pbd_cursor[1].num_pages);

	if (!requested_num_pages) {
		PR_DEBUG("cursor: zero size requested\n");
		udlpcon_free_pb(udlpcon_info, uvhd,
				UDLPCON_ALLOC_PB_FLAG_CURSOR);
	} else if (uvhd->pbd_cursor[0].num_pages ==
					requested_num_pages) {
		PR_DEBUG("cursor : reusing existing push buffer\n");
	} else {
		/* if we got here, then we either dont have the */
		/* push buffer or we ahve one of the wrong size */
		/* (i.e. cursor size changed) */
		PR_INFO("cursor: allocating push buffer size=%d, "
				"num_pages=%d\n",
				size_in_bytes, requested_num_pages);

		udlpcon_free_pb(udlpcon_info, uvhd,
				UDLPCON_ALLOC_PB_FLAG_CURSOR);

		r = udlpcon_alloc_pb(udlpcon_info, uvhd,
				requested_num_pages,
				UDLPCON_ALLOC_PB_FLAG_CURSOR);
	}
	mutex_unlock(&udlpcon_info->buffer_mutex);
	return r;
}

int udlpcon_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("In udlpcon_set_cursor, minor %d\n", udlpcon_info->minor);

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_cursor, &uvhd->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));

	return 0;
}

int udlpcon_set_dpms(int state, void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("in udlpcon_set_dpms, minor %d, state %d\n",
			udlpcon_info->minor, state);

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	uvhd->dpms_state = state;

	if (state == VCRTCM_DPMS_STATE_ON) {
		udlpcon_dpms_wakeup(udlpcon_info);
	} else if (state == VCRTCM_DPMS_STATE_OFF) {
		udlpcon_dpms_sleep(udlpcon_info);
	}

	return 0;
}

int udlpcon_get_dpms(int *state, void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("in udlpcon_get_dpms, minor %d\n",
			udlpcon_info->minor);

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	*state = uvhd->dpms_state;

	return 0;
}

int udlpcon_connected(void *hw_drv_info, int flow, int *status)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	PR_DEBUG("connected: udlpcon_info %p\n", udlpcon_info);

	if (udlpcon_info->monitor_connected) {
		PR_DEBUG("...connected\n");
		*status = VCRTCM_PCON_CONNECTED;
	} else {
		PR_DEBUG("...not connected\n");
		*status = VCRTCM_PCON_DISCONNECTED;
	}
	return 0;
}

int udlpcon_get_modes(void *hw_drv_info, int flow, struct vcrtcm_mode **modes,
		     int *count)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_video_mode *udlpcon_video_modes;
	struct vcrtcm_mode *vcrtcm_mode_list =
			udlpcon_info->last_vcrtcm_mode_list;
	int udlpcon_mode_count = 0;
	int vcrtcm_mode_count = 0;
	int retval = 0;
	int i = 0;

	*modes = NULL;
	*count = 0;

	PR_DEBUG("In udlpcon_get_modes\n");

	retval = udlpcon_build_modelist(udlpcon_info,
			&udlpcon_video_modes, &udlpcon_mode_count);

	if (retval < 0)
		return retval;

	if (udlpcon_mode_count == 0)
		return 0;

	/* If we get this far, we can return modes. */

	/* Erase our old VCRTCM modelist. */
	if (vcrtcm_mode_list) {
		udlpcon_kfree(udlpcon_info, vcrtcm_mode_list);
		vcrtcm_mode_list = NULL;
		vcrtcm_mode_count = 0;
	}

	/* Build the new vcrtcm_mode list. */
	vcrtcm_mode_list = udlpcon_kmalloc(udlpcon_info,
				sizeof(struct vcrtcm_mode) * udlpcon_mode_count,
				GFP_KERNEL);

	/* Copy the udlpcon_video_mode list to the vcrtcm_mode list. */
	for (i = 0; i < udlpcon_mode_count; i++) {
		vcrtcm_mode_list[i].w = udlpcon_video_modes[i].xres;
		vcrtcm_mode_list[i].h = udlpcon_video_modes[i].yres;
		vcrtcm_mode_list[i].refresh = udlpcon_video_modes[i].refresh;
	}

	vcrtcm_mode_count = udlpcon_mode_count;
	udlpcon_info->last_vcrtcm_mode_list = vcrtcm_mode_list;

	*modes = vcrtcm_mode_list;
	*count = vcrtcm_mode_count;

	udlpcon_free_modelist(udlpcon_info, udlpcon_video_modes);

	return 0;
}

int udlpcon_check_mode(void *hw_drv_info, int flow,
		      struct vcrtcm_mode *mode, int *status)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_video_mode *udlpcon_video_modes;
	int udlpcon_mode_count = 0;
	int retval;
	int i;

	PR_DEBUG("In udlpcon_check_mode\n");

	*status = VCRTCM_MODE_BAD;

	retval = udlpcon_build_modelist(udlpcon_info,
			&udlpcon_video_modes, &udlpcon_mode_count);

	if (retval < 0)
		return retval;

	if (udlpcon_mode_count == 0)
		return 0;

	PR_DEBUG("udlpcon_check_mode: checking %dx%d@%d\n",
			mode->w, mode->h, mode->refresh);
	for (i = 0; i < udlpcon_mode_count; i++) {
		struct udlpcon_video_mode *current_mode;
		current_mode = &udlpcon_video_modes[i];
		if (current_mode->xres == mode->w &&
			current_mode->yres == mode->h &&
			current_mode->refresh == mode->refresh) {
			*status = VCRTCM_MODE_OK;
			break;
		}
	}

	udlpcon_free_modelist(udlpcon_info, udlpcon_video_modes);

	return 0;
}

void udlpcon_disable(void *hw_drv_info, int flow)
{
	struct udlpcon_info *udlpcon_info = (struct udlpcon_info *) hw_drv_info;
	struct udlpcon_vcrtcm_hal_descriptor *uvhd =
			udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	mutex_lock(&udlpcon_info->buffer_mutex);
	uvhd->fb_xmit_allowed = 0;
	mutex_unlock(&udlpcon_info->buffer_mutex);
}

void udlpcon_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct udlpcon_info *udlpcon_info =
		container_of(delayed_work, struct udlpcon_info, fake_vblank_work);
	struct udlpcon_vcrtcm_hal_descriptor *uvhd;
	/*static long last_snapshot = 0;*/

	unsigned long jiffies_snapshot = 0;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int udlpcon_fake_vblank_slack_sane = 0;

	PR_DEBUG("vblank fake, minor=%d\n", udlpcon_info->minor);
	udlpcon_fake_vblank_slack_sane =
			(udlpcon_fake_vblank_slack_sane <= 0) ? 0 : udlpcon_fake_vblank_slack;

	if (!udlpcon_info) {
		PR_ERR("udlpcon_fake_vblank: Cannot find udlpcon_info\n");
		return;
	}

	uvhd = udlpcon_info->udlpcon_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("udlpcon_fake_vblank: Cannot find pcon descriptor\n");
		return;
	}

	jiffies_snapshot = jiffies;

	if (uvhd->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + udlpcon_fake_vblank_slack_sane,
				uvhd->next_vblank_jiffies)) {
			uvhd->next_vblank_jiffies +=
					uvhd->fb_xmit_period_jiffies;

			mutex_lock(&udlpcon_info->buffer_mutex);
			udlpcon_do_xmit_fb_push(uvhd);
			mutex_unlock(&udlpcon_info->buffer_mutex);
		}

		if (!next_vblank_jiffies_valid) {
			next_vblank_jiffies = uvhd->next_vblank_jiffies;
			next_vblank_jiffies_valid = 1;
		} else {
			if (time_after_eq(next_vblank_jiffies,
					uvhd->next_vblank_jiffies)) {
				next_vblank_jiffies = uvhd->next_vblank_jiffies;
			}
		}
	}

	if (next_vblank_jiffies_valid) {
		next_vblank_delay =
			(int)next_vblank_jiffies - (int)jiffies_snapshot;
		if (next_vblank_delay <= udlpcon_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		if (!queue_delayed_work(udlpcon_info->workqueue,
					&udlpcon_info->fake_vblank_work,
					next_vblank_delay))
			PR_WARN("dup fake vblank, minor %d\n",
				udlpcon_info->minor);
	} else
		PR_DEBUG("Next fake vblank not scheduled\n");

	return;
}

int udlpcon_do_xmit_fb_push(struct udlpcon_vcrtcm_hal_descriptor *uvhd)
{
	struct udlpcon_info *udlpcon_info = uvhd->udlpcon_info;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	PR_DEBUG("in udlpcon_do_xmit_fb_push, minor %d\n", udlpcon_info->minor);

	spin_lock_irqsave(&udlpcon_info->udlpcon_lock, flags);
	udlpcon_info->status |= UDLPCON_IN_DO_XMIT;
	spin_unlock_irqrestore(&udlpcon_info->udlpcon_lock, flags);

	push_buffer_index = uvhd->push_buffer_index;

	if (uvhd->pbd_fb[push_buffer_index].num_pages) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		PR_WARN("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((uvhd->fb_force_xmit ||
	     time_after(jiffies_snapshot, uvhd->last_xmit_jiffies +
			UDLPCON_XMIT_HARD_DEADLINE)) &&
			have_push_buffer &&
			udlpcon_info->monitor_connected &&
			uvhd->fb_xmit_allowed) {
		/* Someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 * We also check to see if we have a monitor connected, and if
		 * we are allowed to transmit.
		 */

		PR_DEBUG("transmission happening...\n");
		uvhd->fb_force_xmit = 0;
		uvhd->last_xmit_jiffies = jiffies;
		uvhd->fb_xmit_counter++;

		PR_DEBUG("udlpcon_do_xmit_fb_push[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				uvhd->vcrtcm_fb.pitch,
				uvhd->vcrtcm_fb.width,
				uvhd->vcrtcm_fb.height,
				uvhd->vcrtcm_fb.bpp);

		PR_DEBUG("udlpcon_do_xmit_fb_push[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				uvhd->vcrtcm_fb.viewport_x,
				uvhd->vcrtcm_fb.viewport_y,
				uvhd->vcrtcm_fb.hdisplay,
				uvhd->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&udlpcon_info->udlpcon_lock, flags);
		udlpcon_info->status &= ~UDLPCON_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlpcon_info->udlpcon_lock, flags);

		r = vcrtcm_push(uvhd->vcrtcm_pcon_info,
				&uvhd->pbd_fb[push_buffer_index],
				&uvhd->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_emulate_vblank(uvhd->vcrtcm_pcon_info);
		} else {
			/* if push successed, then we need to swap push buffers
			 * and mark the buffer for USB transmission in the next
			 * vblank interval; note that call to vcrtcm_push only
			 * initiates the push request to GPU; when GPU does it
			 * is up to the GPU and doesn't matter as long as it is
			 * within the frame transmission period (otherwise, we'll
			 * see from frame tearing)
			 * If GPU completes the push before the next vblank
			 * interval, then it is perfectly safe to mark the buffer
			 * ready for transmission now because USB transmission wont
			 * look at it until push is complete.
			 */

			uvhd->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			uvhd->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		spin_lock_irqsave(&udlpcon_info->udlpcon_lock, flags);
		udlpcon_info->status &= ~UDLPCON_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlpcon_info->udlpcon_lock, flags);

		vcrtcm_emulate_vblank(uvhd->vcrtcm_pcon_info);
		PR_DEBUG("transmission not happening\n");
	}

	if (uvhd->pb_needs_xmit[push_buffer_index]) {
		unsigned long jiffies_snapshot;
		PR_DEBUG("udlpcon_do_xmit_fb_push[%d]: initiating USB transfer\n",
				push_buffer_index);

		udlpcon_info->main_buffer = uvhd->pb_fb[push_buffer_index];
		udlpcon_info->cursor = uvhd->pb_cursor[push_buffer_index];

		jiffies_snapshot = jiffies;
		udlpcon_transmit_framebuffer(udlpcon_info);
		PR_DEBUG("transmit over USB took %u ms\n", jiffies_to_msecs(jiffies - jiffies_snapshot));

		uvhd->pb_needs_xmit[push_buffer_index] = 0;
	}

	return r;
}
