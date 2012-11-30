/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Author: Ilija Hadzic <ihadzic@research.bell-labs.com>
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


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include "vcrtcm_module.h"
#include "vcrtcm_ioctl_priv.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_alloc_priv.h"
#include "vcrtcm_pcon.h"

static const struct file_operations vcrtcm_fops;
static dev_t vcrtcm_dev;
static struct cdev *vcrtcm_cdev;
static struct device *vcrtcm_device;

int vcrtcm_debug;
int vcrtcm_log_all_vcrtcm_counts;
int vcrtcm_log_all_pim_counts;
int vcrtcm_log_all_pcon_counts;

static int __init vcrtcm_init(void)
{
	VCRTCM_INFO
	    ("Virtual CRTC Manager, (C) Bell Labs, Alcatel-Lucent, Inc.\n");
	init_pcon_table();
	vcrtcm_class = class_create(THIS_MODULE, "vcrtcm");
	vcrtcm_cdev = cdev_alloc();
	if (!vcrtcm_cdev)
		return -ENOMEM;
	alloc_chrdev_region(&vcrtcm_dev, 0, 1, "pimmgr");
	vcrtcm_cdev->ops = &vcrtcm_fops;
	vcrtcm_cdev->owner = THIS_MODULE;
	cdev_add(vcrtcm_cdev, vcrtcm_dev, 1);
	vcrtcm_device = device_create(vcrtcm_class, NULL, vcrtcm_dev,
				      NULL, "pimmgr");
	vcrtcm_sysfs_init(vcrtcm_device);
	VCRTCM_INFO("driver loaded, major %d, minor %d\n",
		    MAJOR(vcrtcm_dev), MINOR(vcrtcm_dev));
	return 0;
}

module_init(vcrtcm_init);

static void __exit vcrtcm_exit(void)
{
	int pconid;

	VCRTCM_INFO("unloading module");
	vcrtcm_free_pims();
	if (vcrtcm_class)
		device_destroy(vcrtcm_class, vcrtcm_dev);
	if (vcrtcm_cdev)
		cdev_del(vcrtcm_cdev);
	unregister_chrdev_region(vcrtcm_dev, 1);

	/*
	 * any remaining virtual CRTC must now be detached and destroyed
	 * even if the PCONs have not explicitly given them up
	 */
	for (pconid = 0; pconid < MAX_NUM_PCONIDS; ++pconid) {
		struct vcrtcm_pcon *pcon;

		pcon = vcrtcm_get_pcon(pconid);
		if (pcon) {
			vcrtcm_lock_pconid(pcon->pconid);
			VCRTCM_INFO("removing pcon %u\n", pcon->pconid);
			if (pcon->drm_crtc) {
				VCRTCM_INFO("pcon in use by CRTC %p, forcing detach\n",
						pcon->drm_crtc);
				if (pcon->pcon_funcs.detach &&
					pcon->pcon_callbacks_enabled &&
					pcon->pim->callbacks_enabled)
					pcon->pcon_funcs.detach(pcon->pconid,
						pcon->pcon_cookie);
				if (pcon->gpu_funcs.detach)
					pcon->gpu_funcs.detach(pcon->pconid,
						pcon->drm_crtc);
			}
			vcrtcm_dealloc_pcon(pcon);
			vcrtcm_unlock_pconid(pcon->pconid);
			vcrtcm_kfree(pcon);
		}
	}
	if (vcrtcm_class)
		class_destroy(vcrtcm_class);
}

static const struct file_operations vcrtcm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vcrtcm_ioctl,
};

module_exit(vcrtcm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Virtual CRTC Manager");
MODULE_AUTHOR("Ilija Hadzic (ihadzic@alcatel-lucent.com)");

module_param_named(debug, vcrtcm_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level, default=0");
module_param_named(log_all_vcrtcm_counts, vcrtcm_log_all_vcrtcm_counts,
		   int, 0644);
MODULE_PARM_DESC(log_all_vcrtcm_counts,
		 "Log all memory alloc counts for this module, default=0");
module_param_named(log_all_pim_counts, vcrtcm_log_all_pim_counts,
		   int, 0644);
MODULE_PARM_DESC(log_all_pim_counts,
		 "Log all memory alloc counts for all PIMs, default=0");
module_param_named(log_all_pcon_counts, vcrtcm_log_all_pcon_counts,
		   int, 0644);
MODULE_PARM_DESC(log_all_pcon_counts,
		 "Log all memory alloc counts for all PCONs, default=0");

