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

#include "v4l2pcon.h"
#include "v4l2pcon_vcrtcm.h"
#include "v4l2pcon_utils.h"
#include "vcrtcm/vcrtcm_pcon.h"

static void v4l2pcon_free_pb(struct v4l2pcon_info *v4l2pcon_info,
		struct v4l2pcon_vcrtcm_hal_descriptor *vhd, int flag)
{
	int i;

	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;

	for (i = 0; i < 2; i++) {
		if (flag == V4L2PCON_ALLOC_PB_FLAG_FB) {
			pbd = &vhd->pbd_fb[i];
			pb_mapped_ram = vhd->pb_fb[i];
		} else {
			pbd = &vhd->pbd_cursor[i];
			pb_mapped_ram = vhd->pb_cursor[i];
		}

		if (pbd->num_pages) {
			BUG_ON(!pbd->gpu_private);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_push_buffer_free(vhd->vcrtcm_pcon_info, pbd);
		}
	}
}

static int v4l2pcon_alloc_pb(struct v4l2pcon_info *v4l2pcon_info,
		struct v4l2pcon_vcrtcm_hal_descriptor *vhd,
		int requested_num_pages, int flag)
{
	int i;
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd = NULL;

	for (i = 0; i < 2; i++) {
		if (flag == V4L2PCON_ALLOC_PB_FLAG_FB)
			pbd = &vhd->pbd_fb[i];
		else
			pbd = &vhd->pbd_cursor[i];

		pbd->num_pages = requested_num_pages;
		r = vcrtcm_push_buffer_alloc(vhd->vcrtcm_pcon_info, pbd);
		if (r) {
			PR_ERR("%s[%d]: push buffer alloc_failed\n",
					V4L2PCON_ALLOC_PB_STRING(flag), i);
			memset(pbd, 0,
				sizeof(struct vcrtcm_push_buffer_descriptor));
			break;
		}

		if (pbd->num_pages != requested_num_pages) {
			PR_ERR("%s[%d]: incorrect size allocated\n",
					V4L2PCON_ALLOC_PB_STRING(flag), i);
			vcrtcm_push_buffer_free(vhd->vcrtcm_pcon_info, pbd);
			/* incorrect size in most cases means too few pages */
			/* so it makes sense to return ENOMEM here */
			r = -ENOMEM;
			break;
		}

		vhd->pb_needs_xmit[i] = 0;
		PR_DEBUG("%s[%d]: allocated %lu pages\n",
				V4L2PCON_ALLOC_PB_STRING(flag),
				i, pbd->num_pages);

		/* we have the buffer, now we need to map it */
		/* (and that can fail too) */
		if (flag == V4L2PCON_ALLOC_PB_FLAG_FB) {
			vhd->pb_fb[i] =
				vm_map_ram(vhd->pbd_fb[i].pages,
					vhd->pbd_fb[i].num_pages,
					0, PAGE_KERNEL);

			if (vhd->pb_fb[i] == NULL) {
				/* If we couldn't map it, we need to */
				/* free the buffer */
				vcrtcm_push_buffer_free(vhd->vcrtcm_pcon_info,
							&vhd->pbd_fb[i]);
				/* TODO: Is this right to return ENOMEM? */
				r = -ENOMEM;
				break;
			}
		} else {
			vhd->pb_cursor[i] =
				vm_map_ram(vhd->pbd_cursor[i].pages,
					vhd->pbd_cursor[i].num_pages,
					0, PAGE_KERNEL);

			if (vhd->pb_cursor[i] == NULL) {
				/* If we couldn't map it, we need to */
				/* free the buffer */
				vcrtcm_push_buffer_free(vhd->vcrtcm_pcon_info,
							&vhd->pbd_cursor[i]);
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
		if (flag == V4L2PCON_ALLOC_PB_FLAG_FB) {
			vm_unmap_ram(vhd->pbd_fb[0].pages,
				vhd->pbd_fb[0].num_pages);
			vcrtcm_push_buffer_free(vhd->vcrtcm_pcon_info,
						&vhd->pbd_fb[0]);
		} else {
			vm_unmap_ram(vhd->pbd_cursor[0].pages,
					vhd->pbd_cursor[0].num_pages);
			vcrtcm_push_buffer_free(vhd->vcrtcm_pcon_info,
						&vhd->pbd_cursor[0]);
		}
	}

	return r;
}

int v4l2pcon_attach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;

	PR_INFO("Attaching vl2pcon %d to pcon %p\n",
		v4l2pcon_info->minor, vcrtcm_pcon_info);

	if (v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor) {
		PR_ERR("attach: minor already served\n");
		return -EBUSY;
	} else {
		struct v4l2pcon_vcrtcm_hal_descriptor
			*vhd = v4l2pcon_kzalloc(v4l2pcon_info,
				sizeof(struct v4l2pcon_vcrtcm_hal_descriptor),
				GFP_KERNEL);
		if (vhd == NULL) {
			PR_ERR("attach: no memory\n");
			return -ENOMEM;
		}

		vhd->v4l2pcon_info = v4l2pcon_info;
		vhd->vcrtcm_pcon_info = vcrtcm_pcon_info;
		vhd->fb_force_xmit = 0;
		vhd->fb_xmit_allowed = 0;
		vhd->fb_xmit_counter = 0;
		vhd->fb_xmit_period_jiffies = 0;
		vhd->next_vblank_jiffies = 0;
		vhd->push_buffer_index = 0;
		vhd->pb_needs_xmit[0] = 0;
		vhd->pb_needs_xmit[1] = 0;
		memset(&vhd->pbd_fb, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		memset(&vhd->pbd_cursor, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		vhd->pb_fb[0] = 0;
		vhd->pb_fb[1] = 0;
		vhd->pb_cursor[0] = 0;
		vhd->pb_cursor[1] = 0;

		vhd->vcrtcm_cursor.flag = VCRTCM_CURSOR_FLAG_HIDE;

		v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor = vhd;

		PR_INFO("v4l2pcon %d now serves pcon %p\n", v4l2pcon_info->minor,
			vcrtcm_pcon_info);

		return 0;
	}
}

void v4l2pcon_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;

	PR_INFO("Detaching v4l2pcon %d from pcon %p\n",
		v4l2pcon_info->minor, vcrtcm_pcon_info);

	vcrtcm_gpu_sync(vcrtcm_pcon_info);
	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	cancel_delayed_work_sync(&v4l2pcon_info->fake_vblank_work);

	if (vhd->vcrtcm_pcon_info == vcrtcm_pcon_info) {
		PR_DEBUG("Found descriptor that should be removed.\n");

		v4l2pcon_free_pb(v4l2pcon_info, vhd, V4L2PCON_ALLOC_PB_FLAG_FB);
		v4l2pcon_free_pb(v4l2pcon_info, vhd, V4L2PCON_ALLOC_PB_FLAG_CURSOR);

		v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor = NULL;
		v4l2pcon_kfree(v4l2pcon_info, vhd);

		v4l2pcon_info->main_buffer = NULL;
		v4l2pcon_info->cursor = NULL;
	}
}

int v4l2pcon_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;
	uint32_t w, h, sb_size;
	int r = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In v4l2pcon_set_fb, minor %d.\n", v4l2pcon_info->minor);

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	/* TODO: Do we need this? */
	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&v4l2pcon_info->buffer_mutex);
	memcpy(&vhd->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));

	size_in_bytes = vhd->vcrtcm_fb.pitch *
			vhd->vcrtcm_fb.vdisplay;

	requested_num_pages = size_in_bytes / PAGE_SIZE;
	if (size_in_bytes % PAGE_SIZE)
		requested_num_pages++;

	BUG_ON(vhd->pbd_fb[0].num_pages != vhd->pbd_fb[1].num_pages);

	if (!requested_num_pages) {
		PR_DEBUG("framebuffer: zero size requested\n");
		v4l2pcon_free_pb(v4l2pcon_info, vhd,
				V4L2PCON_ALLOC_PB_FLAG_FB);
	} else if (vhd->pbd_fb[0].num_pages == requested_num_pages) {
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
		v4l2pcon_free_pb(v4l2pcon_info, vhd,
				V4L2PCON_ALLOC_PB_FLAG_FB);

		r = v4l2pcon_alloc_pb(v4l2pcon_info, vhd,
				requested_num_pages,
				V4L2PCON_ALLOC_PB_FLAG_FB);
		/* shadowbuf init */
		/* this should get freed later */
		w = vhd->vcrtcm_fb.hdisplay;
		h = vhd->vcrtcm_fb.vdisplay;
		sb_size = w * h * (vhd->vcrtcm_fb.bpp >> 3);
		mutex_lock(&v4l2pcon_info->sb_lock);
		v4l2pcon_alloc_shadowbuf(v4l2pcon_info, sb_size);
		mutex_unlock(&v4l2pcon_info->sb_lock);
	}
	vhd->fb_xmit_allowed = 1;
	mutex_unlock(&v4l2pcon_info->buffer_mutex);
	return r;
}

int v4l2pcon_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;

	PR_DEBUG("In v4l2pcon_get_fb, minor %d.\n", v4l2pcon_info->minor);
	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_fb, &vhd->vcrtcm_fb, sizeof(struct vcrtcm_fb));

	return 0;
}

int v4l2pcon_xmit_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;

	PR_DEBUG("in v4l2pcon_xmit_fb, minor %d\n", v4l2pcon_info->minor);

	/* just mark the "force" flag, v4l2pcon_do_xmit_fb_pull
	 * does the rest (when called).
	 */

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (vhd)
		vhd->fb_force_xmit = 1;

	return 0;
}

int v4l2pcon_wait_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	return 0;
}

int v4l2pcon_get_fb_status(struct drm_crtc *drm_crtc,
		void *hw_drv_info, int flow, u32 *status)
{
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	unsigned long flags;

	PR_DEBUG("Queried for status\n");

	spin_lock_irqsave(&v4l2pcon_info->v4l2pcon_lock, flags);
	if (v4l2pcon_info->status & V4L2PCON_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&v4l2pcon_info->v4l2pcon_lock, flags);

	*status = tmp_status;

	return 0;
}

int v4l2pcon_set_fps(int fps, void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;
	unsigned long jiffies_snapshot;

	PR_DEBUG("v4l2pcon_set_fps, fps %d.\n", fps);

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (fps > V4L2PCON_FPS_HARD_LIMIT) {
		PR_ERR("Frame rate above the hard limit\n");
		return -EINVAL;
	}

	if (fps <= 0) {
		vhd->fb_xmit_period_jiffies = 0;
		PR_INFO("Transmission disabled, (negative or zero fps).\n");
	} else {
		vhd->fb_xmit_period_jiffies = HZ / fps;
		jiffies_snapshot = jiffies;
		vhd->last_xmit_jiffies = jiffies_snapshot;
		vhd->fb_force_xmit = 1;
		vhd->next_vblank_jiffies =
			jiffies_snapshot + vhd->fb_xmit_period_jiffies;

		PR_INFO("Frame transmission period set to %d jiffies\n",
			HZ / fps);
	}

	/* Schedule initial fake vblank */
	/*schedule_delayed_work(&v4l2pcon_info->fake_vblank_work, 0);*/
	queue_delayed_work(v4l2pcon_info->workqueue, &v4l2pcon_info->fake_vblank_work, 0);

	return 0;
}

int v4l2pcon_get_fps(int *fps, void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;

	PR_DEBUG("v4l2pcon_get_fps.\n");

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (vhd->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		PR_INFO
		("Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ / vhd->fb_xmit_period_jiffies;
		return 0;
	}
}

int v4l2pcon_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;
	int r = 0;
	int size_in_bytes, requested_num_pages;

	PR_DEBUG("In v4l2pcon_set_cursor, minor %d\n", v4l2pcon_info->minor);

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&v4l2pcon_info->buffer_mutex);
	memcpy(&vhd->vcrtcm_cursor, vcrtcm_cursor,
			sizeof(struct vcrtcm_cursor));

	/* calculate the push buffer size for cursor */
	size_in_bytes =
			vhd->vcrtcm_cursor.height *
			vhd->vcrtcm_cursor.width *
			(vhd->vcrtcm_cursor.bpp >> 3);

	requested_num_pages = size_in_bytes / PAGE_SIZE;
	if (size_in_bytes % PAGE_SIZE)
		requested_num_pages++;

	BUG_ON(vhd->pbd_cursor[0].num_pages !=
		vhd->pbd_cursor[1].num_pages);

	if (!requested_num_pages) {
		PR_DEBUG("cursor: zero size requested\n");
		v4l2pcon_free_pb(v4l2pcon_info, vhd,
				V4L2PCON_ALLOC_PB_FLAG_CURSOR);
	} else if (vhd->pbd_cursor[0].num_pages ==
					requested_num_pages) {
		PR_DEBUG("cursor : reusing existing push buffer\n");
	} else {
		/* if we got here, then we either dont have the */
		/* push buffer or we ahve one of the wrong size */
		/* (i.e. cursor size changed) */
		PR_INFO("cursor: allocating push buffer size=%d, "
				"num_pages=%d\n",
				size_in_bytes, requested_num_pages);

		v4l2pcon_free_pb(v4l2pcon_info, vhd,
				V4L2PCON_ALLOC_PB_FLAG_CURSOR);

		r = v4l2pcon_alloc_pb(v4l2pcon_info, vhd,
				requested_num_pages,
				V4L2PCON_ALLOC_PB_FLAG_CURSOR);
	}
	mutex_unlock(&v4l2pcon_info->buffer_mutex);
	return r;
}

int v4l2pcon_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;

	PR_DEBUG("In v4l2pcon_set_cursor, minor %d\n", v4l2pcon_info->minor);

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_cursor, &vhd->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));

	return 0;
}

int v4l2pcon_set_dpms(int state, void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;

	PR_DEBUG("in v4l2pcon_set_dpms, minor %d, state %d\n",
			v4l2pcon_info->minor, state);

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	vhd->dpms_state = state;

	return 0;
}

int v4l2pcon_get_dpms(int *state, void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *) hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;

	PR_DEBUG("in v4l2pcon_get_dpms, minor %d\n",
			v4l2pcon_info->minor);

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	*state = vhd->dpms_state;

	return 0;
}

void v4l2pcon_disable(void *hw_drv_info, int flow)
{
	struct v4l2pcon_info *v4l2pcon_info = (struct v4l2pcon_info *)hw_drv_info;
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd =
			v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	mutex_lock(&v4l2pcon_info->buffer_mutex);
	vhd->fb_xmit_allowed = 0;
	mutex_unlock(&v4l2pcon_info->buffer_mutex);
}


void v4l2pcon_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct v4l2pcon_info *v4l2pcon_info =
		container_of(delayed_work, struct v4l2pcon_info, fake_vblank_work);
	struct v4l2pcon_vcrtcm_hal_descriptor *vhd;
	/*static long last_snapshot = 0;*/

	unsigned long jiffies_snapshot = 0;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int v4l2pcon_fake_vblank_slack_sane = 0;

	PR_DEBUG("vblank fake, minor=%d\n", v4l2pcon_info->minor);
	v4l2pcon_fake_vblank_slack_sane =
			(v4l2pcon_fake_vblank_slack_sane <= 0) ? 0 : v4l2pcon_fake_vblank_slack;

	if (!v4l2pcon_info) {
		PR_ERR("v4l2pcon_fake_vblank: Cannot find v4l2pcon_info\n");
		return;
	}

	vhd = v4l2pcon_info->v4l2pcon_vcrtcm_hal_descriptor;

	if (!vhd) {
		PR_ERR("v4l2pcon_fake_vblank: Cannot find pcon descriptor\n");
		return;
	}

	jiffies_snapshot = jiffies;

	if (vhd->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + v4l2pcon_fake_vblank_slack_sane,
				vhd->next_vblank_jiffies)) {
			vhd->next_vblank_jiffies +=
					vhd->fb_xmit_period_jiffies;

			mutex_lock(&v4l2pcon_info->buffer_mutex);
			v4l2pcon_do_xmit_fb_push(vhd);
			mutex_unlock(&v4l2pcon_info->buffer_mutex);
		}

		if (!next_vblank_jiffies_valid) {
			next_vblank_jiffies = vhd->next_vblank_jiffies;
			next_vblank_jiffies_valid = 1;
		} else {
			if (time_after_eq(next_vblank_jiffies,
					vhd->next_vblank_jiffies)) {
				next_vblank_jiffies = vhd->next_vblank_jiffies;
			}
		}
	}

	if (next_vblank_jiffies_valid) {
		next_vblank_delay =
			(int)next_vblank_jiffies - (int)jiffies_snapshot;
		if (next_vblank_delay <= v4l2pcon_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		if (!queue_delayed_work(v4l2pcon_info->workqueue,
					&v4l2pcon_info->fake_vblank_work,
					next_vblank_delay))
			PR_WARN("dup fake vblank, minor %d\n",
				v4l2pcon_info->minor);
	} else
		PR_DEBUG("Next fake vblank not scheduled\n");

	return;
}

int v4l2pcon_do_xmit_fb_push(struct v4l2pcon_vcrtcm_hal_descriptor *vhd)
{
	struct v4l2pcon_info *v4l2pcon_info = vhd->v4l2pcon_info;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	PR_DEBUG("in v4l2pcon_do_xmit_fb_push, minor %d\n", v4l2pcon_info->minor);

	spin_lock_irqsave(&v4l2pcon_info->v4l2pcon_lock, flags);
	v4l2pcon_info->status |= V4L2PCON_IN_DO_XMIT;
	spin_unlock_irqrestore(&v4l2pcon_info->v4l2pcon_lock, flags);

	push_buffer_index = vhd->push_buffer_index;

	if (vhd->pbd_fb[push_buffer_index].num_pages) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		PR_WARN("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((vhd->fb_force_xmit ||
	     time_after(jiffies_snapshot, vhd->last_xmit_jiffies +
			V4L2PCON_XMIT_HARD_DEADLINE)) && have_push_buffer &&
	    vhd->fb_xmit_allowed) {
		/* someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 */

		PR_DEBUG("transmission happening...\n");
		vhd->fb_force_xmit = 0;
		vhd->last_xmit_jiffies = jiffies;
		vhd->fb_xmit_counter++;

		PR_DEBUG("v4l2pcon_do_xmit_fb_push[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				vhd->vcrtcm_fb.pitch,
				vhd->vcrtcm_fb.width,
				vhd->vcrtcm_fb.height,
				vhd->vcrtcm_fb.bpp);

		PR_DEBUG("v4l2pcon_do_xmit_fb_push[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				vhd->vcrtcm_fb.viewport_x,
				vhd->vcrtcm_fb.viewport_y,
				vhd->vcrtcm_fb.hdisplay,
				vhd->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&v4l2pcon_info->v4l2pcon_lock, flags);
		v4l2pcon_info->status &= ~V4L2PCON_IN_DO_XMIT;
		spin_unlock_irqrestore(&v4l2pcon_info->v4l2pcon_lock, flags);

		r = vcrtcm_push(vhd->vcrtcm_pcon_info,
				&vhd->pbd_fb[push_buffer_index],
				&vhd->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_emulate_vblank(vhd->vcrtcm_pcon_info);
		} else {
			/* if push successed, then we need to swap push buffers
			 * and mark the buffer for transmission in the next
			 * vblank interval; note that call to vcrtcm_push only
			 * initiates the push request to GPU; when GPU does it
			 * is up to the GPU and doesn't matter as long as it is
			 * within the frame transmission period (otherwise, we'll
			 * see from frame tearing)
			 * If GPU completes the push before the next vblank
			 * interval, then it is perfectly safe to mark the buffer
			 * ready for transmission now because transmission wont
			 * look at it until push is complete.
			 */

			vhd->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			vhd->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		spin_lock_irqsave(&v4l2pcon_info->v4l2pcon_lock, flags);
		v4l2pcon_info->status &= ~V4L2PCON_IN_DO_XMIT;
		spin_unlock_irqrestore(&v4l2pcon_info->v4l2pcon_lock, flags);

		vcrtcm_emulate_vblank(vhd->vcrtcm_pcon_info);
		PR_DEBUG("transmission not happening\n");
	}

	if (vhd->pb_needs_xmit[push_buffer_index]) {
		struct vcrtcm_cursor *cursor;
		unsigned int hpixels, vpixels;
		unsigned int vp_offset, hlen, p, vpx, vpy, Bpp;
		unsigned int i, j;
		char *mb, *sb;

		hpixels = vhd->vcrtcm_fb.hdisplay;
		vpixels = vhd->vcrtcm_fb.vdisplay;
		p = vhd->vcrtcm_fb.pitch;
		vpx = vhd->vcrtcm_fb.viewport_x;
		vpy = vhd->vcrtcm_fb.viewport_y;
		Bpp = vhd->vcrtcm_fb.bpp >> 3;
		vp_offset = p * vpy + vpx * Bpp;
		hlen = hpixels * Bpp;

		v4l2pcon_info->main_buffer = vhd->pb_fb[push_buffer_index];
		v4l2pcon_info->cursor = vhd->pb_cursor[push_buffer_index];

		/* Overlay the cursor on the framebuffer */
		cursor = &vhd->vcrtcm_cursor;
		if (cursor->flag != VCRTCM_CURSOR_FLAG_HIDE) {
			uint32_t *fb_end;
			int clip_y = 0;
			mb = v4l2pcon_info->main_buffer + vp_offset;
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
				cursor_pixel = (uint32_t *) v4l2pcon_info->cursor;
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

		PR_DEBUG("v4l2pcon_do_xmit_fb_push[%d]: initiating copy\n",
				push_buffer_index);
		jiffies_snapshot = jiffies;
		mutex_lock(&v4l2pcon_info->sb_lock);
		if (v4l2pcon_info->shadowbuf) {
			v4l2pcon_info->jshadowbuf = jiffies;
			mb = v4l2pcon_info->main_buffer + vp_offset;
			sb = v4l2pcon_info->shadowbuf;
			for (i = 0; i < vpixels; i++) {
				memcpy(sb, mb, hlen);
				mb += p;
				sb += hlen;
			}
		}
		mutex_unlock(&v4l2pcon_info->sb_lock);
		PR_DEBUG("copy took %u ms\n", jiffies_to_msecs(jiffies - jiffies_snapshot));

		vhd->pb_needs_xmit[push_buffer_index] = 0;
	}

	return r;
}
