/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Authors:
 *      Ilija Hadzic <ihadzic@research.bell-labs.com>
 *      Martin Carroll <martin.carroll@research.bell-labs.com>
 *      William Katsak <wkatsak@cs.rutgers.edu>
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

#ifndef __VCRTCM_PCON_H__
#define __VCRTCM_PCON_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <linux/dma-buf.h>
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_gpu.h>

/*
 * A pcon.  This struct is created when the pcon is created via
 * the user-space "instantiate" ioctl and freed when the pcon is
 * destroyed either by the user-space "destroy" ioctl or by the
 * pim itself.  Neither the gpu driver nor the pim has access to
 * this struct; the gpu driver and pim manipulate pcons via the
 * vcrtcm api using pcon ids.
 */
struct vcrtcm_pcon {
	char description[PCON_DESC_MAXLEN];
	struct vcrtcm_pim *pim;
	struct vcrtcm_p_pcon_funcs pim_funcs;
	struct vcrtcm_g_pcon_funcs gpu_funcs;
	int pcon_callbacks_enabled;
	enum vcrtcm_xfer_mode xfer_mode;
	void *pcon_cookie;
	int pconid; /* index into table maintained by vcrtcm */
	int minor; /* -1 if pcon has no user-accessible minor */
	struct kobject kobj;
	struct list_head pcons_in_pim_list;
	int being_destroyed;
	/* records the time when last (emulated) vblank occurred */
	struct timeval vblank_time;
	int vblank_time_valid;
	/* identifies the CRTC using this PCON */
	struct drm_crtc *drm_crtc;
	int alloc_cnt;
	int page_alloc_cnt;
	int log_alloc_cnts;
	int log_alloc_bugs;
	struct delayed_work vblank_work;
	int fps;
	unsigned long vblank_period_jiffies;
	unsigned long last_vblank_jiffies;
	unsigned long next_vblank_jiffies;
	int vblank_slack_jiffies;
};

void vcrtcm_destroy_pcon(struct vcrtcm_pcon *pcon);
void vcrtcm_prepare_detach(struct vcrtcm_pcon *pcon);
void vcrtcm_set_crtc(struct vcrtcm_pcon *pcon, struct drm_crtc *crtc);

#endif
