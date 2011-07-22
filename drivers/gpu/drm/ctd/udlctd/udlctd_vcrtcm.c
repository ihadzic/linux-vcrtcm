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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "udlctd.h"
#include "udlctd_vcrtcm.h"
#include "udlctd_utils.h"
#include "vcrtcm/vcrtcm_ctd.h"

int udlctd_attach(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;

	pr_info("Attaching udlctd %d to HAL %p\n",
		udlctd_info->minor, vcrtcm_dev_hal);

	mutex_lock(&udlctd_info->xmit_mutex);

	if (udlctd_info->udlctd_vcrtcm_hal_descriptor) {
		mutex_unlock(&udlctd_info->xmit_mutex);
		pr_err("attach: minor already served\n");
		return -EBUSY;
	} else {
		struct udlctd_vcrtcm_hal_descriptor
			*udlctd_vcrtcm_hal_descriptor =
			udlctd_kzalloc(udlctd_info,
					sizeof(struct udlctd_vcrtcm_hal_descriptor),
					GFP_KERNEL);
		if (udlctd_vcrtcm_hal_descriptor == NULL) {
			mutex_unlock(&udlctd_info->xmit_mutex);
			pr_err("attach: no memory\n");
			return -ENOMEM;
		}

		udlctd_vcrtcm_hal_descriptor->udlctd_info = udlctd_info;
		udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal = vcrtcm_dev_hal;
		udlctd_vcrtcm_hal_descriptor->fb_force_xmit = 0;
		udlctd_vcrtcm_hal_descriptor->fb_xmit_counter = 0;
		udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies = 0;
		udlctd_vcrtcm_hal_descriptor->next_fb_xmit_jiffies = 0;
		udlctd_vcrtcm_hal_descriptor->next_vblank_jiffies = 0;
		udlctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr = 0x0;
		udlctd_vcrtcm_hal_descriptor->push_buffer_index = 0;
		udlctd_vcrtcm_hal_descriptor->pb_needs_xmit[0] = 0;
		udlctd_vcrtcm_hal_descriptor->pb_needs_xmit[1] = 0;
		memset(&udlctd_vcrtcm_hal_descriptor->pbd_fb, 0,
				2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		memset(&udlctd_vcrtcm_hal_descriptor->pbd_cursor, 0,
				2 * sizeof(struct vcrtcm_push_buffer_descriptor));
		udlctd_vcrtcm_hal_descriptor->pb_fb[0] = 0;
		udlctd_vcrtcm_hal_descriptor->pb_fb[1] = 0;
		udlctd_vcrtcm_hal_descriptor->pb_cursor[0] = 0;
		udlctd_vcrtcm_hal_descriptor->pb_cursor[1] = 0;

		udlctd_vcrtcm_hal_descriptor->hw_fb_ptr = 0;
		udlctd_vcrtcm_hal_descriptor->hw_fb_prev_ptr = 0;
		udlctd_vcrtcm_hal_descriptor->ioaddr_prev = 0;

		udlctd_vcrtcm_hal_descriptor->vcrtcm_cursor.flag =
			VCRTCM_CURSOR_FLAG_HIDE;

		udlctd_info->udlctd_vcrtcm_hal_descriptor =
			udlctd_vcrtcm_hal_descriptor;

		mutex_unlock(&udlctd_info->xmit_mutex);

		pr_info("udlctd %d now serves HAL %p\n", udlctd_info->minor,
			vcrtcm_dev_hal);

		return 0;
	}
}

void udlctd_detach(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	pr_info("Detaching udlctd %d from HAL %p\n",
		udlctd_info->minor, vcrtcm_dev_hal);

	mutex_lock(&udlctd_info->xmit_mutex);

	udlctd_vcrtcm_hal_descriptor =
		udlctd_info->udlctd_vcrtcm_hal_descriptor;

	cancel_delayed_work_sync(&udlctd_info->fake_vblank_work);

	if (udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal == vcrtcm_dev_hal) {
		pr_debug("Found descriptor that should be removed.\n");

		if (udlctd_vcrtcm_hal_descriptor->pbd_fb[0].num_pages) {
			BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_fb[0].gpu_private);
			if (udlctd_vcrtcm_hal_descriptor->pb_fb[0]) {
				vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_fb[0],
					udlctd_vcrtcm_hal_descriptor->pbd_fb[0].num_pages);
				udlctd_vcrtcm_hal_descriptor->pb_fb[0] = NULL;
			}
			vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
					&udlctd_vcrtcm_hal_descriptor->pbd_fb[0]);
		}

		if (udlctd_vcrtcm_hal_descriptor->pbd_cursor[0].num_pages) {
			BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_cursor[0].gpu_private);
			if (udlctd_vcrtcm_hal_descriptor->pb_cursor[0]) {
				vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_cursor[0],
					udlctd_vcrtcm_hal_descriptor->pbd_cursor[0].num_pages);
				udlctd_vcrtcm_hal_descriptor->pb_cursor[0] = NULL;
			}
			vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
					&udlctd_vcrtcm_hal_descriptor->pbd_cursor[0]);
		}

		if (udlctd_vcrtcm_hal_descriptor->pbd_fb[1].num_pages) {
			BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_fb[1].gpu_private);
			if (udlctd_vcrtcm_hal_descriptor->pb_fb[1]) {
				vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_fb[1],
					udlctd_vcrtcm_hal_descriptor->pbd_fb[1].num_pages);
				udlctd_vcrtcm_hal_descriptor->pb_fb[1] = NULL;
			}
			vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
						&udlctd_vcrtcm_hal_descriptor->pbd_fb[1]);
		}

		if (udlctd_vcrtcm_hal_descriptor->pbd_cursor[1].num_pages) {
			BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_cursor[1].gpu_private);
			if (udlctd_vcrtcm_hal_descriptor->pb_cursor[1]) {
				vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_cursor[1],
					udlctd_vcrtcm_hal_descriptor->pbd_cursor[1].num_pages);
				udlctd_vcrtcm_hal_descriptor->pb_cursor[1] = NULL;
			}
			vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
					&udlctd_vcrtcm_hal_descriptor->pbd_cursor[1]);
		}

		udlctd_info->udlctd_vcrtcm_hal_descriptor = NULL;
		udlctd_kfree(udlctd_info, udlctd_vcrtcm_hal_descriptor);
		if (udlctd_info->local_cursor)
			udlctd_vfree(udlctd_info, udlctd_info->local_cursor);

		udlctd_info->main_buffer = NULL;
		udlctd_info->cursor = NULL;
		udlctd_info->local_fb = NULL;
		udlctd_info->local_cursor = NULL;
	}

	mutex_unlock(&udlctd_info->xmit_mutex);
}

int udlctd_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;
	struct udlctd_video_mode *udlctd_video_mode;
	int found_mode = 0;
	int r = 0;

	pr_info("In udlctd_set_fb, minor %d.\n", udlctd_info->minor);
	/*mutex_lock(&udlctd_info->xmit_mutex);*/

	udlctd_vcrtcm_hal_descriptor =
		udlctd_info->udlctd_vcrtcm_hal_descriptor;

	/* TODO: Do we need this? */
	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		/*mutex_unlock(&udlctd_info->xmit_mutex);*/
		return -EINVAL;
	}

	memcpy(&udlctd_vcrtcm_hal_descriptor->vcrtcm_fb,
		vcrtcm_fb, sizeof(struct vcrtcm_fb));

	/* Find a matching video mode and switch the DL device to that mode */
	list_for_each_entry(udlctd_video_mode,
			&udlctd_info->fb_mode_list, list) {
		pr_info("checking %dx%d\n",
				udlctd_video_mode->xres, udlctd_video_mode->yres);
		pr_info("against %dx%d\n", vcrtcm_fb->hdisplay, vcrtcm_fb->vdisplay);
		if (udlctd_video_mode->xres == vcrtcm_fb->hdisplay &&
				udlctd_video_mode->yres == vcrtcm_fb->vdisplay) {
			udlctd_info->current_video_mode = udlctd_video_mode;
			udlctd_info->bpp = vcrtcm_fb->bpp;
			udlctd_setup_screen(udlctd_info,
					udlctd_info->current_video_mode,
					udlctd_info->bpp);
			found_mode = 1;
			break;
		}
	}

	if (!found_mode) {
		pr_err("could not find matching mode...\n");
		/*mutex_unlock(&udlctd_info->xmit_mutex);*/
		return -EINVAL;
	}

	if (UDLCTD_FB_XFER_MODE == UDLCTD_FB_PUSH) {
		int size_in_bytes, requested_num_pages, i, j;

		size_in_bytes = udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch *
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay;

		requested_num_pages = size_in_bytes / PAGE_SIZE;
		if (size_in_bytes % PAGE_SIZE)
			requested_num_pages++;

		for (i = 0; i < 2; i++) {
			if (!requested_num_pages) {
				pr_debug("framebuffer[%d]: zero size requested\n", i);
				/* free old buffer if there is one */
				if (udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages) {
					BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_fb[i].gpu_private);
					if (udlctd_vcrtcm_hal_descriptor->pb_fb[i]) {
						vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_fb[i],
								udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages);
						udlctd_vcrtcm_hal_descriptor->pb_fb[i] = NULL;
					}
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_fb[i]);

				}
			} else if (udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages == requested_num_pages) {
				pr_debug("framebuffer[%d]: reusing existing push buffer\n", i);
			} else {
				/* if we have got here, then we either don't have the push buffer */
				/* or we have one of the wrong size (i.e. mode has changed) */

				pr_info("framebuffer[%d]: allocating push buffer, size=%d, "
						"num_pages=%d\n", i, size_in_bytes, requested_num_pages);

				/* free old buffer */
				if (udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages) {
					BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_fb[i].gpu_private);
					if (udlctd_vcrtcm_hal_descriptor->pb_fb[i]) {
						vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_fb[i],
								udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages);
						udlctd_vcrtcm_hal_descriptor->pb_fb[i] = NULL;
					}
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_fb[i]);

				}

				/* allocate new buffer */
				udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages =
						requested_num_pages;
				r = vcrtcm_push_buffer_alloc(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
						&udlctd_vcrtcm_hal_descriptor->pbd_fb[i]);

				/* sanity check */
				if (r) {
					pr_err("framebuffer[%d]: push buffer alloc failed\n", i);
					memset(&udlctd_vcrtcm_hal_descriptor->pbd_fb, 0,
							sizeof(struct vcrtcm_push_buffer_descriptor));
					/*mutex_unlock(&udlctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				if (udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages < requested_num_pages) {
					pr_err("framebuffer[%d]: not enough pages allocated\n", i);
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_fb[i]);
					/*mutex_unlock(&udlctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				/* vmap the allocated pages */
				udlctd_vcrtcm_hal_descriptor->pb_fb[i] =
					vm_map_ram(udlctd_vcrtcm_hal_descriptor->pbd_fb[i].pages,
							udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages,
							0, PAGE_KERNEL);

				if (!udlctd_vcrtcm_hal_descriptor->pb_fb[i]) {
					pr_err("framebuffer[%d]: could not do vmap\n", i);
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_fb[i]);
					/*mutex_unlock(&udlctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				udlctd_vcrtcm_hal_descriptor->pb_needs_xmit[i] = 0;
				pr_debug("framebuffer[%d]: allocated %lu pages, last_lomem=%ld, "
						"first_himem=%ld\n", i,
						udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages,
						udlctd_vcrtcm_hal_descriptor->pbd_fb[i].last_lomem_page,
						udlctd_vcrtcm_hal_descriptor->pbd_fb[i].first_himem_page);

				for (j = 0; j < udlctd_vcrtcm_hal_descriptor->pbd_fb[i].num_pages; j++)
					pr_debug("framebuffer[%d]: page [%d]=%p\n", i, j,
							udlctd_vcrtcm_hal_descriptor->pbd_fb[i].pages[j]);

				pr_debug("framebuffer[%d]: vmap=%p\n", i, udlctd_vcrtcm_hal_descriptor->pb_fb[i]);
			}
		}
	}

	/*mutex_unlock(&udlctd_info->xmit_mutex);*/
	return r;
}

int udlctd_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *hw_drv_info,
			int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	pr_info("In udlctd_get_fb, minor %d.\n", udlctd_info->minor);
	udlctd_vcrtcm_hal_descriptor = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	memcpy(vcrtcm_fb, &udlctd_vcrtcm_hal_descriptor->vcrtcm_fb,
		sizeof(struct vcrtcm_fb));

	return 0;
}

int udlctd_xmit_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	pr_debug("in udlctd_xmit_fb, minor %d\n", udlctd_info->minor);

	/* just mark the "force" flag, udlctd_do_xmit_fb_pull
	 * does the rest (when called).
	 */

	mutex_lock(&udlctd_info->xmit_mutex);
	udlctd_vcrtcm_hal_descriptor = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (udlctd_vcrtcm_hal_descriptor)
		udlctd_vcrtcm_hal_descriptor->fb_force_xmit = 1;
	mutex_unlock(&udlctd_info->xmit_mutex);

	return 0;
}

int udlctd_wait_fb(struct drm_crtc *drm_crtc, void *hw_drv_info, int flow)
{
	return udlctd_wait_idle_core((struct udlctd_info *) hw_drv_info);
}

int udlctd_get_fb_status(struct drm_crtc *drm_crtc,
		void *hw_drv_info, int flow, u32 *status)
{
	u32 tmp_status = 0;
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;

	/* we are transmitting if udlctd is busy */
	if (udlctd_info->xfer_in_progress)
		tmp_status |= VCRTCM_FB_STATUS_XMIT;
	else
		tmp_status |= VCRTCM_FB_STATUS_IDLE;

	*status = tmp_status;

	return 0;
}


int udlctd_set_fps(int fps, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;
	unsigned long jiffies_snapshot;

	pr_info("udlctd_set_fps, fps %d.\n", fps);

	mutex_lock(&udlctd_info->xmit_mutex);
	udlctd_vcrtcm_hal_descriptor = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		mutex_unlock(&udlctd_info->xmit_mutex);
		return -EINVAL;
	}

	if (fps > UDLCTD_FPS_HARD_LIMIT) {
		mutex_unlock(&udlctd_info->xmit_mutex);
		pr_err("Frame rate above the hard limit\n");
		return -EINVAL;
	}

	if (fps <= 0) {
		udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies = 0;
		jiffies_snapshot = jiffies;

		udlctd_vcrtcm_hal_descriptor->next_fb_xmit_jiffies =
			jiffies_snapshot;
		mutex_unlock(&udlctd_info->xmit_mutex);
		pr_info
		("Transmission disabled by request (negative or zero fps)\n");
	} else {
		udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies =
			HZ / fps;
		jiffies_snapshot = jiffies;
		udlctd_vcrtcm_hal_descriptor->next_vblank_jiffies =
			jiffies_snapshot +
			udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;
		udlctd_vcrtcm_hal_descriptor->next_fb_xmit_jiffies =
			jiffies_snapshot +
			udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;
		mutex_unlock(&udlctd_info->xmit_mutex);

		pr_info("Frame transmission period set to %d jiffies\n",
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
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	pr_info("udlctd_get_fps.\n");

	mutex_lock(&udlctd_info->xmit_mutex);
	udlctd_vcrtcm_hal_descriptor = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		mutex_unlock(&udlctd_info->xmit_mutex);
		return -EINVAL;
	}

	if (udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies <= 0) {
		*fps = 0;
		mutex_unlock(&udlctd_info->xmit_mutex);
		pr_info
		("Zero or negative frame rate, transmission disabled\n");
		return 0;
	} else {
		*fps = HZ /
			udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;
		mutex_unlock(&udlctd_info->xmit_mutex);
		return 0;
	}
}

int udlctd_page_flip(u32 ioaddr, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	udlctd_vcrtcm_hal_descriptor =
		udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	pr_info("In page flip\n");

	if (udlctd_info->xfer_in_progress) {
		/* there is a transfer in progress, we can't page flip now */
		/* return deferred-completion status */
		udlctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr = ioaddr;
		return VCRTCM_PFLIP_DEFERRED;
	} else {
		/* we can safely page flip */
		/*udlctd_map_new_hw_fb_addr(udlctd_info,
				&udlctd_vcrtcm_hal_descriptor->vcrtcm_fb,
				ioaddr);
*/
		udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.ioaddr = ioaddr;
		return 0;
	}
}

void copy_cursor_work(struct work_struct *work)
{
	struct udlctd_info *udlctd_info =
			container_of(work, struct udlctd_info, copy_cursor_work);
	struct vcrtcm_cursor *vcrtcm_cursor = &udlctd_info->udlctd_vcrtcm_hal_descriptor->vcrtcm_cursor;

	int cursor_len = udlctd_info->bpp/8 * vcrtcm_cursor->height * vcrtcm_cursor->width;

	/*unsigned int start_jiffies = jiffies;*/
	int i;

	uint32_t *hw_addr = ioremap(vcrtcm_cursor->ioaddr, cursor_len);
	uint32_t *sw_addr = (uint32_t *) udlctd_info->local_cursor;
	uint32_t *hw_addr_start = hw_addr;

	for (i = 0; i < cursor_len/4; i++) {
		*sw_addr = ioread32(hw_addr);
		sw_addr++;
		hw_addr++;
		schedule();
	}
	iounmap(hw_addr_start);
	/*pr_info("Copied cursor in %u ms\n", jiffies_to_msecs(jiffies-start_jiffies));*/
}

int udlctd_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;
	int cursor_len;
	int r = 0;

	pr_info("In udlctd_set_cursor, minor %d\n", udlctd_info->minor);

	/*mutex_lock(&udlctd_info->xmit_mutex);*/

	udlctd_vcrtcm_hal_descriptor =
		udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		/*mutex_unlock(&udlctd_info->xmit_mutex);*/
		return -EINVAL;
	}

	memcpy(&udlctd_vcrtcm_hal_descriptor->vcrtcm_cursor,
		vcrtcm_cursor, sizeof(struct vcrtcm_cursor));

	if (UDLCTD_FB_XFER_MODE == UDLCTD_FB_PULL) {
		cursor_len = udlctd_info->bpp/8 * vcrtcm_cursor->height
					* vcrtcm_cursor->width;

		if (cursor_len > udlctd_info->cursor_len) {
			if (udlctd_info->local_cursor)
				udlctd_vfree(udlctd_info, udlctd_info->local_cursor);
			udlctd_info->local_cursor = udlctd_vmalloc(udlctd_info, cursor_len);
			udlctd_info->cursor_len = cursor_len;
			pr_info("allocated cursor\n");
		}

		cancel_work_sync(&udlctd_info->copy_cursor_work);
		queue_work(udlctd_info->workqueue, &udlctd_info->copy_cursor_work);

	} else if (UDLCTD_FB_XFER_MODE == UDLCTD_FB_PUSH) {
		int size_in_bytes, requested_num_pages, i, j;

		/* calculate the push buffer size for cursor */
		size_in_bytes =
				udlctd_vcrtcm_hal_descriptor->vcrtcm_cursor.height *
				udlctd_vcrtcm_hal_descriptor->vcrtcm_cursor.width *
				(udlctd_vcrtcm_hal_descriptor->vcrtcm_cursor.bpp >> 3);

		udlctd_info->cursor_len = size_in_bytes;

		requested_num_pages = size_in_bytes / PAGE_SIZE;
		if (size_in_bytes % PAGE_SIZE)
			requested_num_pages++;

		for (i = 0; i < 2; i++) {
			if (!requested_num_pages) {
				pr_debug("cursor[%d]: zero size requested, nothing allocated\n", i);
				/* free old buffer if there is one */
				if (udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages) {
					BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].gpu_private);
					if (udlctd_vcrtcm_hal_descriptor->pb_cursor[i]) {
						vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_cursor[i],
								udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages);
						udlctd_vcrtcm_hal_descriptor->pb_cursor[i] = NULL;
					}
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
				}
			} else if (udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages == requested_num_pages) {
				pr_debug("cursor[%d]: reusing existing push buffer\n", i);
			} else {
				/* if we got there, then we either dont have the push buffer
				 * or we have one of the wrong size (i.e. mode has changed)
				 */
				pr_info("cursor[%d]: allocating push buffer size=%d, "
					"num_pages=%d\n", i, size_in_bytes, requested_num_pages);

				/* free old buffer */
				if (udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages) {
					BUG_ON(!udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].gpu_private);
					if (udlctd_vcrtcm_hal_descriptor->pb_cursor[i]) {
						vm_unmap_ram(udlctd_vcrtcm_hal_descriptor->pb_cursor[i],
								udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages);
						udlctd_vcrtcm_hal_descriptor->pb_cursor[i] = NULL;
					}
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
				}

				/* allocate the buffer */
				udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages =
						requested_num_pages;
				r = vcrtcm_push_buffer_alloc(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
						&udlctd_vcrtcm_hal_descriptor->pbd_cursor[i]);

				/* sanity check */
				if (r) {
					pr_err("cursor[%d]: push buffer alloc failed\n", i);
					memset(&udlctd_vcrtcm_hal_descriptor->pbd_cursor[i], 0,
							sizeof(struct vcrtcm_push_buffer_descriptor));
					/*mutex_unlock(&udlctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				if (udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages < requested_num_pages) {
					pr_err("cursor[%d]: not enough pages allocated\n", i);
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
					/*mutex_unlock(&udlctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				/* vmap the allocated pages */
				udlctd_vcrtcm_hal_descriptor->pb_cursor[i] =
					vm_map_ram(udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].pages,
							udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages,
							0, PAGE_KERNEL);

				if (!udlctd_vcrtcm_hal_descriptor->pb_cursor[i]) {
					pr_err("cursor[%d]: could not do vmap\n", i);
					vcrtcm_push_buffer_free(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
							&udlctd_vcrtcm_hal_descriptor->pbd_cursor[i]);
					/*mutex_unlock(&udlctd_info->xmit_mutex);*/
					return -ENOMEM;
				}

				udlctd_vcrtcm_hal_descriptor->pb_needs_xmit[i] = 0;
				pr_debug("cursor[%d]: allocated %lu pages, last_lomem=%ld, "
						"first_himem=%ld\n", i,
						udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages,
						udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].last_lomem_page,
						udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].first_himem_page);

				for (j = 0; j < udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].num_pages; j++)
					pr_debug("cursor[%d]: page[%d]=%p\n", i, j,
							udlctd_vcrtcm_hal_descriptor->pbd_cursor[i].pages[j]);

				pr_debug("cursor[%d]: vmap=%p\n", i, udlctd_vcrtcm_hal_descriptor->pb_cursor[i]);
			}
		}
	}

	/*mutex_unlock(&udlctd_info->xmit_mutex);*/
	return r;
}

int udlctd_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	pr_debug("In udlctd_set_cursor, minor %d\n", udlctd_info->minor);

	mutex_lock(&udlctd_info->xmit_mutex);

	udlctd_vcrtcm_hal_descriptor =
		udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		mutex_unlock(&udlctd_info->xmit_mutex);
		return -EINVAL;
	}

	memcpy(vcrtcm_cursor, &udlctd_vcrtcm_hal_descriptor->vcrtcm_cursor,
		sizeof(struct vcrtcm_cursor));

	mutex_unlock(&udlctd_info->xmit_mutex);

	return 0;
}

int udlctd_set_dpms(int state, void *hw_drv_info, int flow)
{
	struct udlctd_info *udlctd_info = (struct udlctd_info *) hw_drv_info;
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	pr_info("in udlctd_set_dpms, minor %d, state %d\n",
			udlctd_info->minor, state);

	udlctd_vcrtcm_hal_descriptor = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	udlctd_vcrtcm_hal_descriptor->dpms_state = state;

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
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;

	pr_info("in udlctd_get_dpms, minor %d\n",
			udlctd_info->minor);

	udlctd_vcrtcm_hal_descriptor = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("Cannot find HAL descriptor\n");
		return -EINVAL;
	}

	*state = udlctd_vcrtcm_hal_descriptor->dpms_state;

	return 0;
}

void udlctd_fake_vblank(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct udlctd_info *udlctd_info =
		container_of(delayed_work, struct udlctd_info, fake_vblank_work);
	struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor;
	/*static long last_snapshot = 0;*/

	unsigned long jiffies_snapshot = 0;
	unsigned long next_vblank_jiffies = 0;
	int next_vblank_jiffies_valid = 0;
	int next_vblank_delay;
	int udlctd_fake_vblank_slack_sane = 0;

	pr_debug("vblank fake, minor=%d\n", udlctd_info->minor);
	udlctd_fake_vblank_slack_sane =
			(udlctd_fake_vblank_slack_sane <= 0) ? 0 : udlctd_fake_vblank_slack;


	if (!udlctd_info) {
		pr_err("udlctd_fake_vblank: Cannot find udlctd_info\n");
		return;
	}

	udlctd_vcrtcm_hal_descriptor = udlctd_info->udlctd_vcrtcm_hal_descriptor;

	if (!udlctd_vcrtcm_hal_descriptor) {
		pr_err("udlctd_fake_vblank: Cannot find HAL descriptor\n");
		return;
	}

	jiffies_snapshot = jiffies;

	if (udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies > 0) {
		if (time_after_eq(jiffies_snapshot + udlctd_fake_vblank_slack_sane,
				udlctd_vcrtcm_hal_descriptor->next_vblank_jiffies)) {
			udlctd_vcrtcm_hal_descriptor->next_vblank_jiffies +=
					udlctd_vcrtcm_hal_descriptor->fb_xmit_period_jiffies;

			if (UDLCTD_FB_XFER_MODE == UDLCTD_FB_PUSH)
				udlctd_do_xmit_fb_push(udlctd_vcrtcm_hal_descriptor);
			else if (UDLCTD_FB_XFER_MODE == UDLCTD_FB_PULL)
				udlctd_do_xmit_fb_pull(udlctd_vcrtcm_hal_descriptor);
		}

		next_vblank_jiffies =
			udlctd_vcrtcm_hal_descriptor->
					next_vblank_jiffies;
		next_vblank_jiffies_valid = 1;

	}

	if (next_vblank_jiffies_valid) {
		next_vblank_delay =
			(int)next_vblank_jiffies - (int)jiffies_snapshot;
		if (next_vblank_delay <= udlctd_fake_vblank_slack_sane)
			next_vblank_delay = 0;
		/* TODO: Set next_blank_jiffies */
		if (!queue_delayed_work(udlctd_info->workqueue, &udlctd_info->fake_vblank_work, next_vblank_delay-5))
			pr_warn("dup fake vblank, minor %d\n", udlctd_info->minor);
	} else
		pr_debug("Next fake vblank not scheduled\n");

	return;
}

int udlctd_wait_idle_core(struct udlctd_info *udlctd_info)
{
	unsigned long jiffies_snapshot, jiffies_snapshot_2;
	int j, r = 0;

	if (udlctd_info->xfer_in_progress) {
		pr_info("udlctd transmission in progress, "
				"work delayed, minor = %d\n",
				udlctd_info->minor);
		jiffies_snapshot = jiffies;
		j = 0;

		while (udlctd_info->xfer_in_progress) {
			if (udlctd_info->enabled_queue) {
				wait_event_timeout(udlctd_info->xmit_sync_queue,
						!udlctd_info->xfer_in_progress,
						UDLCTD_XFER_TIMEOUT);
				jiffies_snapshot_2 = jiffies;
				j = (int) jiffies_snapshot_2 -
					(int) jiffies_snapshot;

				if (j > UDLCTD_XFER_MAX_TRY *
						UDLCTD_XFER_TIMEOUT) {
					pr_info
					("Still busy after all this wait\n");
					r = -EFAULT;
					break;
				}
			} else {
				/* There is no queue to wait on, this is
				 * wrong.
				 */
				pr_info("error, no queue\n");
				r = -EFAULT;
				break;
			}
		}

		pr_info("Time spent waiting for udlctd %d ms\n", j * 1000 / HZ);
	}

	return r;
}

int udlctd_do_xmit_fb_pull(struct udlctd_vcrtcm_hal_descriptor
		*udlctd_vcrtcm_hal_descriptor)
{
	struct udlctd_info *udlctd_info = udlctd_vcrtcm_hal_descriptor->udlctd_info;
	u32 fb_ioaddr, fb_offset;
	u32 *hw_fb_ptr, *hw_fb_ptr_work;
	u32 *sw_fb_ptr;
	unsigned long jiffies_snapshot, jiffies_tmp;
	unsigned int copy_len;
	unsigned int hpixels, vpixels, pitch;
	int i, j;

	pr_info("In udlctd_do_xmit_fb_pull, minor %d\n", udlctd_info->minor);

	mutex_lock(&udlctd_info->xmit_mutex);

	jiffies_snapshot = jiffies;
	udlctd_wait_idle_core(udlctd_info);

	if ((udlctd_vcrtcm_hal_descriptor->fb_force_xmit) ||
			time_after(jiffies_snapshot,
				udlctd_vcrtcm_hal_descriptor->last_xmit_jiffies +
				UDLCTD_XMIT_HARD_HEADLINE)) {

		udlctd_info->xfer_in_progress = 1;

		udlctd_vcrtcm_hal_descriptor->fb_force_xmit = 0;
		udlctd_vcrtcm_hal_descriptor->last_xmit_jiffies = jiffies;
		udlctd_vcrtcm_hal_descriptor->fb_xmit_counter++;

		pr_info("udlctd_do_xmit_fb_pull: framebuffer "
			"pitch %d width %d height %d bpp %d\n",
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch,
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.width,
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.height,
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.bpp);

		pr_info("udlctd_do_xmit_fb_pull: "
			"crtc x %d y %d hdisplay %d vdisplay %d\n",
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_x,
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_y,
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.hdisplay,
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay);

		/* Adjust the address for CRTC viewport offset */
		fb_offset =
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch *
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_y +
			(udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.bpp >> 3) *
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_x;

		fb_ioaddr = udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.ioaddr;
		fb_ioaddr += fb_offset;

		pr_info("udlctd_do_xmit_fb_pull: fb start I/O address 0x%08x, "
			"fb_xmit I/O address 0x%08x\n",
			(unsigned int) udlctd_vcrtcm_hal_descriptor->
			vcrtcm_fb.ioaddr, (unsigned int) fb_ioaddr);


		/* get pixels from video card */
		copy_len =
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch *
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay;

		hw_fb_ptr = ioremap(udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.ioaddr + fb_offset,
				copy_len);
		hw_fb_ptr_work = hw_fb_ptr;

		if (!hw_fb_ptr) {
			pr_err("could not map io memory");
			mutex_unlock(&udlctd_info->xmit_mutex);
			return -1;
		}

		sw_fb_ptr = (void *)udlctd_info->local_fb;

		jiffies_tmp = jiffies;
		hpixels = udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.hdisplay;
		vpixels = udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay;
		pitch = udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch;

		jiffies_tmp = jiffies;
		for (i = 1; i <= vpixels; i++)
			for (j = 1; j <= hpixels; j++) {
				*sw_fb_ptr = ioread32(hw_fb_ptr_work);
				if (j == hpixels) {
					hw_fb_ptr_work += 1 + (pitch/4) - hpixels;
				} else
					hw_fb_ptr_work++;
				sw_fb_ptr++;
		}

		pr_info("FB copy took %d ms.", jiffies_to_msecs(jiffies-jiffies_tmp));
		iounmap(hw_fb_ptr);

		udlctd_info->main_buffer = udlctd_info->local_fb;
		udlctd_info->cursor = udlctd_info->local_cursor;

		/* Start the transmission to the DisplayLink device */
		udlctd_transmit_framebuffer(udlctd_info);
	}

	vcrtcm_emulate_vblank(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal);

	if (udlctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr) {
		pr_info("udlctd_do_xmit_fb_pull: deferred page flip handled, "
			"old %08x, new %08x\n",
			udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.ioaddr,
			udlctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr);

		udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.ioaddr =
				udlctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr;
		udlctd_vcrtcm_hal_descriptor->pending_pflip_ioaddr = 0x0;
	}

	pr_info("Entire xmit took %d ms.", jiffies_to_msecs(jiffies-jiffies_snapshot));
	udlctd_info->xfer_in_progress = 0;
	mutex_unlock(&udlctd_info->xmit_mutex);

	return 0;
}

int udlctd_do_xmit_fb_push(struct udlctd_vcrtcm_hal_descriptor *udlctd_vcrtcm_hal_descriptor)
{
	struct udlctd_info *udlctd_info = udlctd_vcrtcm_hal_descriptor->udlctd_info;
	unsigned long jiffies_snapshot;
	int push_buffer_index, have_push_buffer;
	int r = 0;

	pr_debug("in udlctd_do_xmit_fb_push, minor %d\n", udlctd_info->minor);

	mutex_lock(&udlctd_info->xmit_mutex);
	push_buffer_index = udlctd_vcrtcm_hal_descriptor->push_buffer_index;

	if (udlctd_vcrtcm_hal_descriptor->pbd_fb[push_buffer_index].num_pages) {
		have_push_buffer = 1;
	} else {
		have_push_buffer = 0;
		pr_warn("no push buffer[%d], transmission skipped\n",
				push_buffer_index);
	}

	jiffies_snapshot = jiffies;

	/* TODO: Fix this */
	if (1 || (((udlctd_vcrtcm_hal_descriptor->fb_force_xmit) ||
			time_after(jiffies_snapshot, udlctd_vcrtcm_hal_descriptor->last_xmit_jiffies +
					UDLCTD_XMIT_HARD_HEADLINE)) && have_push_buffer)) {
		/* someone has either indicated that there has been rendering
		 * activity or we went for max time without transmission, so we
		 * should transmit for real.
		 */

		pr_info("transmission happening...\n");
		udlctd_vcrtcm_hal_descriptor->fb_force_xmit = 0;
		udlctd_vcrtcm_hal_descriptor->last_xmit_jiffies = jiffies;
		udlctd_vcrtcm_hal_descriptor->fb_xmit_counter++;

		pr_info("udlctd_do_xmit_fb_push[%d]: frame buffer pitch %d width %d height %d bpp %d\n",
				push_buffer_index,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.pitch,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.width,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.height,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.bpp);

		pr_info("udlctd_do_xmit_fb_push[%d]: crtc x %d crtc y %d hdisplay %d vdisplay %d\n",
				push_buffer_index,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_x,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.viewport_y,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.hdisplay,
				udlctd_vcrtcm_hal_descriptor->vcrtcm_fb.vdisplay);

		r = vcrtcm_push(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal,
				&udlctd_vcrtcm_hal_descriptor->pbd_fb[push_buffer_index],
				&udlctd_vcrtcm_hal_descriptor->pbd_cursor[push_buffer_index]);

		if (r) {
			/* if push did not succeed, then vblank won't happen in the GPU */
			/* so we have to make it out here */
			vcrtcm_emulate_vblank(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal);
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

			udlctd_vcrtcm_hal_descriptor->pb_needs_xmit[push_buffer_index] = 1;
			push_buffer_index = (push_buffer_index + 1) & 0x1;
			udlctd_vcrtcm_hal_descriptor->push_buffer_index = push_buffer_index;
		}
	} else {
		/* transmission didn't happen so we need to fake out a vblank */
		vcrtcm_emulate_vblank(udlctd_vcrtcm_hal_descriptor->vcrtcm_dev_hal);
		pr_info("transmission not happening\n");
	}

	if (udlctd_vcrtcm_hal_descriptor->pb_needs_xmit[push_buffer_index]) {

		pr_info("udlctd_do_xmit_fb_push[%d]: initiating USB transfer\n",
				push_buffer_index);

		udlctd_info->main_buffer = udlctd_vcrtcm_hal_descriptor->pb_fb[push_buffer_index];
		udlctd_info->cursor = udlctd_vcrtcm_hal_descriptor->pb_cursor[push_buffer_index];

		udlctd_transmit_framebuffer(udlctd_info);

		udlctd_vcrtcm_hal_descriptor->pb_needs_xmit[push_buffer_index] = 0;
	}

	mutex_unlock(&udlctd_info->xmit_mutex);
	return r;
}
