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

#include "udlctd.h"
#include "udlctd_vcrtcm.h"
#include "udlctd_utils.h"


static void udlctd_free_pb(struct udlctd_info *udlctd_info,
		struct udlctd_vcrtcm_hal_descriptor *uvhd, int flag)
{
	int i;

	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;

	for (i = 0; i < 2; i++) {
		if (flag == UDLCTD_ALLOC_PB_FLAG_FB) {
			pbd = &uvhd->pbd_fb[i];
			pb_mapped_ram = uvhd->pb_fb[i];
		} else {
			pbd = &uvhd->pbd_cursor[i];
			pb_mapped_ram = uvhd->pb_cursor[i];
		}

		if (pbd->num_pages) {
			BUG_ON(!pbd->gpu_private);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_push_buffer_free(uvhd->vcrtcm_dev_hal, pbd);
		}
	}
}

static int udlctd_alloc_pb(struct udlctd_info *udlctd_info,
		struct udlctd_vcrtcm_hal_descriptor *uvhd,
		int requested_num_pages, int flag)
{
	int i;
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd = NULL;

	for (i = 0; i < 2; i++) {
		if (flag == UDLCTD_ALLOC_PB_FLAG_FB)
			pbd = &uvhd->pbd_fb[i];
		else
			pbd = &uvhd->pbd_cursor[i];

		pbd->num_pages = requested_num_pages;
		r = vcrtcm_push_buffer_alloc(uvhd->vcrtcm_dev_hal, pbd);
		if (r) {
			PR_ERR("%s[%d]: push buffer alloc_failed\n",
					UDLCTD_ALLOC_PB_STRING(flag), i);
			memset(pbd, 0,
				sizeof(struct vcrtcm_push_buffer_descriptor));
			break;
		}

		if (pbd->num_pages != requested_num_pages) {
			PR_ERR("%s[%d]: incorrect size allocated\n",
					UDLCTD_ALLOC_PB_STRING(flag), i);
			vcrtcm_push_buffer_free(uvhd->vcrtcm_dev_hal, pbd);
			/* incorrect size in most cases means too few pages */
			/* so it makes sense to return ENOMEM here */
			r = -ENOMEM;
			break;
		}

		uvhd->pb_needs_xmit[i] = 0;
		PR_DEBUG("%s[%d]: allocated %lu pages\n",
				UDLCTD_ALLOC_PB_STRING(flag),
				i, pbd->num_pages);

		/* we have the buffer, now we need to map it */
		/* (and that can fail too) */
		if (flag == UDLCTD_ALLOC_PB_FLAG_FB) {
			uvhd->pb_fb[i] =
				vm_map_ram(uvhd->pbd_fb[i].pages,
					uvhd->pbd_fb[i].num_pages,
					0, PAGE_KERNEL);

			if (uvhd->pb_fb[i] == NULL) {
				/* If we couldn't map it, we need to */
				/* free the buffer */
				vcrtcm_push_buffer_free(uvhd->vcrtcm_dev_hal,
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
				vcrtcm_push_buffer_free(uvhd->vcrtcm_dev_hal,
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
		if (flag == UDLCTD_ALLOC_PB_FLAG_FB) {
			vm_unmap_ram(uvhd->pbd_fb[0].pages,
				uvhd->pbd_fb[0].num_pages);
			vcrtcm_push_buffer_free(uvhd->vcrtcm_dev_hal,
						&uvhd->pbd_fb[0]);
		} else {
			vm_unmap_ram(uvhd->pbd_cursor[0].pages,
					uvhd->pbd_cursor[0].num_pages);
			vcrtcm_push_buffer_free(uvhd->vcrtcm_dev_hal,
						&uvhd->pbd_cursor[0]);
		}
	}

	return r;
}

int udlctd_attach(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;

	PR_INFO("Attaching udlctd %d to HAL %p\n",
		udlctd_info->minor, vcrtcm_dev_hal);

	if (udlctd_info->udlctd_vcrtcm_hal_descriptor) {
		PR_ERR("attach: minor already served\n");
		return -EBUSY;
	} else {
		struct udlctd_vcrtcm_hal_descriptor
			*uvhd = udlctd_kzalloc(udlctd_info,
				sizeof(struct udlctd_vcrtcm_hal_descriptor),
				GFP_KERNEL);
		if (uvhd == NULL) {
			PR_ERR("attach: no memory\n");
			return -ENOMEM;
		}

		uvhd->udlctd_info = udlctd_info;
		uvhd->vcrtcm_dev_hal = vcrtcm_dev_hal;
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

		udlctd_info->udlctd_vcrtcm_hal_descriptor =
			uvhd;

		/* Do an initial query of the EDID */
		udlctd_query_edid_core(udlctd_info);

		/* Start the EDID query thread */
		queue_delayed_work(udlctd_info->workqueue,
					&udlctd_info->query_edid_work, 0);

		PR_INFO("udlctd %d now serves HAL %p\n", udlctd_info->minor,
			vcrtcm_dev_hal);

		return 0;
	}
}

void udlctd_detach(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;

	PR_INFO("Detaching udlctd %d from HAL %p\n",
		udlctd_info->minor, vcrtcm_dev_hal);

	vcrtcm_gpu_sync(vcrtcm_dev_hal);
	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	cancel_delayed_work_sync(&udlctd_info->fake_vblank_work);
	cancel_delayed_work_sync(&udlctd_info->query_edid_work);

	if (uvhd->vcrtcm_dev_hal == vcrtcm_dev_hal) {
		PR_DEBUG("Found descriptor that should be removed.\n");

		udlctd_free_pb(udlctd_info, uvhd, UDLCTD_ALLOC_PB_FLAG_FB);
		udlctd_free_pb(udlctd_info, uvhd, UDLCTD_ALLOC_PB_FLAG_CURSOR);

		udlctd_info->udlctd_vcrtcm_hal_descriptor = NULL;
		udlctd_kfree(udlctd_info, uvhd);

		udlctd_info->main_buffer = NULL;
		udlctd_info->cursor = NULL;
		udlctd_info->hline_16 = NULL;
		udlctd_info->hline_8 = NULL;
	}
}

int udlctd_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;
	struct udlctd_video_mode *udlctd_video_modes;
	int udlctd_mode_count = 0;
	int found_mode = 0;
	int r = 0;
	int i = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In udlctd_set_fb, minor %d.\n", udlctd_info->minor);

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	/* TODO: Do we need this? */
	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlctd_info->buffer_mutex);
	memcpy(&uvhd->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));

	udlctd_build_modelist(udlctd_info,
			&udlctd_video_modes, &udlctd_mode_count);

	/* Find a matching video mode and switch the DL device to that mode */
	for (i = 0; i < udlctd_mode_count; i++) {
		PR_DEBUG("set_fb checking %dx%d\n",
				udlctd_video_modes[i].xres,
				udlctd_video_modes[i].yres);
		PR_DEBUG("against %dx%d\n",
				vcrtcm_fb->hdisplay, vcrtcm_fb->vdisplay);
		if (udlctd_video_modes[i].xres == vcrtcm_fb->hdisplay &&
			udlctd_video_modes[i].yres == vcrtcm_fb->vdisplay) {
			/* If the modes match */
			udlctd_info->bpp = vcrtcm_fb->bpp;
			udlctd_setup_screen(udlctd_info,
					&udlctd_video_modes[i], vcrtcm_fb);
			found_mode = 1;
			uvhd->fb_xmit_allowed = 1;
			break;
		}
	}

	udlctd_free_modelist(udlctd_info, udlctd_video_modes);

	if (!found_mode) {
		PR_ERR("could not find matching mode...\n");
		uvhd->fb_xmit_allowed = 0;
		udlctd_error_screen(udlctd_info);
		mutex_unlock(&udlctd_info->buffer_mutex);
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
		udlctd_free_pb(udlctd_info, uvhd,
				UDLCTD_ALLOC_PB_FLAG_FB);
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
		udlctd_free_pb(udlctd_info, uvhd,
				UDLCTD_ALLOC_PB_FLAG_FB);

		r = udlctd_alloc_pb(udlctd_info, uvhd,
				requested_num_pages,
				UDLCTD_ALLOC_PB_FLAG_FB);
	}
	mutex_unlock(&udlctd_info->buffer_mutex);
	return r;
}

int udlctd_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("In udlctd_get_fb, minor %d.\n", udlctd_info->minor);
	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_fb, &uvhd->vcrtcm_fb, sizeof(struct vcrtcm_fb));

	return 0;
}

int udlctd_xmit_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("in udlctd_xmit_fb, minor %d\n", udlctd_info->minor);

	/* just mark the "force" flag, udlctd_do_xmit_fb_pull
	 * does the rest (when called).
	 */

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (uvhd)
		uvhd->fb_force_xmit = 1;

	return 0;
}

int udlctd_wait_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	return 0;
}

int udlctd_get_fb_status(struct drm_crtc *drm_crtc,
		void *hw_drv_info, int flow, u32 *status)
{
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	unsigned long flags;

	PR_DEBUG("Queried for status\n");

	spin_lock_irqsave(&udlctd_info->udlctd_lock, flags);
	if (udlctd_info->status & UDLCTD_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&udlctd_info->udlctd_lock, flags);

	*status = tmp_status;

	return 0;
}


int udlctd_set_fps(int fps, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;
	unsigned long jiffies_snapshot;

	PR_DEBUG("udlctd_set_fps, fps %d.\n", fps);

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	if (fps > UDLCTD_FPS_HARD_LIMIT) {
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
	/*schedule_delayed_work(&udlctd_info->fake_vblank_work, 0);*/
	queue_delayed_work(udlctd_info->workqueue, &udlctd_info->fake_vblank_work, 0);

	return 0;
}

int udlctd_get_fps(int *fps, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("udlctd_get_fps.\n");

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
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

int udlctd_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;
	int r = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In udlctd_set_cursor, minor %d\n", udlctd_info->minor);

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&udlctd_info->buffer_mutex);
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
		udlctd_free_pb(udlctd_info, uvhd,
				UDLCTD_ALLOC_PB_FLAG_CURSOR);
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

		udlctd_free_pb(udlctd_info, uvhd,
				UDLCTD_ALLOC_PB_FLAG_CURSOR);

		r = udlctd_alloc_pb(udlctd_info, uvhd,
				requested_num_pages,
				UDLCTD_ALLOC_PB_FLAG_CURSOR);
	}
	mutex_unlock(&udlctd_info->buffer_mutex);
	return r;
}

int udlctd_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("In udlctd_set_cursor, minor %d\n", udlctd_info->minor);

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_cursor, &uvhd->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));

	return 0;
}

int udlctd_set_dpms(int state, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("in udlctd_set_dpms, minor %d, state %d\n",
			udlctd_info->minor, state);

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	uvhd->dpms_state = state;

	if (state == VCRTCM_DPMS_STATE_ON) {
		udlctd_dpms_wakeup(udlctd_info);
	} else if (state == VCRTCM_DPMS_STATE_OFF) {
		udlctd_dpms_sleep(udlctd_info);
	}

	return 0;
}

int udlctd_get_dpms(int *state, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd;

	PR_DEBUG("in udlctd_get_dpms, minor %d\n",
			udlctd_info->minor);

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	*state = uvhd->dpms_state;

	return 0;
}

int udlctd_connected(void *hw_drv_info, int flow, int *status)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	PR_DEBUG("connected: udlctd_info %p\n", udlctd_info);

	if (udlctd_info->monitor_connected) {
		PR_DEBUG("...connected\n");
		*status = VCRTCM_HAL_CONNECTED;
	} else {
		PR_DEBUG("...not connected\n");
		*status = VCRTCM_HAL_DISCONNECTED;
	}
	return 0;
}

int udlctd_get_modes(void *hw_drv_info, int flow, struct vcrtcm_mode **modes,
		     int *count)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_video_mode *udlctd_video_modes;
	struct vcrtcm_mode *vcrtcm_mode_list =
			udlctd_info->last_vcrtcm_mode_list;
	int udlctd_mode_count = 0;
	int vcrtcm_mode_count = 0;
	int retval = 0;
	int i = 0;

	*modes = NULL;
	*count = 0;

	PR_DEBUG("In udlctd_get_modes\n");

	retval = udlctd_build_modelist(udlctd_info,
			&udlctd_video_modes, &udlctd_mode_count);

	if (retval < 0)
		return retval;

	if (udlctd_mode_count == 0)
		return 0;

	/* If we get this far, we can return modes. */

	/* Erase our old VCRTCM modelist. */
	if (vcrtcm_mode_list) {
		udlctd_kfree(udlctd_info, vcrtcm_mode_list);
		vcrtcm_mode_list = NULL;
		vcrtcm_mode_count = 0;
	}

	/* Build the new vcrtcm_mode list. */
	vcrtcm_mode_list = udlctd_kmalloc(udlctd_info,
				sizeof(struct vcrtcm_mode) * udlctd_mode_count,
				GFP_KERNEL);

	/* Copy the udlctd_video_mode list to the vcrtcm_mode list. */
	for (i = 0; i < udlctd_mode_count; i++) {
		vcrtcm_mode_list[i].w = udlctd_video_modes[i].xres;
		vcrtcm_mode_list[i].h = udlctd_video_modes[i].yres;
		vcrtcm_mode_list[i].refresh = udlctd_video_modes[i].refresh;
	}

	vcrtcm_mode_count = udlctd_mode_count;
	udlctd_info->last_vcrtcm_mode_list = vcrtcm_mode_list;

	*modes = vcrtcm_mode_list;
	*count = vcrtcm_mode_count;

	udlctd_free_modelist(udlctd_info, udlctd_video_modes);

	return 0;
}

int udlctd_check_mode(void *hw_drv_info, int flow,
		      struct vcrtcm_mode *mode, int *status)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_video_mode *udlctd_video_modes;
	int udlctd_mode_count = 0;
	int retval;
	int i;

	PR_DEBUG("In udlctd_check_mode\n");

	*status = VCRTCM_MODE_BAD;

	retval = udlctd_build_modelist(udlctd_info,
			&udlctd_video_modes, &udlctd_mode_count);

	if (retval < 0)
		return retval;

	if (udlctd_mode_count == 0)
		return 0;

	PR_DEBUG("udlctd_check_mode: checking %dx%d@%d\n",
			mode->w, mode->h, mode->refresh);
	for (i = 0; i < udlctd_mode_count; i++) {
		struct udlctd_video_mode *current_mode;
		current_mode = &udlctd_video_modes[i];
		if (current_mode->xres == mode->w &&
			current_mode->yres == mode->h &&
			current_mode->refresh == mode->refresh) {
			*status = VCRTCM_MODE_OK;
			break;
		}
	}

	udlctd_free_modelist(udlctd_info, udlctd_video_modes);

	return 0;
}

void udlctd_disable(void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *uvhd =
			udlctd_info->udlctd_vcrtcm_hal_descriptor;

	mutex_lock(&udlctd_info->buffer_mutex);
	uvhd->fb_xmit_allowed = 0;
	mutex_unlock(&udlctd_info->buffer_mutex);
}

void udlctd_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct udlctd_info *udlctd_info =
		container_of(delayed_work, struct udlctd_info, fake_vblank_work);
	struct udlctd_vcrtcm_hal_descriptor *uvhd;
	/*static long last_snapshot = 0;*/

	unsigned long jiffies_snapshot = 0;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int udlctd_fake_vblank_slack_sane = 0;

	PR_DEBUG("vblank fake, minor=%d\n", udlctd_info->minor);
	udlctd_fake_vblank_slack_sane =
			(udlctd_fake_vblank_slack_sane <= 0) ? 0 : udlctd_fake_vblank_slack;

	if (!udlctd_info) {
		PR_ERR("udlctd_fake_vblank: Cannot find udlctd_info\n");
		return;
	}

	uvhd = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!uvhd) {
		PR_ERR("udlctd_fake_vblank: Cannot find HAL descriptor\n");
		return;
	}

	jiffies_snapshot = jiffies;

	if (uvhd->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + udlctd_fake_vblank_slack_sane,
				uvhd->next_vblank_jiffies)) {
			uvhd->next_vblank_jiffies +=
					uvhd->fb_xmit_period_jiffies;

			mutex_lock(&udlctd_info->buffer_mutex);
			udlctd_do_xmit_fb_push(uvhd);
			mutex_unlock(&udlctd_info->buffer_mutex);
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
		if (next_vblank_delay <= udlctd_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		if (!queue_delayed_work(udlctd_info->workqueue,
					&udlctd_info->fake_vblank_work,
					next_vblank_delay))
			PR_WARN("dup fake vblank, minor %d\n",
				udlctd_info->minor);
	} else
		PR_DEBUG("Next fake vblank not scheduled\n");

	return;
}

int udlctd_do_xmit_fb_push(struct udlctd_vcrtcm_hal_descriptor *uvhd)
{
	struct udlctd_info *udlctd_info = uvhd->udlctd_info;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	PR_DEBUG("in udlctd_do_xmit_fb_push, minor %d\n", udlctd_info->minor);

	spin_lock_irqsave(&udlctd_info->udlctd_lock, flags);
	udlctd_info->status |= UDLCTD_IN_DO_XMIT;
	spin_unlock_irqrestore(&udlctd_info->udlctd_lock, flags);

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
			UDLCTD_XMIT_HARD_DEADLINE)) &&
			have_push_buffer &&
			udlctd_info->monitor_connected &&
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

		PR_DEBUG("udlctd_do_xmit_fb_push[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				uvhd->vcrtcm_fb.pitch,
				uvhd->vcrtcm_fb.width,
				uvhd->vcrtcm_fb.height,
				uvhd->vcrtcm_fb.bpp);

		PR_DEBUG("udlctd_do_xmit_fb_push[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				uvhd->vcrtcm_fb.viewport_x,
				uvhd->vcrtcm_fb.viewport_y,
				uvhd->vcrtcm_fb.hdisplay,
				uvhd->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&udlctd_info->udlctd_lock, flags);
		udlctd_info->status &= ~UDLCTD_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlctd_info->udlctd_lock, flags);

		r = vcrtcm_push(uvhd->vcrtcm_dev_hal,
				&uvhd->pbd_fb[push_buffer_index],
				&uvhd->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_emulate_vblank(uvhd->vcrtcm_dev_hal);
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
		spin_lock_irqsave(&udlctd_info->udlctd_lock, flags);
		udlctd_info->status &= ~UDLCTD_IN_DO_XMIT;
		spin_unlock_irqrestore(&udlctd_info->udlctd_lock, flags);

		vcrtcm_emulate_vblank(uvhd->vcrtcm_dev_hal);
		PR_DEBUG("transmission not happening\n");
	}

	if (uvhd->pb_needs_xmit[push_buffer_index]) {
		unsigned long jiffies_snapshot;
		PR_DEBUG("udlctd_do_xmit_fb_push[%d]: initiating USB transfer\n",
				push_buffer_index);

		udlctd_info->main_buffer = uvhd->pb_fb[push_buffer_index];
		udlctd_info->cursor = uvhd->pb_cursor[push_buffer_index];

		jiffies_snapshot = jiffies;
		udlctd_transmit_framebuffer(udlctd_info);
		PR_DEBUG("transmit over USB took %u ms\n", jiffies_to_msecs(jiffies - jiffies_snapshot));

		uvhd->pb_needs_xmit[push_buffer_index] = 0;
	}

	return r;
}
