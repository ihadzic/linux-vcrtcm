/*
 * Copyright (C) 2012 Alcatel-Lucent, Inc.
 * Author: Bill Katsak <william.katsak@alcatel-lucent.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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
#include <vcrtcm/pimmgr.h>
#include "pimmgr_private.h"
#include "pimmgr_ioctl.h"
#include "pimmgr_sysfs.h"

int pimmgr_debug = 1;
atomic_t pimmgr_kmalloc_track = ATOMIC_INIT(0);

static const struct file_operations pimmgr_fops;
struct list_head	pim_list;
struct mutex		pim_list_mutex;
struct list_head	pcon_list;

static dev_t pimmgr_dev;
static struct cdev *pimmgr_cdev;
static struct device *pimmgr_device;

static int pimmgr_init(void)
{
	struct class *vcrtcm_class;

	INIT_LIST_HEAD(&pim_list);
	INIT_LIST_HEAD(&pcon_list);
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

static void pimmgr_exit(void)
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

	VCRTCM_INFO("kmalloc count: %i\n", atomic_read(&pimmgr_kmalloc_track));
	VCRTCM_INFO("pimmgr unloaded\n");
}

long pimmgr_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return pimmgr_ioctl_core(filp, cmd, arg);
}


static const struct file_operations pimmgr_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = pimmgr_ioctl,
};

module_init(pimmgr_init);
module_exit(pimmgr_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PIM Manager");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
