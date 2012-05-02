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

#include <linux/module.h>
#include "vcrtcm_utils.h"
#include "vcrtcm_private.h"

/* called by the GPU driver to attach its CRTC to the
   pixel consumer (PCON)
   GPU driver *must* supply the pointer to its own drm_crtc
   structure that describes the CRTC and the pointer to a
   callback function to be called on detach */
int vcrtcm_g_attach(int major, int minor, int flow,
		  struct drm_crtc *drm_crtc,
		  struct vcrtcm_gpu_funcs *gpu_funcs,
		  struct vcrtcm_pcon_info **vcrtcm_pcon_info)
{

	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private;

	/* find the entry that should be remove */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry(vcrtcm_pcon_info_private, &vcrtcm_pcon_list, list) {
		if ((vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major == major) &&
		    (vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor == minor) &&
		    (vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow == flow)) {
			unsigned long flags;
			mutex_lock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.mutex);
			spin_lock_irqsave(&vcrtcm_pcon_info_private->lock, flags);
			if (vcrtcm_pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
				spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock,
							flags);
				VCRTCM_ERROR("pcon %d.%d.%d already attached "
					     "to crtc_drm %p\n",
					     major, minor, flow, drm_crtc);
				mutex_unlock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.
					     mutex);
				mutex_unlock(&vcrtcm_pcon_list_mutex);
				return -EBUSY;
			}
			spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
			/* if we got here, then we have found the PCON
			   and it's free for us to attach to */

			/* call the device specific back-end of attach */
			if (vcrtcm_pcon_info_private->vcrtcm_pcon_info.funcs.attach) {
				int r;
				r = vcrtcm_pcon_info_private->
				    vcrtcm_pcon_info.funcs.
				    attach(&vcrtcm_pcon_info_private->vcrtcm_pcon_info);
				if (r) {
					VCRTCM_ERROR("back-end attach call failed\n");
					mutex_unlock(&vcrtcm_pcon_info_private->
						     vcrtcm_pcon_info.mutex);
					mutex_unlock(&vcrtcm_pcon_list_mutex);
					return r;
				}
			}
			/* nothing can fail now, go ahead and
			   populate the structure */
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major = major;
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor = minor;
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow = flow;
			vcrtcm_pcon_info_private->drm_crtc = drm_crtc;
			vcrtcm_pcon_info_private->gpu_funcs = *gpu_funcs;

			/* point the GPU driver to PCON we have just attached */
			*vcrtcm_pcon_info = &vcrtcm_pcon_info_private->vcrtcm_pcon_info;

			/* very last thing to do: change the status */
			spin_lock_irqsave(&vcrtcm_pcon_info_private->lock, flags);
			vcrtcm_pcon_info_private->status |= VCRTCM_STATUS_PCON_IN_USE;
			spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock,
					       flags);
			mutex_unlock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.
				     mutex);
			mutex_unlock(&vcrtcm_pcon_list_mutex);
			return 0;

		}
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	/* if we got here, then the PCON was not found */
	VCRTCM_ERROR("pcon %d.%d.%d not found\n", major, minor, flow);
	return -EINVAL;
}
EXPORT_SYMBOL(vcrtcm_g_attach);

/* called by the GPU driver to detach its CRTC from the
   pixel consumer (PCON)
   GPU driver must supply the PCON it is detaching
   this function will also work if someoby else's
   PCON is provided for detaching but this is not expected
   to be used by the GPU driver (although VCRTCM may do
   this if the PCON disappears from the system)

   after doing local cleanup, the detach function will
   make a callback into the driver to finish up the
   GPU driver-side cleanup (at least the GPU must
   set the pointer to PCON to NULL and it may need to do
   additional (driver-dependent) cleanup */
int vcrtcm_g_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{

	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);
	unsigned long flags;

	mutex_lock(&vcrtcm_pcon_info->mutex);
	spin_lock_irqsave(&vcrtcm_pcon_info_private->lock, flags);
	if (!vcrtcm_pcon_info_private->status) {
		spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
		VCRTCM_WARNING("pcon already detached\n");
		mutex_unlock(&vcrtcm_pcon_info->mutex);
		return -EINVAL;
	}
	vcrtcm_pcon_info_private->status &= ~VCRTCM_STATUS_PCON_IN_USE;
	spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
	if (vcrtcm_pcon_info_private->vcrtcm_pcon_info.funcs.detach)
		vcrtcm_pcon_info_private->
		    vcrtcm_pcon_info.funcs.detach(&vcrtcm_pcon_info_private->vcrtcm_pcon_info);
	if (vcrtcm_pcon_info_private->gpu_funcs.detach)
		vcrtcm_pcon_info_private->gpu_funcs.detach(vcrtcm_pcon_info_private->drm_crtc);
	memset(&vcrtcm_pcon_info_private->gpu_funcs, 0,
	       sizeof(struct vcrtcm_gpu_funcs));
	vcrtcm_pcon_info_private->drm_crtc = NULL;
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return 0;

}
EXPORT_SYMBOL(vcrtcm_g_detach);

/* Emulates write/setup access to registers that
   define where the frame buffer associated with
   the CRTC is and what its geometry is

   GPU driver should pass the parameters (content to be written into
   emulated registers); registers must be implemented
   in the backend function; this function simply passes
   the register content and flow information to the back-end
   and lets the PCON deal with it */
int vcrtcm_g_set_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.set_fb) {
		VCRTCM_DEBUG("calling set_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.set_fb(vcrtcm_pcon_info, vcrtcm_fb);
	} else {
		VCRTCM_WARNING("missing set_fb backend, pcon %d.%d.%d\n",
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_fb);

/* The opposite of vcrtcm_g_set_fb; GPU driver can read the content
   of the emulated registers (implemented in the GTD driver) into
   a structure pointed by vcrtcm_fb argument. */
int vcrtcm_get_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		  struct vcrtcm_fb *vcrtcm_fb)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.get_fb) {
		VCRTCM_DEBUG("calling get_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.get_fb(vcrtcm_pcon_info, vcrtcm_fb);
	} else {
		VCRTCM_WARNING("missing get_fb backend, pcon %d.%d.%d\n",
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_fb);

/* Emulates a page-flip call for a virtual CRTC
   similar to vcrtcm_g_set_fb, but only the ioaddr is modified
   and the backend is expected to make sure that frame tearing
   is avoided

   GPU driver should pass the IO address of where the new frame buffer
   is; backend must be able to deal with the address (FIXME: we may need
   a 64-bit address); the function will return 0 if the flip was
   done right away, VCRTCM_PFLIP_DEFERRED if the flip could not be
   done immediately (backend must chache it and execute when possible)
   or an error code when if the flip can't be done at all */
int vcrtcm_g_page_flip(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		     u32 ioaddr)
{
	int r;
	/* this method is intended to be called from ISR, so no */
	/* semaphore grabbing allowed */
	if (vcrtcm_pcon_info->funcs.page_flip)
		r = vcrtcm_pcon_info->funcs.page_flip(vcrtcm_pcon_info, ioaddr);
	else
		r = 0;
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_page_flip);

/* GPU driver calls this function whenever the the framebuffer
   associated with a given CRTC has changed to request transmission
   the transmission policy and scheduling is totally up to the
   backend implementation
   NOTE: this function may block */
int vcrtcm_g_dirty_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	/* see the long comment in wait_fb implementation about
	   blocking and mutexes */
	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.dirty_fb) {
		VCRTCM_DEBUG("calling dirty_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.dirty_fb(vcrtcm_pcon_info, vcrtcm_pcon_info_private->drm_crtc);
	} else {
		VCRTCM_DEBUG("missing dirty_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_dirty_fb);

/* GPU driver can use this function to synchronize with the
   PCON transmission (e.g. wait for the transmission to
   complete). whether the wait will actually occur or not
   totally depends on the policy implemented in the backend
   NOTE: this function may block */
int vcrtcm_g_wait_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);
	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.wait_fb) {
		VCRTCM_DEBUG("calling wait_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.wait_fb(vcrtcm_pcon_info, vcrtcm_pcon_info_private->drm_crtc);
	} else {
		VCRTCM_DEBUG("missing wait_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_wait_fb);

/* retrieves the status of frame buffer */
int vcrtcm_g_get_fb_status(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			 u32 *status)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	if (vcrtcm_pcon_info->funcs.get_fb_status) {
		VCRTCM_DEBUG("calling get_fb_status backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.get_fb_status(vcrtcm_pcon_info, vcrtcm_pcon_info_private->drm_crtc, status);
	} else {
		VCRTCM_WARNING("missing get_fb_status backend, pcon %d.%d.%d\n",
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_fb_status);

/* sets the frame rate for frame buffer transmission */
int vcrtcm_g_set_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int fps)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.set_fps) {
		VCRTCM_DEBUG("calling set_fps backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.set_fps(vcrtcm_pcon_info, fps);
	} else {
		VCRTCM_WARNING("missing set_fps backend, pcon %d.%d.%d\n",
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_fps);

/* reads the frame rate for frame buffer transmission */
int vcrtcm_g_get_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *fps)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.get_fps) {
		VCRTCM_DEBUG("calling get_fps backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.get_fps(vcrtcm_pcon_info, fps);
	} else {
		VCRTCM_WARNING("missing get_fps backend, pcon %d.%d.%d\n",
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_fps);

/* Emulates write/setup access to registers that
   control the hardware cursor

   GPU driver should pass the parameters (content to be written into
   emulated registers); registers must be implemented
   in the backend function; this function simply passes
   the register content and flow information to the back-end
   and lets the PCON deal with it */
int vcrtcm_g_set_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.set_cursor) {
		VCRTCM_DEBUG("calling set_cursor backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.set_cursor(vcrtcm_pcon_info, vcrtcm_cursor);
	} else {
		VCRTCM_DEBUG("missing set_cursor backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_cursor);

/* The opposite of vcrtcm_g_set_cursor; GPU driver can read the content
   of the emulated registers (implemented in the GTD driver) into
   a structure pointed by vcrtcm_fb argument. */
int vcrtcm_g_get_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		      struct vcrtcm_cursor *vcrtcm_cursor)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.set_cursor) {
		VCRTCM_DEBUG("calling get_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.get_cursor(vcrtcm_pcon_info, vcrtcm_cursor);
	} else {
		VCRTCM_DEBUG("missing get_fb backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_cursor);

/* dpms manipulation functions */
int vcrtcm_g_set_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int state)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.set_dpms) {
		VCRTCM_DEBUG("calling set_dpms backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.set_dpms(vcrtcm_pcon_info, state);
	} else {
		VCRTCM_DEBUG("missing set_dpms backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_dpms);

/* dpms manipulation functions */
int vcrtcm_get_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *state)
{
	int r;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.get_dpms) {
		VCRTCM_DEBUG("calling get_dpms backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.get_dpms(vcrtcm_pcon_info, state);
	} else {
		VCRTCM_DEBUG("missing get_dpms backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);
	return r;
}
EXPORT_SYMBOL(vcrtcm_get_dpms);

/* retrieve the last (fake) vblank time if it exists */
int vcrtcm_g_get_vblank_time(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			   struct timeval *vblank_time)
{
	int r;
	unsigned long flags;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info,
			     struct vcrtcm_pcon_info_private, vcrtcm_pcon_info);

	spin_lock_irqsave(&vcrtcm_pcon_info_private->lock, flags);
	if ((vcrtcm_pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) &&
	    (vcrtcm_pcon_info_private->vblank_time_valid)) {
		*vblank_time = vcrtcm_pcon_info_private->vblank_time;
		r = 0;
	} else
		r = -EAGAIN;
	spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_vblank_time);

/* set new (fake) vblank time; used when vblank emulation */
/* is generated internally by the GPU without involving the PCON */
/* (typically after a successful push) */
void vcrtcm_g_set_vblank_time(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	unsigned long flags;
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info,
			     struct vcrtcm_pcon_info_private, vcrtcm_pcon_info);

	spin_lock_irqsave(&vcrtcm_pcon_info_private->lock, flags);
	if (!vcrtcm_pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
		/* someone pulled the rug under our feet, bail out */
		spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
		return;
	}
	do_gettimeofday(&vcrtcm_pcon_info_private->vblank_time);
	vcrtcm_pcon_info_private->vblank_time_valid = 1;
	spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
	return;
}
EXPORT_SYMBOL(vcrtcm_g_set_vblank_time);

/* check if the attached PCON is in the connected state
 * some PCONs can be always connected (typically software
 * emulators), but some can feed real display devices
 * and may want to query the device they are driving for status
 */
int vcrtcm_g_pcon_connected(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *status)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info,
			     struct vcrtcm_pcon_info_private, vcrtcm_pcon_info);
	int r;

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.connected) {
		VCRTCM_DEBUG("calling connected backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.connected(vcrtcm_pcon_info, status);
	} else {
		VCRTCM_DEBUG("missing connected backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		*status = VCRTCM_PCON_CONNECTED;
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);

	return r;
}
EXPORT_SYMBOL(vcrtcm_g_pcon_connected);

static struct vcrtcm_mode common_modes[17] = {
	{640, 480, 60},
	{720, 480, 60},
	{800, 600, 60},
	{848, 480, 60},
	{1024, 768, 60},
	{1152, 768, 60},
	{1280, 720, 60},
	{1280, 800, 60},
	{1280, 854, 60},
	{1280, 960, 60},
	{1280, 1024, 60},
	{1440, 900, 60},
	{1400, 1050, 60},
	{1680, 1050, 60},
	{1600, 1200, 60},
	{1920, 1080, 60},
	{1920, 1200, 60}
};

/* get the list of modes that the attached PCON supports
 * if the PCON does not implement the backend function, assume
 * that it can support anything and use a list of common modes
 */
int vcrtcm_g_get_modes(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		     struct vcrtcm_mode **modes, int *count)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info,
			     struct vcrtcm_pcon_info_private, vcrtcm_pcon_info);
	int r;

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.get_modes) {
		VCRTCM_DEBUG("calling get_modes backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.get_modes(vcrtcm_pcon_info, modes, count);
	} else {
		VCRTCM_DEBUG("missing get_modes backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		*count = sizeof(common_modes) / sizeof(struct vcrtcm_mode);
		*modes = common_modes;
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);

	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_modes);


/* check if the mode is acceprable by the attached PCON
 * if backed function is not implemented, assume the PCON
 * accepts everything and the mode is OK
 */
int vcrtcm_g_check_mode(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		      struct vcrtcm_mode *mode, int *status)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info,
			     struct vcrtcm_pcon_info_private, vcrtcm_pcon_info);
	int r;

	mutex_lock(&vcrtcm_pcon_info->mutex);
	if (vcrtcm_pcon_info->funcs.check_mode) {
		VCRTCM_DEBUG("calling check_mode backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		r = vcrtcm_pcon_info->funcs.check_mode(vcrtcm_pcon_info, mode, status);
	} else {
		VCRTCM_DEBUG("missing check_mode backend, pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		*status = VCRTCM_MODE_OK;
		r = 0;
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);

	return r;
}
EXPORT_SYMBOL(vcrtcm_g_check_mode);

/* disable the transmission on specified PCON
 * called when CRTC associated with the PCON is
 * disabled from userland
 */
void vcrtcm_g_disable(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
			container_of(vcrtcm_pcon_info,
				struct vcrtcm_pcon_info_private, vcrtcm_pcon_info);

	mutex_lock(&vcrtcm_pcon_info->mutex);

	if (vcrtcm_pcon_info->funcs.disable) {
		VCRTCM_DEBUG("calling disable backend, pcon %d.%d.%d\n",
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);

		vcrtcm_pcon_info->funcs.disable(vcrtcm_pcon_info);
	} else {
		VCRTCM_DEBUG("missing disable backend, pcon %d.%d.%d\n",
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
	}
	mutex_unlock(&vcrtcm_pcon_info->mutex);

	return;
}
EXPORT_SYMBOL(vcrtcm_g_disable);
