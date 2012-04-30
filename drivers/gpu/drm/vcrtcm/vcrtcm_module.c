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

struct list_head vcrtcm_dev_list;
struct mutex vcrtcm_dev_list_mutex;

int vcrtcm_debug;

static int __init vcrtcm_init(void)
{
	VCRTCM_INFO
	    ("Virtual CRTC Manager, (C) Bell Labs, Alcatel-Lucent, Inc.\n");

	mutex_init(&vcrtcm_dev_list_mutex);
	INIT_LIST_HEAD(&vcrtcm_dev_list);

	VCRTCM_INFO("module loaded");
	return 0;
}

module_init(vcrtcm_init);

static void __exit vcrtcm_exit(void)
{
	struct vcrtcm_dev_info *vcrtcm_dev_info, *tmp;

	VCRTCM_INFO("unloading module");

	/* any remaining virtual CRTC must now be detached and destroyed
	   even if the PCONs have not explicitly given them up
	   (we have no other choice) */
	mutex_lock(&vcrtcm_dev_list_mutex);
	list_for_each_entry_safe(vcrtcm_dev_info, tmp, &vcrtcm_dev_list, list) {
		mutex_lock(&vcrtcm_dev_info->vcrtcm_dev_hal.hal_mutex);
		VCRTCM_INFO("removing HAL %d.%d.%d\n",
			    vcrtcm_dev_info->hw_major,
			    vcrtcm_dev_info->hw_minor,
			    vcrtcm_dev_info->hw_flow);
		if (vcrtcm_dev_info->status & VCRTCM_STATUS_HAL_IN_USE) {
			VCRTCM_INFO("HAL in use by CRTC %p, forcing detach\n",
				    vcrtcm_dev_info->drm_crtc);
			if (vcrtcm_dev_info->vcrtcm_dev_hal.funcs.detach)
				vcrtcm_dev_info->
				    vcrtcm_dev_hal.funcs.
				    detach(&vcrtcm_dev_info->vcrtcm_dev_hal,
					   vcrtcm_dev_info->hw_drv_info,
					   vcrtcm_dev_info->hw_flow);
			if (vcrtcm_dev_info->gpu_callbacks.detach)
				vcrtcm_dev_info->
					gpu_callbacks.detach(vcrtcm_dev_info->drm_crtc);
		}
		list_del(&vcrtcm_dev_info->list);
		mutex_unlock(&vcrtcm_dev_info->vcrtcm_dev_hal.hal_mutex);
		kfree(vcrtcm_dev_info);
	}
	mutex_unlock(&vcrtcm_dev_list_mutex);

	VCRTCM_INFO("all virtual crtcs gone");

}

module_exit(vcrtcm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Virtual CRTC Manager");
MODULE_AUTHOR("Ilija Hadzic (ihadzic@alcatel-lucent.com)");

module_param_named(debug, vcrtcm_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level, default=0");
