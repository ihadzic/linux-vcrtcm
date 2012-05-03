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

/* called by the pixel consumer (PCON) to
   register itself implementation with vcrtcm; the PCON can also provide
   pointers to back-end functions (functions that get called after
   generic PCON function is executed) */
int vcrtcm_p_add(struct vcrtcm_pcon_funcs *vcrtcm_pcon_funcs,
		  struct vcrtcm_pcon_props *vcrtcm_pcon_props,
		  int major, int minor, int flow, void *pcon_cookie)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private;

	/* first check whether we are already registered */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry(vcrtcm_pcon_info_private, &vcrtcm_pcon_list, list) {
		if ((vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major == major) &&
		    (vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor == minor) &&
		    (vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow == flow)) {
			/* if the PCON already exists, we just overwrite
			   the provided functions (assuming that the PCON
			   that called us knows what it's doing */
			mutex_lock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.mutex);
			VCRTCM_WARNING("found an existing pcon %d.%d.%d, "
				       "refreshing its implementation\n",
				       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
				       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
				       vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
			memcpy(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.funcs,
			       vcrtcm_pcon_funcs, sizeof(struct vcrtcm_pcon_funcs));
			memcpy(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.props,
			       vcrtcm_pcon_props, sizeof(struct vcrtcm_pcon_props));
			mutex_unlock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.
				     mutex);
			mutex_unlock(&vcrtcm_pcon_list_mutex);
			return 0;
		}
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	/* if we got here, then we are dealing with a new implementation
	   and we have to allocate and populate the PCON structure */
	vcrtcm_pcon_info_private = kmalloc(sizeof(struct vcrtcm_pcon_info_private), GFP_KERNEL);
	if (vcrtcm_pcon_info_private == NULL)
		return -ENOMEM;

	/* populate the PCON structures (no need to hold the mutex
	   because no one else sees this structure yet) */
	spin_lock_init(&vcrtcm_pcon_info_private->lock);
	mutex_init(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.mutex);
	memcpy(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.funcs, vcrtcm_pcon_funcs,
	       sizeof(struct vcrtcm_pcon_funcs));
	memcpy(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.props, vcrtcm_pcon_props,
	       sizeof(struct vcrtcm_pcon_props));

	/* populate the info structure and link it to the PCON structure */
	vcrtcm_pcon_info_private->status = 0;
	vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major = major;
	vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor = minor;
	vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow = flow;
	vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_cookie = pcon_cookie;
	vcrtcm_pcon_info_private->vblank_time_valid = 0;
	vcrtcm_pcon_info_private->vblank_time.tv_sec = 0;
	vcrtcm_pcon_info_private->vblank_time.tv_usec = 0;
	vcrtcm_pcon_info_private->drm_crtc = NULL;
	memset(&vcrtcm_pcon_info_private->gpu_funcs, 0,
	       sizeof(struct vcrtcm_gpu_funcs));

	VCRTCM_INFO("adding new pcon %d.%d.%d\n",
		    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
		    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor, vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);

	/* make the new PCON available to the rest of the system */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_add(&vcrtcm_pcon_info_private->list, &vcrtcm_pcon_list);
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_add);

/* called by the pixel consumer (PCON)
   (typically on exit) to unregister its implementation with PCON */
void vcrtcm_p_del(int major, int minor, int flow)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private;

	/* find the entry that should be removed */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry(vcrtcm_pcon_info_private, &vcrtcm_pcon_list, list) {
		if ((vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major == major) &&
		    (vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor == minor) &&
		    (vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow == flow)) {
			unsigned long flags;
			mutex_lock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.mutex);
			VCRTCM_INFO("found an existing pcon %d.%d.%d "
				    "removing\n",
				    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
				    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
				    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
			spin_lock_irqsave(&vcrtcm_pcon_info_private->lock, flags);
			if (vcrtcm_pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
				vcrtcm_pcon_info_private->status &= ~VCRTCM_STATUS_PCON_IN_USE;
				spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock,
						       flags);
				VCRTCM_INFO("pcon in use by CRTC %p, "
					    "forcing detach\n",
					    vcrtcm_pcon_info_private->drm_crtc);
				if (vcrtcm_pcon_info_private->vcrtcm_pcon_info.funcs.detach)
					vcrtcm_pcon_info_private->vcrtcm_pcon_info.funcs.
					    detach(&vcrtcm_pcon_info_private->vcrtcm_pcon_info);
				if (vcrtcm_pcon_info_private->gpu_funcs.detach)
					vcrtcm_pcon_info_private->
						gpu_funcs.detach(vcrtcm_pcon_info_private->drm_crtc);
			} else
				spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock,
						       flags);
			list_del(&vcrtcm_pcon_info_private->list);
			mutex_unlock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.
				     mutex);
			kfree(vcrtcm_pcon_info_private);
			mutex_unlock(&vcrtcm_pcon_list_mutex);
			return;
		}
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	/* if we got here, then the caller is attempting to remove something
	   that does not exist */
	VCRTCM_WARNING("requested pcon %d.%d.%d not found\n",
		       major, minor, flow);
}
EXPORT_SYMBOL(vcrtcm_p_del);

/* The PCON can use this function wait for the GPU to finish rendering
   to the frame.  PCONs typically call this to prevent frame tearing. */
void vcrtcm_p_gpu_sync(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);
	unsigned long jiffies_snapshot, jiffies_snapshot_2;

	VCRTCM_INFO("waiting for GPU pcon %d.%d.%d\n",
		    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
		    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor, vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
	jiffies_snapshot = jiffies;
	if (vcrtcm_pcon_info_private->gpu_funcs.sync)
		vcrtcm_pcon_info_private->gpu_funcs.sync(vcrtcm_pcon_info_private->drm_crtc);
	jiffies_snapshot_2 = jiffies;

	VCRTCM_INFO("time spent waiting for GPU %d ms\n",
		    ((int)jiffies_snapshot_2 -
		     (int)(jiffies_snapshot)) * 1000 / HZ);

}
EXPORT_SYMBOL(vcrtcm_p_gpu_sync);

/* called by the PCON to emulate vblank
   this is the link between the vblank event that happened in
   the PCON but is emulated by the virtual
   CRTC implementation in the GPU-hardware-specific driver */
void vcrtcm_p_emulate_vblank(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
	    container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			 vcrtcm_pcon_info);
	unsigned long flags;

	spin_lock_irqsave(&vcrtcm_pcon_info_private->lock, flags);
	if (!vcrtcm_pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
		/* someone pulled the rug under our feet, bail out */
		spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
		return;
	}
	do_gettimeofday(&vcrtcm_pcon_info_private->vblank_time);
	vcrtcm_pcon_info_private->vblank_time_valid = 1;
	spin_unlock_irqrestore(&vcrtcm_pcon_info_private->lock, flags);
	if (vcrtcm_pcon_info_private->gpu_funcs.vblank) {
		VCRTCM_DEBUG("emulating vblank event for pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		vcrtcm_pcon_info_private->gpu_funcs.vblank(vcrtcm_pcon_info_private->drm_crtc);
	}
}
EXPORT_SYMBOL(vcrtcm_p_emulate_vblank);

/* called by the PCON to allocate a push buffer */
int vcrtcm_p_push_buffer_alloc(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			     struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			     vcrtcm_pcon_info);
	struct drm_crtc *crtc = vcrtcm_pcon_info_private->drm_crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *obj;

	if (vcrtcm_pcon_info_private->gpu_funcs.pb_alloc) {
		int r;
		r = vcrtcm_pcon_info_private->gpu_funcs.pb_alloc(dev, pbd);
		obj = pbd->gpu_private;
		VCRTCM_DEBUG("pcon %d.%d.%d "
			     "allocated push buffer name=%d, size=%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow,
			     obj->name, obj->size);
		return r;
	} else
		return -ENOMEM;
}
EXPORT_SYMBOL(vcrtcm_p_push_buffer_alloc);

/* called by the PCON to free up a push buffer */
void vcrtcm_p_push_buffer_free(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			     struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			     vcrtcm_pcon_info);
	struct drm_gem_object *obj = pbd->gpu_private;

	if (vcrtcm_pcon_info_private->gpu_funcs.pb_free) {
		VCRTCM_DEBUG("pcon %d.%d.%d "
			     "freeing push buffer name=%d, size=%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow,
			     obj->name, obj->size);
		vcrtcm_pcon_info_private->gpu_funcs.pb_free(obj);
		memset(pbd, 0,
		       sizeof(struct vcrtcm_push_buffer_descriptor));
	}
}
EXPORT_SYMBOL(vcrtcm_p_push_buffer_free);

/* called by the PCON to request GPU push of the */
/* frame buffer pixels; pushes the frame buffer associated with  */
/* the ctrc that is attached to the specified hal into the push buffer */
/* defined by pbd */
int vcrtcm_p_push(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
		struct vcrtcm_push_buffer_descriptor *fpbd,
		struct vcrtcm_push_buffer_descriptor *cpbd)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			     vcrtcm_pcon_info);
	struct drm_crtc *crtc = vcrtcm_pcon_info_private->drm_crtc;
	struct drm_gem_object *push_buffer_fb = fpbd->gpu_private;
	struct drm_gem_object *push_buffer_cursor = cpbd->gpu_private;

	if (vcrtcm_pcon_info_private->gpu_funcs.push) {
		VCRTCM_DEBUG("push for pcon %d.%d.%d\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		return vcrtcm_pcon_info_private->gpu_funcs.push(crtc,
			push_buffer_fb, push_buffer_cursor);
	} else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(vcrtcm_p_push);

/* called by the PCON to signal hotplug event on a CRTC
 * attached to the specified PCON
 */
void vcrtcm_p_hotplug(struct vcrtcm_pcon_info *vcrtcm_pcon_info)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private =
		container_of(vcrtcm_pcon_info, struct vcrtcm_pcon_info_private,
			     vcrtcm_pcon_info);
	struct drm_crtc *crtc = vcrtcm_pcon_info_private->drm_crtc;

	if (vcrtcm_pcon_info_private->gpu_funcs.hotplug) {
		vcrtcm_pcon_info_private->gpu_funcs.hotplug(crtc);
		VCRTCM_DEBUG("pcon %d.%d.%d hotplug\n",
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			     vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);

	}
}
EXPORT_SYMBOL(vcrtcm_p_hotplug);
