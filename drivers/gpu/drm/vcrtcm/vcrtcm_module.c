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
#include "vcrtcm_private.h"
#include "vcrtcm_ioctl.h"
#include "vcrtcm_sysfs.h"

int pimmgr_debug = 1;
atomic_t pimmgr_kmalloc_track = ATOMIC_INIT(0);

static const struct file_operations pimmgr_fops;
struct list_head	pim_list;
struct mutex		pim_list_mutex;

static dev_t pimmgr_dev;
static struct cdev *pimmgr_cdev;
static struct device *pimmgr_device;

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
	return pimmgr_init();
}

module_init(vcrtcm_init);

static void __exit vcrtcm_exit(void)
{
	struct vcrtcm_pcon_info_private *pcon_info_private, *tmp;

	VCRTCM_INFO("unloading module");

	/*
	 * any remaining virtual CRTC must now be detached and destroyed
	 * even if the PCONs have not explicitly given them up
	 * (we have no other choice)
	 */
	pimmgr_exit();
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry_safe(pcon_info_private, tmp,
				&vcrtcm_pcon_list, list) {
		mutex_lock(&pcon_info_private->pcon_info.mutex);
		VCRTCM_INFO("removing pcon %u\n",
			    pcon_info_private->pcon_info.pconid);
		if (pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
			VCRTCM_INFO("pcon in use by CRTC %p, forcing detach\n",
				    pcon_info_private->drm_crtc);
			if (pcon_info_private->pcon_info.funcs.detach)
				pcon_info_private->
				    pcon_info.funcs.
				    detach(&pcon_info_private->pcon_info);
			if (pcon_info_private->gpu_funcs.detach)
				pcon_info_private->gpu_funcs.detach(
					pcon_info_private->drm_crtc);
		}
		list_del(&pcon_info_private->list);
		mutex_unlock(&pcon_info_private->pcon_info.mutex);
		kfree(pcon_info_private);
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	if (vcrtcm_class)
		class_destroy(vcrtcm_class);

	VCRTCM_INFO("all virtual crtcs gone");

}

int pimmgr_init(void)
{
	struct class *vcrtcm_class;

	INIT_LIST_HEAD(&pim_list);
	mutex_init(&pim_list_mutex);

	pimmgr_cdev = cdev_alloc();

	if (!pimmgr_cdev)
		goto error;

	vcrtcm_class = vcrtcm_sysfs_get_class();
	if (!vcrtcm_class)
		goto error;

	alloc_chrdev_region(&pimmgr_dev, 0, 1, "pimmgr");

	pimmgr_cdev->ops = &pimmgr_fops;
	pimmgr_cdev->owner = THIS_MODULE;
	cdev_add(pimmgr_cdev, pimmgr_dev, 1);

	pimmgr_device = device_create(vcrtcm_class, NULL, pimmgr_dev,
							NULL, "pimmgr");
	pimmgr_sysfs_init(pimmgr_device);

	if (pimmgr_structures_init() < 0)
		goto error;

	VCRTCM_INFO("Bell Labs PIM Manager (pimmgr)\n");
	VCRTCM_INFO("Copyright (C) 2012 Alcatel-Lucent, Inc.\n");
	VCRTCM_INFO("pimmgr driver loaded, major %d, minor %d\n",
					MAJOR(pimmgr_dev), MINOR(pimmgr_dev));

	return 0;
error:
	if (pimmgr_cdev)
		cdev_del(pimmgr_cdev);

	return -ENOMEM;
}

void pimmgr_exit(void)
{
	struct pim_info *info, *tmp;
	struct class *vcrtcm_class;

	list_for_each_entry_safe(info, tmp, &pim_list, pim_list) {
		list_del(&info->pim_list);
		vcrtcm_kfree(info, &pimmgr_kmalloc_track);
	}

	vcrtcm_class = vcrtcm_sysfs_get_class();
	if (vcrtcm_class)
		device_destroy(vcrtcm_class, pimmgr_dev);

	if (pimmgr_cdev)
		cdev_del(pimmgr_cdev);

	unregister_chrdev_region(pimmgr_dev, 1);

	pimmgr_structures_destroy();

	VCRTCM_INFO("kmalloc count: %i\n", atomic_read(&pimmgr_kmalloc_track));
	VCRTCM_INFO("pimmgr unloaded\n");
}

static const struct file_operations pimmgr_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = pimmgr_ioctl,
};

module_exit(vcrtcm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Virtual CRTC Manager");
MODULE_AUTHOR("Ilija Hadzic (ihadzic@alcatel-lucent.com)");

module_param_named(debug, vcrtcm_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level, default=0");
