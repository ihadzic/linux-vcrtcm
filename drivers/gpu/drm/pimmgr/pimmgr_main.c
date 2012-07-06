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

#include "pimmgr.h"
#include "pimmgr_utils.h"

int pimmgr_debug = 1;

static const struct file_operations pimmgr_fops = {
	.owner = THIS_MODULE,
};

struct list_head	pim_list;
struct mutex		pim_list_mutex;

dev_t dev;
struct cdev *cdev;
struct class *class;

static int pimmgr_init(void)
{
	INIT_LIST_HEAD(&pim_list);
	mutex_init(&pim_list_mutex);

	cdev = cdev_alloc();

	if (!cdev)
		goto error;

	class = class_create(THIS_MODULE, "pimmgr");

	if (!class)
		goto error;

	alloc_chrdev_region(&dev, 0, 1, "pimmgr");

	cdev->ops = &pimmgr_fops;
	cdev->owner = THIS_MODULE;
	cdev_add(cdev, dev, 1);

	device_create(class, NULL, dev, NULL, "pimmgr");


	PR_INFO("Bell Labs PIM Manager (pimmgr)\n");
	PR_INFO("Copyright (C) 2012 Alcatel-Lucent, Inc.\n");
	PR_INFO("pimmgr driver loaded, major %d, minor %d\n",
					MAJOR(dev), MINOR(dev));

	return 0;
error:

	if (cdev)
		cdev_del(cdev);

	if (class)
		class_destroy(class);

	return -ENOMEM;
}

static void pimmgr_exit(void)
{
	struct pim_info *info, *tmp;

	list_for_each_entry_safe(info, tmp, &pim_list, list) {
		/* Free list entry. */
		list_del(&info->list);
	}

	if (cdev)
		cdev_del(cdev);

	if (class) {
		device_destroy(class, dev);
		class_destroy(class);
	}

	unregister_chrdev_region(dev, 1);

	pimmgr_print_alloc_stats();
	PR_INFO("pimmgr unloaded\n");
}

module_init(pimmgr_init);
module_exit(pimmgr_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PIM Manager");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
