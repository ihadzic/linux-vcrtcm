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

#include "vcrtcm/vcrtcm_common.h"

/* Private data structures for Virtual CRTC Manager and modules
   that use it: GPU driver and compression/transmission/display
   (CTD) cards */

#define VCRTCM_STATUS_HAL_IN_USE 0x01

/* main structure for keeping track of each CTD-CRTC relationship */
struct vcrtcm_dev_info {
	struct list_head list;
	/* see VCRTCM_STATUS_HAL constants above for possible status bits */
	int status;
	/* identifies the driver/hardware that implements this HAL */
	int hw_major;
	int hw_minor;
	int hw_flow;
	/* pointer back to the (hardware) specific CTD driver structure */
	void *hw_drv_info;
	/* identifies the CRTC using this HAL */
	struct drm_crtc *drm_crtc;
	/* callback into GPU driver when detach is called */
	void (*detach_gpu_callback) (struct drm_crtc *drm_crtc);
	/* callback into the GPU driver for VBLANK emulation function  */
	/* if one is needed by the HAL (typically used by virtual CRTCs)  */
	void (*vblank_gpu_callback) (struct drm_crtc *drm_crtc);
	/* callback into the GPU driver for synchronization with */
	/* GPU rendering (e.g. fence wait) */
	void (*sync_gpu_callback) (struct drm_crtc *drm_crtc);
	/* public HAL information */
	struct vcrtcm_dev_hal vcrtcm_dev_hal;
};

extern struct list_head vcrtcm_dev_list;
extern struct mutex vcrtcm_dev_list_mutex;

#endif
