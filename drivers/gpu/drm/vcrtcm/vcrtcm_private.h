/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Author: Ilija Hadzic <ihadzic@research.bell-labs.com>

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


#ifndef __VCRTCM_PRIVATE_H__
#define __VCRTCM_PRIVATE_H__

#include <vcrtcm/vcrtcm_common.h>

/* Private data structures for Virtual CRTC Manager and modules
   that use it: GPU driver and pixel consumer (PCON) */

#define VCRTCM_STATUS_PCON_IN_USE 0x01

/* main structure for keeping track of each PCON-CRTC relationship */
struct vcrtcm_pcon_flow_info_private {
	struct list_head list;
	/* general lock for fields subject to concurrent access */
	spinlock_t lock;
	/* see VCRTCM_STATUS_PCON constants above for possible status bits */
	int status;
	/* identifies the driver/hardware that implements this PCON */
	int hw_major;
	int hw_minor;
	int hw_flow;
	/* records the time when last (emulated) vblank occurred */
	struct timeval vblank_time;
	int vblank_time_valid;
	/* pointer back to the (hardware) specific PCON structure */
	void *pcon_cookie;
	/* identifies the CRTC using this PCON */
	struct drm_crtc *drm_crtc;
	/* functional interface to GPU driver */
	struct vcrtcm_gpu_funcs gpu_funcs;
	/* public PCON information */
	struct vcrtcm_pcon_info vcrtcm_pcon_info;
};

extern struct list_head vcrtcm_dev_list;
extern struct mutex vcrtcm_dev_list_mutex;

#endif
