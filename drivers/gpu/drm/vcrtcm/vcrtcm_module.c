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
#include "vcrtcm_private.h"
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_pcon.h>
#include "vcrtcm_ioctl.h"
#include "vcrtcm_sysfs.h"

atomic_t vcrtcm_kmalloc_track = ATOMIC_INIT(0);

static const struct file_operations vcrtcm_fops;
struct list_head	pim_list;
struct mutex		pim_list_mutex;

static dev_t vcrtcm_dev;
static struct cdev *vcrtcm_cdev;
static struct device *vcrtcm_device;

struct list_head vcrtcm_pcon_list;
struct mutex vcrtcm_pcon_list_mutex;

int vcrtcm_debug;

static int __init vcrtcm_init(void)
{
	VCRTCM_INFO
	    ("Virtual CRTC Manager, (C) Bell Labs, Alcatel-Lucent, Inc.\n");
	mutex_init(&vcrtcm_pcon_list_mutex);
	INIT_LIST_HEAD(&vcrtcm_pcon_list);
	vcrtcm_class = class_create(THIS_MODULE, "vcrtcm");
	INIT_LIST_HEAD(&pim_list);
	mutex_init(&pim_list_mutex);
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
	if (vcrtcm_structures_init() < 0) {
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
	struct vcrtcm_pcon_info *pcon_info, *priv_tmp;
	struct vcrtcm_pim_info *info, *info_tmp;

	VCRTCM_INFO("unloading module");
	list_for_each_entry_safe(info, info_tmp, &pim_list, pim_list) {
		list_del(&info->pim_list);
		vcrtcm_kfree(info, &vcrtcm_kmalloc_track);
	}
	if (vcrtcm_class)
		device_destroy(vcrtcm_class, vcrtcm_dev);
	if (vcrtcm_cdev)
		cdev_del(vcrtcm_cdev);
	unregister_chrdev_region(vcrtcm_dev, 1);
	vcrtcm_structures_destroy();

	/*
	 * any remaining virtual CRTC must now be detached and destroyed
	 * even if the PCONs have not explicitly given them up
	 * (we have no other choice)
	 */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry_safe(pcon_info, priv_tmp,
				&vcrtcm_pcon_list, list) {
		mutex_lock(&pcon_info->mutex);
		VCRTCM_INFO("removing pcon %u\n",
			    pcon_info->pconid);
		if (pcon_info->status & VCRTCM_STATUS_PCON_IN_USE) {
			VCRTCM_INFO("pcon in use by CRTC %p, forcing detach\n",
				    pcon_info->drm_crtc);
			if (pcon_info->funcs.detach)
				pcon_info->funcs.detach(pcon_info);
			if (pcon_info->gpu_funcs.detach)
				pcon_info->gpu_funcs.detach(pcon_info->drm_crtc);
		}
		list_del(&pcon_info->list);
		mutex_unlock(&pcon_info->mutex);
		vcrtcm_dealloc_pcon_info(pcon_info->pconid);
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

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
