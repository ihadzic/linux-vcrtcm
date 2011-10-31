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
   TODO:
   * VCRTCM functions for hotplug probe and mode
     query so that GPU can ask the CTD driver what it can support
*/

#include "vcrtcm_utils.h"
#include "vcrtcm_private.h"

/* called by the GPU driver to attach its CRTC to the
   compression/transmission/display (CTD) device
   GPU driver *must* supply the pointer to its own drm_crtc
   structure that describes the CRTC and the pointer to a
   callback function to be called on detach */
int vcrtcm_attach(int major, int minor, int flow,
		  struct drm_crtc *drm_crtc,
		  struct vcrtcm_gpu_callbacks *gpu_callbacks,
		  struct vcrtcm_dev_hal **vcrtcm_dev_hal)
{

	struct vcrtcm_dev_info *vcrtcm_dev_info;

	/* find the entry that should be remove */
	mutex_lock(&vcrtcm_dev_list_mutex);
	list_for_each_entry(vcrtcm_dev_info, &vcrtcm_dev_list, list) {
		if ((vcrtcm_dev_info->hw_major == major) &&
		    (vcrtcm_dev_info->hw_minor == minor) &&
		    (vcrtcm_dev_info->hw_flow == flow)) {
			mutex_lock(&vcrtcm_dev_info->vcrtcm_dev_hal.hal_mutex);
			if (vcrtcm_dev_info->status & VCRTCM_STATUS_HAL_IN_USE) {
				VCRTCM_ERROR("HAL %d.%d.%d already attached "
					     "to crtc_drm %p\n",
					     major, minor, flow, drm_crtc);
				mutex_unlock(&vcrtcm_dev_info->vcrtcm_dev_hal.
					     hal_mutex);
				mutex_unlock(&vcrtcm_dev_list_mutex);
				return -EBUSY;
			}
			/* if we got here, then we have found the HAL
			   and it's free for us to attach to */

			/* call the device specific back-end of attach */
			if (vcrtcm_dev_info->vcrtcm_dev_hal.funcs.attach) {
				int r;
				r = vcrtcm_dev_info->
				    vcrtcm_dev_hal.funcs.
				    attach(&vcrtcm_dev_info->vcrtcm_dev_hal,
					   vcrtcm_dev_info->hw_drv_info,
					   vcrtcm_dev_info->hw_flow);
				if (r) {
					VCRTCM_ERROR("back-end attach call failed\n");
					mutex_unlock(&vcrtcm_dev_info->
						     vcrtcm_dev_hal.hal_mutex);
					mutex_unlock(&vcrtcm_dev_list_mutex);
					return r;
				}
			}
			/* nothing can fail now, go ahead and
			   populate the structure */
			vcrtcm_dev_info->hw_major = major;
			vcrtcm_dev_info->hw_minor = minor;
			vcrtcm_dev_info->hw_flow = flow;
			vcrtcm_dev_info->drm_crtc = drm_crtc;
			vcrtcm_dev_info->status |= VCRTCM_STATUS_HAL_IN_USE;
			vcrtcm_dev_info->gpu_callbacks = *gpu_callbacks;

			/* point the GPU driver to HAL we have just attached */
			*vcrtcm_dev_hal = &vcrtcm_dev_info->vcrtcm_dev_hal;

			mutex_unlock(&vcrtcm_dev_info->vcrtcm_dev_hal.
				     hal_mutex);
			mutex_unlock(&vcrtcm_dev_list_mutex);
			return 0;

		}
	}
	mutex_unlock(&vcrtcm_dev_list_mutex);

	/* if we got here, then the HAL was not found */
	VCRTCM_ERROR("HAL %d.%d.%d not found\n", major, minor, flow);
	return -EINVAL;
}
EXPORT_SYMBOL(vcrtcm_attach);

/* called by the GPU driver to detach its CRTC from the
   compression/transmission/display (CTD) device
   GPU driver must supply the HAL it is detaching
   this function will also work if someoby else's
   HAL is provided for detaching but this is not expected
   to be used by the GPU driver (although VCRTCM may do
   this if the HAL implementation disappears from the system)

   after doing local cleanup, the detach function will
   make a callback into the driver to finish up the
   GPU driver-side cleanup (at least the GPU must
   set the pointer to HAL to NULL and it may need to do
   additional (driver-dependent) cleanup */
int vcrtcm_detach(struct vcrtcm_dev_hal *vcrtcm_dev_hal)
{

	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (!vcrtcm_dev_info->status) {
		VCRTCM_WARNING("HAL already detached\n");
		mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
		return -EINVAL;
	}
	vcrtcm_dev_info->status &= ~VCRTCM_STATUS_HAL_IN_USE;
	if (vcrtcm_dev_info->vcrtcm_dev_hal.funcs.detach)
		vcrtcm_dev_info->
		    vcrtcm_dev_hal.funcs.detach(&vcrtcm_dev_info->
						vcrtcm_dev_hal,
						vcrtcm_dev_info->hw_drv_info,
						vcrtcm_dev_info->hw_flow);
	if (vcrtcm_dev_info->gpu_callbacks.detach)
		vcrtcm_dev_info->gpu_callbacks.detach(vcrtcm_dev_info->drm_crtc);
	memset(&vcrtcm_dev_info->gpu_callbacks, 0,
	       sizeof(struct vcrtcm_gpu_callbacks));
	vcrtcm_dev_info->drm_crtc = NULL;
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return 0;

}
EXPORT_SYMBOL(vcrtcm_detach);

/* Emulates write/setup access to registers that
   define where the frame buffer associated with
   the CRTC is and what its geometry is

   GPU driver should pass the parameters (content to be written into
   emulated registers); registers must be implemented
   in the backend function; this function simply passes
   the register content and flow information to the back-end
   and lets the CTD driver deal with it */
int vcrtcm_set_fb(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	if (vcrtcm_dev_hal->funcs.set_fb) {
		VCRTCM_DEBUG("calling set_fb backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.set_fb(vcrtcm_fb,
						 vcrtcm_dev_info->hw_drv_info,
						 vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing set_fb backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_set_fb);

/* The opposite of vcrtcm_set_fb; GPU driver can read the content
   of the emulated registers (implemented in the GTD driver) into
   a structure pointed by vcrtcm_fb argument. */
int vcrtcm_get_fb(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	if (vcrtcm_dev_hal->funcs.get_fb) {
		VCRTCM_DEBUG("calling get_fb backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.get_fb(vcrtcm_fb,
						 vcrtcm_dev_info->hw_drv_info,
						 vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing get_fb backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_fb);

/* Emulates a page-flip call for a virtual CRTC
   similar to vcrtcm_set_fb, but only the ioaddr is modified
   and the backend is expected to make sure that frame tearing
   is avoided

   GPU driver should pass the IO address of where the new frame buffer
   is; backend must be able to deal with the address (FIXME: we may need
   a 64-bit address); the function will return 0 if the flip was
   done right away, VCRTCM_PFLIP_DEFERRED if the flip could not be
   done immediately (backend must chache it and execute when possible)
   or an error code when if the flip can't be done at all */
int vcrtcm_page_flip(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
		     u32 ioaddr)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	/* this method is intended to be called from ISR, so no */
	/* semaphore grabbing allowed */
	if (vcrtcm_dev_hal->funcs.page_flip)
		r = vcrtcm_dev_hal->funcs.page_flip(ioaddr,
						    vcrtcm_dev_info->hw_drv_info,
						    vcrtcm_dev_info->hw_flow);
	else
		r = 0;
	return r;
}
EXPORT_SYMBOL(vcrtcm_page_flip);

/* GPU driver calls this function whenever the the framebuffer
   associated with a given CRTC has changed to request transmission
   the transmission policy and scheduling is totally up to the
   backend implementation
   NOTE: this function may block */
int vcrtcm_xmit_fb(struct vcrtcm_dev_hal *vcrtcm_dev_hal)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	/* see the long comment in wait_fb implementation about
	   blocking and mutexes */
	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.xmit_fb) {
		VCRTCM_DEBUG("calling xmit_fb backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.xmit_fb(vcrtcm_dev_info->drm_crtc,
						  vcrtcm_dev_info->hw_drv_info,
						  vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing xmit_fb backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_xmit_fb);

/* GPU driver can use this function to synchronize with the
   CTD driver transmission (e.g. wait for the transmission to
   complete). whether the wait will actually occur or not
   totally depends on the policy implemented in the backend
   NOTE: this function may block */
int vcrtcm_wait_fb(struct vcrtcm_dev_hal *vcrtcm_dev_hal)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);
	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.wait_fb) {
		VCRTCM_DEBUG("calling wait_fb backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.wait_fb(vcrtcm_dev_info->drm_crtc,
						  vcrtcm_dev_info->hw_drv_info,
						  vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing wait_fb backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_wait_fb);

/* retrieves the status of frame buffer */
int vcrtcm_get_fb_status(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			 u32 *status)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	if (vcrtcm_dev_hal->funcs.get_fb_status) {
		VCRTCM_DEBUG("calling get_fb_status backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.get_fb_status(vcrtcm_dev_info->drm_crtc,
							vcrtcm_dev_info->hw_drv_info,
							vcrtcm_dev_info->hw_flow,
							status);
	} else {
		VCRTCM_WARNING("missing get_fb_status backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_fb_status);

/* sets the frame rate for frame buffer transmission */
int vcrtcm_set_fps(struct vcrtcm_dev_hal *vcrtcm_dev_hal, int fps)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.set_fps) {
		VCRTCM_DEBUG("calling set_fps backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.set_fps(fps,
						  vcrtcm_dev_info->hw_drv_info,
						  vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing set_fps backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_set_fps);

/* reads the frame rate for frame buffer transmission */
int vcrtcm_get_fps(struct vcrtcm_dev_hal *vcrtcm_dev_hal, int *fps)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.get_fps) {
		VCRTCM_DEBUG("calling get_fps backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.get_fps(fps,
						  vcrtcm_dev_info->hw_drv_info,
						  vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing get_fps backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_fps);

/* Emulates write/setup access to registers that
   control the hardware cursor

   GPU driver should pass the parameters (content to be written into
   emulated registers); registers must be implemented
   in the backend function; this function simply passes
   the register content and flow information to the back-end
   and lets the CTD driver deal with it */
int vcrtcm_set_cursor(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.set_cursor) {
		VCRTCM_DEBUG("calling set_cursor backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.set_cursor(vcrtcm_cursor,
						     vcrtcm_dev_info->
						     hw_drv_info,
						     vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing set_cursor backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_set_cursor);

/* The opposite of vcrtcm_set_cursor; GPU driver can read the content
   of the emulated registers (implemented in the GTD driver) into
   a structure pointed by vcrtcm_fb argument. */
int vcrtcm_get_cursor(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.set_cursor) {
		VCRTCM_DEBUG("calling get_fb backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.get_cursor(vcrtcm_cursor,
						     vcrtcm_dev_info->
						     hw_drv_info,
						     vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing get_fb backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_cursor);

/* dpms manipulation functions */
int vcrtcm_set_dpms(struct vcrtcm_dev_hal *vcrtcm_dev_hal, int state)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.set_dpms) {
		VCRTCM_DEBUG("calling set_dpms backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.set_dpms(state,
						   vcrtcm_dev_info->hw_drv_info,
						   vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing set_dpms backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_set_dpms);

/* dpms manipulation functions */
int vcrtcm_get_dpms(struct vcrtcm_dev_hal *vcrtcm_dev_hal, int *state)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
	    container_of(vcrtcm_dev_hal, struct vcrtcm_dev_info,
			 vcrtcm_dev_hal);

	mutex_lock(&vcrtcm_dev_hal->hal_mutex);
	if (vcrtcm_dev_hal->funcs.get_dpms) {
		VCRTCM_DEBUG("calling get_dpms backend, HAL %d.%d.%d\n",
			     vcrtcm_dev_info->hw_major,
			     vcrtcm_dev_info->hw_minor,
			     vcrtcm_dev_info->hw_flow);
		r = vcrtcm_dev_hal->funcs.get_dpms(state,
						   vcrtcm_dev_info->hw_drv_info,
						   vcrtcm_dev_info->hw_flow);
	} else {
		VCRTCM_WARNING("missing get_dpms backend, HAL %d.%d.%d\n",
			       vcrtcm_dev_info->hw_major,
			       vcrtcm_dev_info->hw_minor,
			       vcrtcm_dev_info->hw_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_dev_hal->hal_mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_dpms);

/* retrieve the last (fake) vblank time if it exists */
int vcrtcm_get_vblank_time(struct vcrtcm_dev_hal *vcrtcm_dev_hal,
			   struct timeval *vblank_time)
{
	int r;
	struct vcrtcm_dev_info *vcrtcm_dev_info =
		container_of(vcrtcm_dev_hal,
			     struct vcrtcm_dev_info, vcrtcm_dev_hal);

	if (vcrtcm_dev_info->vblank_time_valid) {
		*vblank_time = vcrtcm_dev_info->vblank_time;
		r = 0;
	} else
		r = -EAGAIN;
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_vblank_time);
