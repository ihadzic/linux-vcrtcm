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

#include "v4l2ctd.h"
#include "v4l2ctd_vcrtcm.h"
#include "v4l2ctd_utils.h"
#include "vcrtcm/vcrtcm_ctd.h"

int v4l2ctd_attach(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;

	V4L2CTD_DEBUG(1, "Attaching v4l2ctd %d to HAL %p\n",
		v4l2ctd_info->minor, vcrtcm_dev_hal);

	mutex_lock(&v4l2ctd_info->xmit_mutex);

	if (v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor) {
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		V4L2CTD_ERROR("minor already served\n");
		return -EBUSY;
	} else {
		struct v4l2ctd_vcrtcm_hal_descriptor
			*v4l2ctd_vcrtcm_hal_descriptor =
			v4l2ctd_kzalloc(v4l2ctd_info,
					sizeof(struct v4l2ctd_vcrtcm_hal_descriptor),
					GFP_KERNEL);
		if (v4l2ctd_vcrtcm_hal_descriptor == NULL) {
			mutex_unlock(&v4l2ctd_info->xmit_mutex);
			V4L2CTD_ERROR("no memory\n");
			return -ENOMEM;
		}

		v4l2ctd_vcrtcm_hal_descriptor->v4l2ctd_info = v4l2ctd_info;
		v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal = vcrtcm_dev_hal;
		v4l2ctd_vcrtcm_hal_descriptor->fb_force_xmit = 0;
		v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_counter = 0;
		v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies = 0;
		v4l2ctd_vcrtcm_hal_descriptor->next_fb_xmit_jiffies = 0;
		v4l2ctd_vcrtcm_hal_descriptor->next_vblank_jiffies = 0;
		v4l2ctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr = 0x0;
		v4l2ctd_vcrtcm_hal_descriptor->push_buffer_index = 0;
		v4l2ctd_vcrtcm_hal_descriptor->pb_needs_xmit[0] = 0;
		v4l2ctd_vcrtcm_hal_descriptor->pb_needs_xmit[1] = 0;
		memset(&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		memset(&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor, 0,
			2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		v4l2ctd_vcrtcm_hal_descriptor->pb_fb[0] = 0;
		v4l2ctd_vcrtcm_hal_descriptor->pb_fb[1] = 0;
		v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[0] = 0;
		v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[1] = 0;

		v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_cursor.flag =
			VCRTCM_CURSOR_FLAG_HIDE;

		v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor =
			v4l2ctd_vcrtcm_hal_descriptor;

		mutex_unlock(&v4l2ctd_info->xmit_mutex);

		V4L2CTD_DEBUG(1, "v4l2ctd %d now serves HAL %p\n", v4l2ctd_info->minor,
			vcrtcm_dev_hal);

		return 0;
	}
}

void v4l2ctd_detach(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	V4L2CTD_DEBUG(1, "Detaching v4l2ctd %d from HAL %p\n",
		v4l2ctd_info->minor, vcrtcm_dev_hal);

	mutex_lock(&v4l2ctd_info->xmit_mutex);

	cancel_delayed_work_sync(&v4l2ctd_info->fake_vblank_work);

	v4l2ctd_vcrtcm_hal_descriptor =
		v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	/* TODO: Do we need this if? */
	if (v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal == vcrtcm_dev_hal) {
		V4L2CTD_DEBUG(1, "Found descriptor that should be removed.\n");

		if (v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[0].num_pages) {
			BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[0].gpu_private);
			if (v4l2ctd_vcrtcm_hal_descriptor->pb_fb[0]) {
				vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_fb[0],
						v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[0].num_pages);
				v4l2ctd_vcrtcm_hal_descriptor->pb_fb[0] = NULL;
			}
			vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
				&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[0]);
		}

		if (v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[0].num_pages) {
			BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[0].gpu_private);
			if (v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[0]) {
				vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[0],
						v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[0].num_pages);
				v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[0] = NULL;
			}
			vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
					&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[0]);
		}

		if (v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[1].num_pages) {
			BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[1].gpu_private);
			if (v4l2ctd_vcrtcm_hal_descriptor->pb_fb[1]) {
				vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_fb[1],
						v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[1].num_pages);
				v4l2ctd_vcrtcm_hal_descriptor->pb_fb[1] = NULL;
			}
			vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
						&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[1]);
		}

		if (v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[1].num_pages) {
			BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[1].gpu_private);
			if (v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[1]) {
				vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[1],
						v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[1].num_pages);
				v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[1] = NULL;
			}
			vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
					&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[1]);
		}

		v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor = NULL;
		v4l2ctd_kfree(v4l2ctd_info, v4l2ctd_vcrtcm_hal_descriptor);

		v4l2ctd_info->cursor = NULL;
		v4l2ctd_info->cursor_len = 0;
		v4l2ctd_info->main_buffer = NULL;
		v4l2ctd_info->fb_len = 0;
	}

	mutex_unlock(&v4l2ctd_info->xmit_mutex);
}

int v4l2ctd_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;
	uint32_t w, h, sb_size;
	int r = 0;

	V4L2CTD_DEBUG(2, "minor %d.\n", v4l2ctd_info->minor);

	v4l2ctd_vcrtcm_hal_descriptor =
		v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	/* TODO: Do we need this? */
	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	memcpy(&v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb,
		vcrtcm_fb, sizeof(struct vcrtcm_fb));

	if (V4L2CTD_FB_XFER_MODE == V4L2CTD_FB_PUSH) {
		int size_in_bytes, requested_num_pages, i, j;

		size_in_bytes = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch *
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay;

		requested_num_pages = size_in_bytes / PAGE_SIZE;
		if (size_in_bytes % PAGE_SIZE)
			requested_num_pages++;

		for (i = 0; i < 2; i++) {
			if (!requested_num_pages) {
				V4L2CTD_DEBUG(1, "framebuffer[%d]: zero size requested\n", i);
				/* free old buffer if there is one */
				if (v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages) {
					BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].gpu_private);
					if (v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i]) {
						vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i],
								v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages);
						v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i] = NULL;
					}
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i]);
				}
			} else if (v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages == requested_num_pages) {
				V4L2CTD_DEBUG(1, "framebuffer[%d]: reusing existing push buffer\n", i);
			} else {
				/* if we have got here, then we either don't have the push buffer */
				/* or we have one of the wrong size (i.e. mode has changed) */

				/* shadowbuf init */
				/* this should get freed later */
				w = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.hdisplay;
				h = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay;
				sb_size = w * h * (V4L2CTD_BPP / 8);
				mutex_lock(&v4l2ctd_info->sb_lock);
				v4l2ctd_alloc_shadowbuf(v4l2ctd_info, sb_size);
				if (!v4l2ctd_info->shadowbuf)
					return -ENOMEM;
				mutex_unlock(&v4l2ctd_info->sb_lock);

				V4L2CTD_DEBUG(1, "framebuffer[%d]: allocating push buffer, size=%d, "
						"num_pages=%d\n", i, size_in_bytes, requested_num_pages);

				/* free old buffer */
				if (v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages) {
					BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].gpu_private);
					if (v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i]) {
						vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i],
								v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages);
						v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i] = NULL;
					}
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i]);
				}

				/* allocate new buffer */
				v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages =
						requested_num_pages;
				r = vcrtcm_push_buffer_alloc(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
						&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i]);

				/* sanity check */

				if (r) {
					V4L2CTD_ERROR("framebuffer[%d]: push buffer alloc failed\n", i);
					memset(&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb, 0,
							sizeof(struct vcrtcm_push_buffer_descriptor));
					return -ENOMEM;
				}

				if (v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages < requested_num_pages) {
					V4L2CTD_ERROR("framebuffer[%d]: not enough pages allocated\n", i);
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i]);
					return -ENOMEM;
				}

				/* vmap the allocated pages */
				v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i] =
						vm_map_ram(v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].pages,
								v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages,
								0, PAGE_KERNEL);

				if (!v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i]) {
					V4L2CTD_ERROR("framebuffer[%d]: could not do vmap\n", i);
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i]);
					/*mutex_unlock(&v4l2ctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				v4l2ctd_vcrtcm_hal_descriptor->pb_needs_xmit[i] = 0;
				V4L2CTD_DEBUG(2, "framebuffer[%d]: allocated %lu pages, last_lomem=%ld, "
						"first_himem=%ld\n", i,
						v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages,
						v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].last_lomem_page,
						v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].first_himem_page);

				for (j = 0; j < v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages; j++)
					V4L2CTD_DEBUG(2, "framebuffer[%d]: page [%d]=%p\n", i, j,
							v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[i].pages[j]);

				V4L2CTD_DEBUG(2, "framebuffer[%d]: vmap=%p\n", i, v4l2ctd_vcrtcm_hal_descriptor->pb_fb[i]);
			}
		}
	}

	return r;
}

int v4l2ctd_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	V4L2CTD_DEBUG(2, "minor %d.\n", v4l2ctd_info->minor);
	v4l2ctd_vcrtcm_hal_descriptor = v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_fb, &v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb,
		sizeof(struct vcrtcm_fb));

	return 0;
}

int v4l2ctd_xmit_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	V4L2CTD_DEBUG(2, "minor %d\n", v4l2ctd_info->minor);

	/* just mark the "force flag", v4l2ctd_do_xmit_fb_pull
	 * does the rest (when called).	 *
	 */

	mutex_lock(&v4l2ctd_info->xmit_mutex);
	v4l2ctd_vcrtcm_hal_descriptor = v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (v4l2ctd_vcrtcm_hal_descriptor)
		v4l2ctd_vcrtcm_hal_descriptor->fb_force_xmit = 1;

	mutex_unlock(&v4l2ctd_info->xmit_mutex);

	return 0;
}

int v4l2ctd_wait_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info =
			(struct v4l2ctd_info *) hw_drv_info;

	return v4l2ctd_wait_idle_core(v4l2ctd_info);
}

int v4l2ctd_get_fb_status(struct drm_crtc *drm_crtc,
		void *hw_drv_info, int flow, u32 *status)
{
	u32 tmp_status = 0;
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;

	/* we are transmitting if v4l2ctd is busy */
	if (v4l2ctd_info->xfer_in_progress)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	else
		tmp_status |= VCRTCM_FB_STATUS_IDLE;

	*status = tmp_status;

	return 0;
}


int v4l2ctd_set_fps(int fps, void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;
	unsigned long jiffies_snapshot;

	V4L2CTD_DEBUG(1, "fps %d.\n", fps);

	mutex_lock(&v4l2ctd_info->xmit_mutex);
	v4l2ctd_vcrtcm_hal_descriptor = v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	if (fps > V4L2CTD_FPS_HARD_LIMIT) {
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		V4L2CTD_ERROR("Frame rate above the hard limit\n");
		return -EINVAL;
	}

	if (fps <= 0) {
		v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies = 0;
		jiffies_snapshot = jiffies;

		v4l2ctd_vcrtcm_hal_descriptor->next_fb_xmit_jiffies =
			jiffies_snapshot;
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		V4L2CTD_DEBUG
			(1, "Transmission disabled by request\n");
	} else {
		v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies =
			HZ / fps;
		jiffies_snapshot = jiffies;
		v4l2ctd_vcrtcm_hal_descriptor->next_vblank_jiffies =
			jiffies_snapshot +
			v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;
		v4l2ctd_vcrtcm_hal_descriptor->next_fb_xmit_jiffies =
			jiffies_snapshot +
			v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;
		mutex_unlock(&v4l2ctd_info->xmit_mutex);

		V4L2CTD_DEBUG(1, "Frame transmission period set to %d jiffies\n",
			HZ / fps);
	}

	/* Schedule initial fake vblank */
	queue_delayed_work(v4l2ctd_info->workqueue,
			&v4l2ctd_info->fake_vblank_work, 0);

	return 0;
}

int v4l2ctd_get_fps(int *fps, void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	V4L2CTD_DEBUG(2, "\n");

	mutex_lock(&v4l2ctd_info->xmit_mutex);
	v4l2ctd_vcrtcm_hal_descriptor = v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	if (v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		V4L2CTD_DEBUG(1, "Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ /
			v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		return 0;
	}
}

int v4l2ctd_page_flip(u32 ioaddr, void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	v4l2ctd_vcrtcm_hal_descriptor =
		v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	V4L2CTD_DEBUG(2, "\n");

	if (v4l2ctd_info->xfer_in_progress) {
		/* there is a transfer in progress, we can't page flip now */
		/* return deferred-completion status */
		v4l2ctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr = ioaddr;
		return VCRTCM_PFLIP_DEFERRED;
	} else {
		/* we can safely page flip */

		v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.ioaddr = ioaddr;
		return 0;
	}
}

int v4l2ctd_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;
	int r = 0;

	V4L2CTD_DEBUG(2, "minor %d\n", v4l2ctd_info->minor);

	/*mutex_lock(&v4l2ctd_info->xmit_mutex);*/

	v4l2ctd_vcrtcm_hal_descriptor =
		v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		/*mutex_unlock(&v4l2ctd_info->xmit_mutex);*/
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	if (!vcrtcm_cursor) {
		/*mutex_unlock(&v4l2ctd_info->xmit_mutex);*/
		V4L2CTD_ERROR("NULL cursor\n");
		return -EINVAL;
	}
	/*pr_info("Setting cursor: height %d width %d cursor_len %d flag %d\n",*/
	/*		vcrtcm_cursor->height, vcrtcm_cursor->width, cursor_len, vcrtcm_cursor->flag);*/

	memcpy(&v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_cursor,
		vcrtcm_cursor, sizeof(struct vcrtcm_cursor));

	if (V4L2CTD_FB_XFER_MODE == V4L2CTD_FB_PUSH) {
		int size_in_bytes, requested_num_pages, i, j;

		/* calculate the push buffer size for cursor */
		size_in_bytes =
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_cursor.height *
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_cursor.width *
				(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_cursor.bpp >> 3);

		requested_num_pages = size_in_bytes / PAGE_SIZE;
		if (size_in_bytes % PAGE_SIZE)
			requested_num_pages++;

		for (i = 0; i < 2; i++) {
			if (!requested_num_pages) {
				V4L2CTD_DEBUG(1, "cursor[%d]: zero size requested, nothing allocated\n", i);
				/* free old buffer if there is one */
				if (v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages) {
					BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].gpu_private);
					if (v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i]) {
						vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i],
								v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages);
						v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i] = NULL;
					}
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i]);

				}
			} else if (v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages == requested_num_pages) {
				V4L2CTD_DEBUG(1, "cursor[%d]: reusing existing push buffer\n", i);
			} else {
				/* if we got there, then we either dont have the push buffer
				 * or we have one of the wrong size (i.e. mode has changed)
				 */
				V4L2CTD_DEBUG(1, "cursor[%d]: allocating push buffer size=%d, "
					"num_pages=%d\n", i, size_in_bytes, requested_num_pages);

				/* free old buffer */
				if (v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages) {
					BUG_ON(!v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].gpu_private);
					if (v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i]) {
						vm_unmap_ram(v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i],
								v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages);
						v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i] = NULL;
					}
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
				}
				/* allocate the buffer */
				v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages =
						requested_num_pages;
				r = vcrtcm_push_buffer_alloc(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
						&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i]);

				/* sanity check */
				if (r) {
					V4L2CTD_ERROR("cursor[%d]: push buffer alloc failed\n", i);
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
					memset(&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i], 0,
							sizeof(struct vcrtcm_push_buffer_descriptor));
					/*mutex_unlock(&v4l2ctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				if (v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages < requested_num_pages) {
					V4L2CTD_ERROR("cursor[%d]: not enough pages allocated\n", i);
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
					/*mutex_unlock(&v4l2ctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				/* vmap the allocated pages */
				v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i] =
						vm_map_ram(v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].pages,
								v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages,
								0, PAGE_KERNEL);

				if (!v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i]) {
					V4L2CTD_ERROR("cursor[%d]: could not do vmap\n", i);
					vcrtcm_push_buffer_free(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
					/*mutex_unlock(&v4l2ctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				v4l2ctd_vcrtcm_hal_descriptor->pb_needs_xmit[i] = 0;
				V4L2CTD_DEBUG(2, "cursor[%d]: allocated %lu pages, last_lomem=%ld, "
						"first_himem=%ld\n", i,
						v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages,
						v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].last_lomem_page,
						v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].first_himem_page);

				for (j = 0; j < v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages; j++)
					V4L2CTD_DEBUG(2, "cursor[%d]: page[%d]=%p\n", i, j,
							v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[i].pages[j]);

				V4L2CTD_DEBUG(2, "cursor[%d]: vmap=%p\n", i, v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[i]);
			}
		}
	}

	/*mutex_unlock(&v4l2ctd_info->xmit_mutex);*/

	return r;
}

int v4l2ctd_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	V4L2CTD_DEBUG(2, "minor %d\n", v4l2ctd_info->minor);

	mutex_lock(&v4l2ctd_info->xmit_mutex);

	v4l2ctd_vcrtcm_hal_descriptor =
		v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		mutex_unlock(&v4l2ctd_info->xmit_mutex);
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_cursor, &v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));

	mutex_unlock(&v4l2ctd_info->xmit_mutex);

	return 0;
}

int v4l2ctd_set_dpms(int state, void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	V4L2CTD_DEBUG(2, "minor %d, state %d\n",
			v4l2ctd_info->minor, state);

	v4l2ctd_vcrtcm_hal_descriptor = v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	v4l2ctd_vcrtcm_hal_descriptor->dpms_state = state;

	/* TODO, send DPMS info */

	return 0;
}

int v4l2ctd_get_dpms(int *state, void *hw_drv_info, int flow)
{
	struct v4l2ctd_info *v4l2ctd_info = (struct v4l2ctd_info *) hw_drv_info;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	V4L2CTD_DEBUG(2, "minor %d\n",
			v4l2ctd_info->minor);

	v4l2ctd_vcrtcm_hal_descriptor = v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	*state = v4l2ctd_vcrtcm_hal_descriptor->dpms_state;

	return 0;
}

void v4l2ctd_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct v4l2ctd_info *v4l2ctd_info =
		container_of(delayed_work, struct v4l2ctd_info, fake_vblank_work);
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;

	unsigned long jiffies_snapshot;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int v4l2ctd_fake_vblank_slack_sane = 0;

	V4L2CTD_DEBUG(1, "minor=%d\n", v4l2ctd_info->minor);
	v4l2ctd_fake_vblank_slack_sane =
			(v4l2ctd_fake_vblank_slack_sane <= 0) ? 0 : v4l2ctd_fake_vblank_slack;

	if (!v4l2ctd_info) {
		V4L2CTD_ERROR("Cannot find v4l2ctd_info\n");
		return;
	}

	v4l2ctd_vcrtcm_hal_descriptor = v4l2ctd_info->v4l2ctd_vcrtcm_hal_descriptor;

	if (!v4l2ctd_vcrtcm_hal_descriptor) {
		V4L2CTD_ERROR("Cannot find HAL descriptor\n");
		return;
	}


	jiffies_snapshot = jiffies;

	if (v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + v4l2ctd_fake_vblank_slack_sane,
				v4l2ctd_vcrtcm_hal_descriptor->next_vblank_jiffies)) {
			v4l2ctd_vcrtcm_hal_descriptor->next_vblank_jiffies +=
					v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;
			if (V4L2CTD_FB_XFER_MODE == V4L2CTD_FB_PUSH)
				v4l2ctd_do_xmit_fb_push(v4l2ctd_vcrtcm_hal_descriptor);
		}

		next_vblank_jiffies =
			v4l2ctd_vcrtcm_hal_descriptor->
				next_vblank_jiffies;
		next_vblank_jiffies_valid = 1;
	}

	if (next_vblank_jiffies_valid) {
		next_vblank_delay =
			(int)next_vblank_jiffies - (int)jiffies_snapshot;
		if (next_vblank_delay <= v4l2ctd_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		if (!queue_delayed_work(v4l2ctd_info->workqueue,
				&v4l2ctd_info->fake_vblank_work, next_vblank_delay))
			V4L2CTD_DEBUG(1, "dup, minor %d\n", v4l2ctd_info->minor);
	} else
		V4L2CTD_DEBUG(1, "Next fake blank not scheduled\n");

	return;
}

int v4l2ctd_wait_idle_core(struct v4l2ctd_info *v4l2ctd_info)
{
	unsigned long jiffies_snapshot, jiffies_snapshot_2;
	int j, r = 0;

	if (v4l2ctd_info->xfer_in_progress) {
		V4L2CTD_DEBUG(1, "v4l2ctd transmission in progress, "
				"work delayed, minor = %d\n",
				v4l2ctd_info->minor);
		jiffies_snapshot = jiffies;
		j = 0;

		while (v4l2ctd_info->xfer_in_progress) {
			if (v4l2ctd_info->enabled_queue) {
				wait_event_timeout(v4l2ctd_info->xmit_sync_queue,
						!v4l2ctd_info->xfer_in_progress,
						V4L2CTD_XFER_TIMEOUT);
				jiffies_snapshot_2 = jiffies;
				j = (int) jiffies_snapshot_2 - (int) jiffies_snapshot;

				if (j > V4L2CTD_XFER_MAX_TRY *
						V4L2CTD_XFER_TIMEOUT) {
					V4L2CTD_ERROR
					("Still busy after all this wait\n");
					r = -EFAULT;
					break;
				}
			} else {
				/* There is no queue to wait on, this is
				 * wrong.
				 */
				V4L2CTD_ERROR("error, no queue\n");
				r = -EFAULT;
				break;
			}
		}

		V4L2CTD_DEBUG(1, "Time spend waiting for v4l2ctd %d ms\n", j * 1000 / HZ);
	}

	return r;
}

int v4l2ctd_do_xmit_fb_push(struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor)
{
	struct v4l2ctd_info *v4l2ctd_info = v4l2ctd_vcrtcm_hal_descriptor->v4l2ctd_info;
	struct vcrtcm_cursor *vcrtcm_cursor;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	unsigned int vp_offset, hlen, p, vpx, vpy, Bpp;
	unsigned int hpixels, vpixels;
	int i, j;
	int r = 0;
	char *mb, *sb;

	V4L2CTD_DEBUG(2, "minor %d\n", v4l2ctd_info->minor);

	mutex_lock(&v4l2ctd_info->xmit_mutex);
	push_buffer_index = v4l2ctd_vcrtcm_hal_descriptor->push_buffer_index;

	if (v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[push_buffer_index].num_pages) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		V4L2CTD_WARNING("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	/* TODO: Ugly hack.  force transmit! */
	if (1 || (((v4l2ctd_vcrtcm_hal_descriptor->fb_force_xmit) ||
			time_after(jiffies_snapshot, v4l2ctd_vcrtcm_hal_descriptor->last_xmit_jiffies +
					V4L2CTD_XMIT_HARD_DEADLINE)) && have_push_buffer)) {
		/* someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 */

		v4l2ctd_vcrtcm_hal_descriptor->fb_force_xmit = 0;
		v4l2ctd_vcrtcm_hal_descriptor->last_xmit_jiffies = jiffies;
		v4l2ctd_vcrtcm_hal_descriptor->fb_xmit_counter++;

		V4L2CTD_DEBUG(1, "%d: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.width,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.height,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.bpp);

		V4L2CTD_DEBUG(1, "%d: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_x,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_y,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.hdisplay,
				v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay);

		r = vcrtcm_push(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
				&v4l2ctd_vcrtcm_hal_descriptor->pbd_fb[push_buffer_index],
				&v4l2ctd_vcrtcm_hal_descriptor->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_emulate_vblank(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal);
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

			v4l2ctd_vcrtcm_hal_descriptor->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			v4l2ctd_vcrtcm_hal_descriptor->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		vcrtcm_emulate_vblank(v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal);
	}

	if (v4l2ctd_vcrtcm_hal_descriptor->pb_needs_xmit[push_buffer_index]) {

		V4L2CTD_DEBUG(1, "%d: initiating transfer\n",
				push_buffer_index);
		v4l2ctd_info->main_buffer = v4l2ctd_vcrtcm_hal_descriptor->pb_fb[push_buffer_index];
		v4l2ctd_info->cursor = v4l2ctd_vcrtcm_hal_descriptor->pb_cursor[push_buffer_index];

		hpixels = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.hdisplay;
		vpixels = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay;
		p = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch;
		vpx = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_x;
		vpy = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_y;
		Bpp = v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_fb.bpp >> 3;
		vp_offset = p * vpy + vpx * Bpp;
		hlen = hpixels * Bpp;

		if (v4l2ctd_info->main_buffer && v4l2ctd_info->cursor) {
			/* Overlay the cursor on the framebuffer */
			vcrtcm_cursor = &v4l2ctd_vcrtcm_hal_descriptor->vcrtcm_cursor;
			if (vcrtcm_cursor->flag != VCRTCM_CURSOR_FLAG_HIDE) {
				uint32_t *fb_end;
				mb = v4l2ctd_info->main_buffer + vp_offset;
				fb_end = (uint32_t *) (mb + p * (vpixels - 1));
				fb_end += hpixels;
				/* loop for each line of the framebuffer. */
				for (i = 0; i < vcrtcm_cursor->height; i++) {
					uint32_t *cursor_pixel;
					uint32_t *fb_pixel;
					uint32_t *fb_line_end;

					cursor_pixel = (uint32_t *) v4l2ctd_info->cursor;
					cursor_pixel += i * vcrtcm_cursor->width;

					fb_pixel = (uint32_t *) (mb + p * (vcrtcm_cursor->location_y + i));
					fb_line_end = fb_pixel + hpixels;
					fb_pixel += vcrtcm_cursor->location_x;

					for (j = 0; j < vcrtcm_cursor->width; j++) {
						if (fb_pixel >= fb_end || fb_pixel >= fb_line_end)
							continue;

						if (*cursor_pixel >> 24 > 0)
							*fb_pixel = *cursor_pixel;

						cursor_pixel++;
						fb_pixel++;
					}
				}
			}
			V4L2CTD_DEBUG(1, "wrote mouse\n");
		}

		/* shadowbuf */
		mutex_lock(&v4l2ctd_info->sb_lock);
		if (v4l2ctd_info->shadowbuf) {
			v4l2ctd_info->jshadowbuf = jiffies;
			mb = v4l2ctd_info->main_buffer + vp_offset;
			sb = v4l2ctd_info->shadowbuf;
			for (i = 0; i < vpixels; i++) {
				memcpy(sb, mb, hlen);
				mb += p;
				sb += hlen;
			}
		}
		mutex_unlock(&v4l2ctd_info->sb_lock);

		v4l2ctd_info->main_buffer = NULL;
		v4l2ctd_info->cursor = NULL;

		v4l2ctd_vcrtcm_hal_descriptor->pb_needs_xmit[push_buffer_index] = 0;
	}

	mutex_unlock(&v4l2ctd_info->xmit_mutex);
	return r;
}
