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

#include <vcrtcm/vcrtcm_pcon.h>
#include "v4l2pim.h"
#include "v4l2pim_vcrtcm.h"

static void v4l2pim_free_pb(struct v4l2pim_info *v4l2pim_info,
		struct v4l2pim_flow_info *flow_info, int flag)
{
	int i;

	struct vcrtcm_push_buffer_descriptor *pbd;
	void *pb_mapped_ram;

	for (i = 0; i < 2; i++) {
		if (flag == V4L2PIM_ALLOC_PB_FLAG_FB) {
			pbd = &flow_info->pbd_fb[i];
			pb_mapped_ram = flow_info->pb_fb[i];
		} else {
			pbd = &flow_info->pbd_cursor[i];
			pb_mapped_ram = flow_info->pb_cursor[i];
		}

		if (pbd->num_pages) {
			BUG_ON(!pbd->gpu_private);
			vm_unmap_ram(pb_mapped_ram, pbd->num_pages);
			vcrtcm_p_unregister_prime(flow_info->vcrtcm_pcon_info,
						pbd);
			vcrtcm_free_multiple_pages(pbd->pages, pbd->num_pages,
						&v4l2pim_info->page_track);
			vcrtcm_kfree(pbd->pages, &v4l2pim_info->kmalloc_track);
			memset(pbd, 0,
				sizeof(struct vcrtcm_push_buffer_descriptor));
			pbd->owner_pcon = flow_info->vcrtcm_pcon_info;
		}
	}
}

static int v4l2pim_alloc_pb(struct v4l2pim_info *v4l2pim_info,
		struct v4l2pim_flow_info *flow_info,
		int num_pages, int flag)
{
	int i;
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd, *old_pbd = NULL;
	void *pb, *old_pb = NULL;

	for (i = 0; i < 2; i++) {
		if (flag == V4L2PIM_ALLOC_PB_FLAG_FB)
			pbd = &flow_info->pbd_fb[i];
		else
			pbd = &flow_info->pbd_cursor[i];
		pbd->pages = vcrtcm_kzalloc(num_pages * sizeof(struct page *),
				GFP_KERNEL, &v4l2pim_info->kmalloc_track);
		if (!pbd->pages) {
			VCRTCM_ERROR("%s[%d]: pages pointer alloc failed\n",
					V4L2PIM_ALLOC_PB_STRING(flag), i);
			r = -ENOMEM;
			goto out_err0;
		}
		r = vcrtcm_alloc_multiple_pages(GFP_KERNEL | __GFP_HIGHMEM,
						pbd->pages, num_pages,
						&v4l2pim_info->page_track);
		if (r) {
			VCRTCM_ERROR("%s[%d]: push buffer alloc_failed\n",
					V4L2PIM_ALLOC_PB_STRING(flag), i);
			goto out_err1;
		}
		V4L2PIM_DEBUG("%s[%d]: allocated %d pages\n",
			V4L2PIM_ALLOC_PB_STRING(flag), i, num_pages);
		pbd->num_pages = num_pages;
		pb = vm_map_ram(pbd->pages, num_pages, 0, PAGE_KERNEL);
		if (pb == NULL) {
			VCRTCM_ERROR("%s[%d]: vm_map_ram failed\n",
				V4L2PIM_ALLOC_PB_STRING(flag), i);
			r = -ENOMEM;
			goto out_err2;
		}
		if (flag == V4L2PIM_ALLOC_PB_FLAG_FB)
			flow_info->pb_fb[i] = pb;
		else
			flow_info->pb_cursor[i] = pb;
		/* register_prime will finish up the construction of pbd */
		r = vcrtcm_p_register_prime(flow_info->vcrtcm_pcon_info, pbd);
		if (r) {
			VCRTCM_ERROR("%s[%d]: export to VCRTCM failed\n",
				V4L2PIM_ALLOC_PB_STRING(flag), i);
				goto out_err3;
		}
		flow_info->pb_needs_xmit[i] = 0;
		old_pbd = pbd;
		old_pb = pb;
	}
	if (!r)
		return r;

out_err3:
	vm_unmap_ram(pbd->pages, num_pages);
	if (flag == V4L2PIM_ALLOC_PB_FLAG_FB)
		flow_info->pb_fb[i] = NULL;
	else
		flow_info->pb_cursor[i] = NULL;
out_err2:
	vcrtcm_free_multiple_pages(pbd->pages, num_pages,
				&v4l2pim_info->page_track);
out_err1:
	vcrtcm_kfree(pbd->pages, &v4l2pim_info->kmalloc_track);
	memset(pbd, 0, sizeof(struct vcrtcm_push_buffer_descriptor));
	pbd->owner_pcon = flow_info->vcrtcm_pcon_info;
out_err0:
	if (i == 1) {
		/*
		 * allocation failed in the second iteration
		 * of the loop, we must release the buffer
		 * allocated in the first one before returning
		 */
		BUG_ON(!old_pbd || !old_pb);
		vm_unmap_ram(old_pbd->pages, num_pages);
		if (flag == V4L2PIM_ALLOC_PB_FLAG_FB)
			flow_info->pb_fb[0] = NULL;
		else
			flow_info->pb_cursor[0] = NULL;
		vcrtcm_free_multiple_pages(old_pbd->pages, num_pages,
					&v4l2pim_info->page_track);
		vcrtcm_kfree(old_pbd->pages, &v4l2pim_info->kmalloc_track);
		memset(old_pbd, 0,
			sizeof(struct vcrtcm_push_buffer_descriptor));
		old_pbd->owner_pcon = flow_info->vcrtcm_pcon_info;
	}
	return r;
}

int v4l2pim_attach(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;

	VCRTCM_INFO("Attaching vl2pcon %d to pcon %p\n",
		v4l2pim_info->minor, vcrtcm_pcon_info);

	if (v4l2pim_info->flow_info) {
		VCRTCM_ERROR("minor already served\n");
		return -EBUSY;
	} else {
		struct v4l2pim_flow_info *flow_info =
			vcrtcm_kzalloc(sizeof(struct v4l2pim_flow_info),
				GFP_KERNEL, &v4l2pim_info->kmalloc_track);
		if (flow_info == NULL) {
			VCRTCM_ERROR("no memory\n");
			return -ENOMEM;
		}

		flow_info->v4l2pim_info = v4l2pim_info;
		flow_info->vcrtcm_pcon_info = vcrtcm_pcon_info;
		flow_info->fps = 0;
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
		flow_info->pbd_fb[0].owner_pcon = vcrtcm_pcon_info;
		flow_info->pbd_fb[1].owner_pcon = vcrtcm_pcon_info;
		flow_info->pbd_cursor[0].owner_pcon = vcrtcm_pcon_info;
		flow_info->pbd_cursor[1].owner_pcon = vcrtcm_pcon_info;
		flow_info->pb_fb[0] = 0;
		flow_info->pb_fb[1] = 0;
		flow_info->pb_cursor[0] = 0;
		flow_info->pb_cursor[1] = 0;

		flow_info->vcrtcm_cursor.flag = VCRTCM_CURSOR_FLAG_HIDE;

		v4l2pim_info->flow_info = flow_info;

		VCRTCM_INFO("v4l2pim %d now serves pcon %p\n",
			    v4l2pim_info->minor, vcrtcm_pcon_info);

		return 0;
	}
}

int v4l2pim_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;

	if (atomic_read(&v4l2pim_info->users) > 0) {
		VCRTCM_INFO("Could not detach v4l2pim %d from pcon %p, file open",
			v4l2pim_info->minor, vcrtcm_pcon_info);
		return -EBUSY;
	}

	VCRTCM_INFO("Detaching v4l2pim %d from pcon %p\n",
		v4l2pim_info->minor, vcrtcm_pcon_info);

	vcrtcm_p_wait_fb(vcrtcm_pcon_info);
	flow_info = v4l2pim_info->flow_info;

	cancel_delayed_work_sync(&v4l2pim_info->fake_vblank_work);

	if (flow_info->vcrtcm_pcon_info == vcrtcm_pcon_info) {
		V4L2PIM_DEBUG("Found descriptor that should be removed.\n");

		v4l2pim_free_pb(v4l2pim_info, flow_info,
				V4L2PIM_ALLOC_PB_FLAG_FB);
		v4l2pim_free_pb(v4l2pim_info, flow_info,
				V4L2PIM_ALLOC_PB_FLAG_CURSOR);

		v4l2pim_info->flow_info = NULL;
		vcrtcm_kfree(flow_info, &v4l2pim_info->kmalloc_track);

		v4l2pim_info->main_buffer = NULL;
		v4l2pim_info->cursor = NULL;
	}
	return 0;
}

static int v4l2pim_realloc_pb(struct v4l2pim_info *v4l2pim_info,
			      struct v4l2pim_flow_info *flow_info,
			      int size, int flag)
{
	int num_pages, r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd0, *pbd1;

	if (flag ==  V4L2PIM_ALLOC_PB_FLAG_FB) {
		pbd0 = &flow_info->pbd_fb[0];
		pbd1 = &flow_info->pbd_fb[1];
	} else {
		pbd0 = &flow_info->pbd_cursor[0];
		pbd1 = &flow_info->pbd_cursor[1];
	}

	num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE)
		num_pages++;
	BUG_ON(pbd0->num_pages != pbd1->num_pages);
	if (!num_pages) {
		V4L2PIM_DEBUG("%s: zero size requested\n",
			      V4L2PIM_ALLOC_PB_STRING(flag));
		v4l2pim_free_pb(v4l2pim_info, flow_info, flag);
	} else if (pbd0->num_pages == num_pages) {
		V4L2PIM_DEBUG("%s: reusing existing push buffer\n",
			      V4L2PIM_ALLOC_PB_STRING(flag));
	} else {
		/*
		 * if we got here, then we either don't have the
		 * push buffer or we have one of the wrong size
		 */
		V4L2PIM_DEBUG("%s: allocating push buffer "
			      "size=%d, num_pages=%d\n",
			      V4L2PIM_ALLOC_PB_STRING(flag), size, num_pages);
		v4l2pim_free_pb(v4l2pim_info, flow_info, flag);
		r = v4l2pim_alloc_pb(v4l2pim_info, flow_info, num_pages, flag);
		if (flag ==  V4L2PIM_ALLOC_PB_FLAG_FB) {
			uint32_t w, h, sb_size;

			/* this should get freed later */
			w = flow_info->vcrtcm_fb.hdisplay;
			h = flow_info->vcrtcm_fb.vdisplay;
			sb_size = w * h * (flow_info->vcrtcm_fb.bpp >> 3);
			mutex_lock(&v4l2pim_info->sb_lock);
			v4l2pim_alloc_shadowbuf(v4l2pim_info, sb_size);
			mutex_unlock(&v4l2pim_info->sb_lock);
		}
	}

	return r;
}

int v4l2pim_set_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		   struct vcrtcm_fb *vcrtcm_fb)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;
	int r = 0;
	int size;

	V4L2PIM_DEBUG("minor %d.\n", v4l2pim_info->minor);

	flow_info = v4l2pim_info->flow_info;

	/* TODO: Do we need this? */
	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&v4l2pim_info->buffer_mutex);
	memcpy(&flow_info->vcrtcm_fb, vcrtcm_fb, sizeof(struct vcrtcm_fb));

	size = flow_info->vcrtcm_fb.pitch * flow_info->vcrtcm_fb.vdisplay;
	r = v4l2pim_realloc_pb(v4l2pim_info, flow_info, size,
			       V4L2PIM_ALLOC_PB_FLAG_FB);
	flow_info->fb_xmit_allowed = 1;
	mutex_unlock(&v4l2pim_info->buffer_mutex);
	return r;
}

int v4l2pim_get_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		   struct vcrtcm_fb *vcrtcm_fb)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;

	V4L2PIM_DEBUG("minor %d.\n", v4l2pim_info->minor);
	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_fb, &flow_info->vcrtcm_fb, sizeof(struct vcrtcm_fb));

	return 0;
}

int v4l2pim_dirty_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		     struct drm_crtc *drm_crtc)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;

	V4L2PIM_DEBUG("minor %d\n", v4l2pim_info->minor);

	/* just mark the "force" flag, v4l2pim_do_xmit_fb_pull
	 * does the rest (when called).
	 */

	flow_info = v4l2pim_info->flow_info;

	if (flow_info)
		flow_info->fb_force_xmit = 1;

	return 0;
}

int v4l2pim_wait_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		    struct drm_crtc *drm_crtc)
{
	return 0;
}

int v4l2pim_get_fb_status(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			  struct drm_crtc *drm_crtc, u32 *status)
{
	u32 tmp_status = VCRTCM_FB_STATUS_IDLE;
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	unsigned long flags;

	V4L2PIM_DEBUG("\n");

	spin_lock_irqsave(&v4l2pim_info->v4l2pim_lock, flags);
	if (v4l2pim_info->status & V4L2PIM_IN_DO_XMIT)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	spin_unlock_irqrestore(&v4l2pim_info->v4l2pim_lock, flags);

	*status = tmp_status;

	return 0;
}

int v4l2pim_set_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int fps)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;
	unsigned long jiffies_snapshot;

	V4L2PIM_DEBUG("fps %d.\n", fps);

	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (fps > V4L2PIM_FPS_HARD_LIMIT) {
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
	/*schedule_delayed_work(&v4l2pim_info->fake_vblank_work, 0);*/
	queue_delayed_work(v4l2pim_info->workqueue,
			   &v4l2pim_info->fake_vblank_work, 0);

	return 0;
}

int v4l2pim_get_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *fps)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;

	V4L2PIM_DEBUG("\n");

	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	if (flow_info->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		VCRTCM_INFO("Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ / flow_info->fb_xmit_period_jiffies;
		return 0;
	}
}

int v4l2pim_set_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		       struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;
	int r = 0;
	int size;

	V4L2PIM_DEBUG("minor %d\n", v4l2pim_info->minor);
	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	mutex_lock(&v4l2pim_info->buffer_mutex);
	memcpy(&flow_info->vcrtcm_cursor, vcrtcm_cursor,
			sizeof(struct vcrtcm_cursor));
	size = flow_info->vcrtcm_cursor.height *
		flow_info->vcrtcm_cursor.width *
		(flow_info->vcrtcm_cursor.bpp >> 3);
	r = v4l2pim_realloc_pb(v4l2pim_info, flow_info, size,
			       V4L2PIM_ALLOC_PB_FLAG_CURSOR);
	mutex_unlock(&v4l2pim_info->buffer_mutex);
	return r;
}

int v4l2pim_get_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		       struct vcrtcm_cursor *vcrtcm_cursor)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;

	V4L2PIM_DEBUG("minor %d\n", v4l2pim_info->minor);

	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_cursor, &flow_info->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));

	return 0;
}

int v4l2pim_set_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int state)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;

	V4L2PIM_DEBUG("minor %d, state %d\n", v4l2pim_info->minor, state);

	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	flow_info->dpms_state = state;

	return 0;
}

int v4l2pim_get_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *state)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *) vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info;

	V4L2PIM_DEBUG("minor %d\n", v4l2pim_info->minor);

	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return -EINVAL;
	}

	*state = flow_info->dpms_state;

	return 0;
}

void v4l2pim_disable(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	struct v4l2pim_info *v4l2pim_info =
		(struct v4l2pim_info *)vcrtcm_pcon_info->pcon_cookie;
	struct v4l2pim_flow_info *flow_info =
			v4l2pim_info->flow_info;

	mutex_lock(&v4l2pim_info->buffer_mutex);
	flow_info->fb_xmit_allowed = 0;
	mutex_unlock(&v4l2pim_info->buffer_mutex);
}


void v4l2pim_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct v4l2pim_info *v4l2pim_info =
		container_of(delayed_work, struct v4l2pim_info,
			     fake_vblank_work);
	struct v4l2pim_flow_info *flow_info;
	/*static long last_snapshot = 0;*/

	unsigned long jiffies_snapshot = 0;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int v4l2pim_fake_vblank_slack_sane = 0;

	V4L2PIM_DEBUG("minor=%d\n", v4l2pim_info->minor);
	v4l2pim_fake_vblank_slack_sane =
			(v4l2pim_fake_vblank_slack_sane <= 0) ? 0 : v4l2pim_fake_vblank_slack;

	if (!v4l2pim_info) {
		VCRTCM_ERROR("Cannot find v4l2pim_info\n");
		return;
	}

	flow_info = v4l2pim_info->flow_info;

	if (!flow_info) {
		VCRTCM_ERROR("Cannot find pcon descriptor\n");
		return;
	}

	jiffies_snapshot = jiffies;

	if (flow_info->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + v4l2pim_fake_vblank_slack_sane,
				flow_info->next_vblank_jiffies)) {
			flow_info->next_vblank_jiffies +=
					flow_info->fb_xmit_period_jiffies;

			mutex_lock(&v4l2pim_info->buffer_mutex);
			v4l2pim_do_xmit_fb_push(flow_info);
			mutex_unlock(&v4l2pim_info->buffer_mutex);
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
		if (next_vblank_delay <= v4l2pim_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		if (!queue_delayed_work(v4l2pim_info->workqueue,
					&v4l2pim_info->fake_vblank_work,
					next_vblank_delay))
			VCRTCM_WARNING("dup fake vblank, minor %d\n",
				v4l2pim_info->minor);
	} else
		V4L2PIM_DEBUG("Next fake vblank not scheduled\n");

	return;
}

int v4l2pim_do_xmit_fb_push(struct v4l2pim_flow_info *flow_info)
{
	struct v4l2pim_info *v4l2pim_info = flow_info->v4l2pim_info;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;
	unsigned long flags;

	V4L2PIM_DEBUG("in v4l2pim_do_xmit_fb_push, minor %d\n",
		      v4l2pim_info->minor);

	spin_lock_irqsave(&v4l2pim_info->v4l2pim_lock, flags);
	v4l2pim_info->status |= V4L2PIM_IN_DO_XMIT;
	spin_unlock_irqrestore(&v4l2pim_info->v4l2pim_lock, flags);

	push_buffer_index = flow_info->push_buffer_index;

	if (flow_info->pbd_fb[push_buffer_index].num_pages) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		VCRTCM_WARNING("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	if ((flow_info->fb_force_xmit ||
	     time_after(jiffies_snapshot, flow_info->last_xmit_jiffies +
			V4L2PIM_XMIT_HARD_DEADLINE)) && have_push_buffer &&
	    flow_info->fb_xmit_allowed) {
		/* someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 */

		V4L2PIM_DEBUG("transmission happening...\n");
		flow_info->fb_force_xmit = 0;
		flow_info->last_xmit_jiffies = jiffies;
		flow_info->fb_xmit_counter++;

		V4L2PIM_DEBUG("v4l2pim_do_xmit_fb_push[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				flow_info->vcrtcm_fb.pitch,
				flow_info->vcrtcm_fb.width,
				flow_info->vcrtcm_fb.height,
				flow_info->vcrtcm_fb.bpp);

		V4L2PIM_DEBUG("v4l2pim_do_xmit_fb_push[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				flow_info->vcrtcm_fb.viewport_x,
				flow_info->vcrtcm_fb.viewport_y,
				flow_info->vcrtcm_fb.hdisplay,
				flow_info->vcrtcm_fb.vdisplay);

		spin_lock_irqsave(&v4l2pim_info->v4l2pim_lock, flags);
		v4l2pim_info->status &= ~V4L2PIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&v4l2pim_info->v4l2pim_lock, flags);

		r = vcrtcm_p_push(flow_info->vcrtcm_pcon_info,
				&flow_info->pbd_fb[push_buffer_index],
				&flow_info->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_p_emulate_vblank(flow_info->vcrtcm_pcon_info);
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

			flow_info->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			flow_info->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		spin_lock_irqsave(&v4l2pim_info->v4l2pim_lock, flags);
		v4l2pim_info->status &= ~V4L2PIM_IN_DO_XMIT;
		spin_unlock_irqrestore(&v4l2pim_info->v4l2pim_lock, flags);

		vcrtcm_p_emulate_vblank(flow_info->vcrtcm_pcon_info);
		V4L2PIM_DEBUG("transmission not happening\n");
	}

	if (flow_info->pb_needs_xmit[push_buffer_index]) {
		struct vcrtcm_cursor *cursor;
		unsigned int hpixels, vpixels;
		unsigned int vp_offset, hlen, p, vpx, vpy, Bpp;
		unsigned int i, j;
		char *mb, *sb;

		hpixels = flow_info->vcrtcm_fb.hdisplay;
		vpixels = flow_info->vcrtcm_fb.vdisplay;
		p = flow_info->vcrtcm_fb.pitch;
		vpx = flow_info->vcrtcm_fb.viewport_x;
		vpy = flow_info->vcrtcm_fb.viewport_y;
		Bpp = flow_info->vcrtcm_fb.bpp >> 3;
		vp_offset = p * vpy + vpx * Bpp;
		hlen = hpixels * Bpp;

		v4l2pim_info->main_buffer = flow_info->pb_fb[push_buffer_index];
		v4l2pim_info->cursor = flow_info->pb_cursor[push_buffer_index];

		/* Overlay the cursor on the framebuffer */
		cursor = &flow_info->vcrtcm_cursor;
		if (cursor->flag != VCRTCM_CURSOR_FLAG_HIDE) {
			uint32_t *fb_end;
			int clip_y = 0;
			mb = v4l2pim_info->main_buffer + vp_offset;
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
				cursor_pixel = (uint32_t *) v4l2pim_info->cursor;
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
		mutex_lock(&v4l2pim_info->sb_lock);
		if (v4l2pim_info->shadowbuf) {
			v4l2pim_info->jshadowbuf = jiffies;
			mb = v4l2pim_info->main_buffer + vp_offset;
			sb = v4l2pim_info->shadowbuf;
			for (i = 0; i < vpixels; i++) {
				memcpy(sb, mb, hlen);
				mb += p;
				sb += hlen;
			}
		}
		mutex_unlock(&v4l2pim_info->sb_lock);
		V4L2PIM_DEBUG("copy took %u ms\n",
			      jiffies_to_msecs(jiffies - jiffies_snapshot));
		flow_info->pb_needs_xmit[push_buffer_index] = 0;
	}

	return r;
}
