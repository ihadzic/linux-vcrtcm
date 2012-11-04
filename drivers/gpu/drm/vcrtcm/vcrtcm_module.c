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
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_gpu.h>
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

atomic_t vcrtcm_kmalloc_track = ATOMIC_INIT(0);

static const struct file_operations vcrtcm_fops;
struct list_head	vcrtcm_pim_list;
struct mutex		vcrtcm_pim_list_mutex;

static dev_t vcrtcm_dev;
static struct cdev *vcrtcm_cdev;
static struct device *vcrtcm_device;

int vcrtcm_debug;

static int __init vcrtcm_init(void)
{
	VCRTCM_INFO
	    ("Virtual CRTC Manager, (C) Bell Labs, Alcatel-Lucent, Inc.\n");
	vcrtcm_class = class_create(THIS_MODULE, "vcrtcm");
	INIT_LIST_HEAD(&vcrtcm_pim_list);
	mutex_init(&vcrtcm_pim_list_mutex);
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
	if (vcrtcm_pcon_table_init() < 0) {
		cdev_del(vcrtcm_cdev);
		return -ENOMEM;
	}
	VCRTCM_INFO("driver loaded, major %d, minor %d\n",
					MAJOR(vcrtcm_dev), MINOR(vcrtcm_dev));
	return 0;
}

module_init(vcrtcm_init);

static void __exit vcrtcm_exit(void)
{
	struct vcrtcm_pim *pim, *pim_tmp;
	int pconid;

	VCRTCM_INFO("unloading module");
	list_for_each_entry_safe(pim, pim_tmp, &vcrtcm_pim_list, pim_list) {
		list_del(&pim->pim_list);
		vcrtcm_kfree(pim, &vcrtcm_kmalloc_track);
	}
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
			mutex_lock(&pcon->mutex);
			VCRTCM_INFO("removing pcon %u\n",
					pcon->pconid);
			if (pcon->status & VCRTCM_STATUS_PCON_IN_USE) {
				VCRTCM_INFO("pcon in use by CRTC %p, forcing detach\n",
						pcon->drm_crtc);
				if (pcon->funcs.detach)
					pcon->funcs.detach(pcon->pconid, pcon->pcon_cookie);
				if (pcon->gpu_funcs.detach)
					pcon->gpu_funcs.detach(pcon->drm_crtc);
			}
			mutex_unlock(&pcon->mutex);
			vcrtcm_dealloc_pcon(pcon->pconid);
		}
	}
	if (vcrtcm_class)
		class_destroy(vcrtcm_class);

	VCRTCM_INFO("all virtual crtcs gone");
	VCRTCM_INFO("kmalloc count: %i\n", atomic_read(&vcrtcm_kmalloc_track));
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
