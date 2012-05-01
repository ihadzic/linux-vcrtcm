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


/*
  Public interface for Virtual CRTC Manager to the GPU
  GPU drivers should include this file only
*/

#ifndef __VCRTCM_GPU_H__
#define __VCRTCM_GPU_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

#include "vcrtcm_common.h"

/* setup/config functions */
int vcrtcm_gpu_attach(int major, int minor, int flow,
		  struct drm_crtc *drm_crtc,
		  struct vcrtcm_gpu_funcs *gpu_callbacks,
		  struct vcrtcm_pcon_info **vcrtcm_pcon_info);
int vcrtcm_gpu_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info);

/* functions for use by GPU driver in operational state */
int vcrtcm_gpu_set_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		  struct vcrtcm_fb *vcrtcm_fb);
int vcrtcm_gpu_get_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		  struct vcrtcm_fb *vcrtcm_fb);
int vcrtcm_gpu_page_flip(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		     u32 ioaddr);
int vcrtcm_gpu_xmit_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info);
int vcrtcm_gpu_wait_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info);
int vcrtcm_gpu_get_fb_status(struct vcrtcm_pcon_info *vcrtcm_pcon_info, u32 *status);
int vcrtcm_gpu_get_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *fps);
int vcrtcm_gpu_set_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int fps);
int vcrtcm_gpu_set_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		      struct vcrtcm_cursor *vcrtcm_cursor);
int vcrtcm_get_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		      struct vcrtcm_cursor *vcrtcm_cursor);
int vcrtcm_gpu_set_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int state);
int vcrtcm_gpu_get_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *state);
int vcrtcm_gpu_get_vblank_time(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			   struct timeval *vblank_time);
void vcrtcm_gpu_set_vblank_time(struct vcrtcm_pcon_info *vcrtcm_pcon_info);
int vcrtcm_gpu_pcon_connected(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *status);
int vcrtcm_gpu_get_modes(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		     struct vcrtcm_mode **modes, int *count);
int vcrtcm_gpu_check_mode(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		      struct vcrtcm_mode *mode, int *status);
void vcrtcm_gpu_disable(struct vcrtcm_pcon_info *vcrtcm_pcon_info);

#endif
