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
  Public interface for Virtual CRTC Manager to the CTD
  CTD drivers should include this file only
*/

#ifndef __VCRTCM_CTD_H__
#define __VCRTCM_CTD_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

#include "vcrtcm_common.h"

/* setup/config functions for CTD driver's use */
int vcrtcm_hw_add(struct vcrtcm_funcs *vcrtcm_funcs,
		  int major, int minor, int flow, void *hw_drv_info);
void vcrtcm_hw_del(int major, int minor, int flow);

/* functions for use by CTD driver in operational state */
void vcrtcm_emulate_vblank(struct vcrtcm_dev_hal *vcrtcm_dev_hal);
void vcrtcm_gpu_sync(struct vcrtcm_dev_hal *vcrtcm_dev_hal);

#endif
