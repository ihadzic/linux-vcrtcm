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
#include <linux/moduleparam.h>
#include "vcrtcm_utils.h"
#include "vcrtcm_private.h"

struct list_head vcrtcm_pcon_list;
struct mutex vcrtcm_pcon_list_mutex;

int vcrtcm_debug;

static int __init vcrtcm_init(void)
{
	VCRTCM_INFO
	    ("Virtual CRTC Manager, (C) Bell Labs, Alcatel-Lucent, Inc.\n");

	mutex_init(&vcrtcm_pcon_list_mutex);
	INIT_LIST_HEAD(&vcrtcm_pcon_list);

	VCRTCM_INFO("module loaded");
	return 0;
}

module_init(vcrtcm_init);

static void __exit vcrtcm_exit(void)
{
	struct vcrtcm_pcon_info_private *vcrtcm_pcon_info_private, *tmp;

	VCRTCM_INFO("unloading module");

	/* any remaining virtual CRTC must now be detached and destroyed
	   even if the PCONs have not explicitly given them up
	   (we have no other choice) */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry_safe(vcrtcm_pcon_info_private, tmp, &vcrtcm_pcon_list, list) {
		mutex_lock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.mutex);
		VCRTCM_INFO("removing pcon %d.%d.%d\n",
			    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_major,
			    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_minor,
			    vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
		if (vcrtcm_pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
			VCRTCM_INFO("pcon in use by CRTC %p, forcing detach\n",
				    vcrtcm_pcon_info_private->drm_crtc);
			if (vcrtcm_pcon_info_private->vcrtcm_pcon_info.funcs.detach)
				vcrtcm_pcon_info_private->
				    vcrtcm_pcon_info.funcs.
				    detach(&vcrtcm_pcon_info_private->vcrtcm_pcon_info,
					   vcrtcm_pcon_info_private->pcon_cookie,
					   vcrtcm_pcon_info_private->vcrtcm_pcon_info.pcon_flow);
			if (vcrtcm_pcon_info_private->gpu_funcs.detach)
				vcrtcm_pcon_info_private->
					gpu_funcs.detach(vcrtcm_pcon_info_private->drm_crtc);
		}
		list_del(&vcrtcm_pcon_info_private->list);
		mutex_unlock(&vcrtcm_pcon_info_private->vcrtcm_pcon_info.mutex);
		kfree(vcrtcm_pcon_info_private);
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	VCRTCM_INFO("all virtual crtcs gone");

}

module_exit(vcrtcm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Virtual CRTC Manager");
MODULE_AUTHOR("Ilija Hadzic (ihadzic@alcatel-lucent.com)");

module_param_named(debug, vcrtcm_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level, default=0");
