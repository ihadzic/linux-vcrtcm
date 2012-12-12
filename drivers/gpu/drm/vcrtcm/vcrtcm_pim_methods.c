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
#include <linux/string.h>
#include <linux/slab.h>
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include "vcrtcm_pim_methods.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_pcon_methods.h"
#include "vcrtcm_pcon.h"

int vcrtcm_pim_register(char *pim_name,
	struct vcrtcm_pim_funcs *funcs, int *pimid)
{
	struct vcrtcm_pim *pim;

	VCRTCM_INFO("registering pim %s\n", pim_name);
	pim = vcrtcm_create_pim(pim_name, funcs);
	if (IS_ERR(pim))
		return PTR_ERR(pim);
	*pimid = pim->id;
	vcrtcm_sysfs_add_pim(pim);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_register);

int vcrtcm_pim_unregister(int pimid)
{
	struct vcrtcm_pim *pim;
	struct vcrtcm_pcon *pcon, *tmp;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	VCRTCM_INFO("unregistering %s\n", pim->name);
	list_for_each_entry_safe(pcon, tmp,
			&pim->pcons_in_pim_list, pcons_in_pim_list)
		vcrtcm_p_destroy_l(pcon->pconid);
	vcrtcm_sysfs_del_pim(pim);
	vcrtcm_destroy_pim(pim);
	VCRTCM_INFO("finished unregistering pim\n");
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_unregister);

int vcrtcm_pim_enable_callbacks(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	VCRTCM_INFO("enabling callbacks for pim %s\n", pim->name);
	pim->callbacks_enabled = 1;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_enable_callbacks);

int vcrtcm_pim_disable_callbacks(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	VCRTCM_INFO("disabling callbacks for pim %s\n", pim->name);
	pim->callbacks_enabled = 0;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_disable_callbacks);

int vcrtcm_pim_log_alloc_cnts(int pimid, int on)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	pim->log_alloc_cnts = on;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_log_alloc_cnts);


/*
 * Helper function for vcrtcm_pim_add_major. It allocates and registers
 * a major device number. If desired major device numer is specified
 * it tries to use that one. Otherwise, it allocates dynamically
 */
static int vcrtcm_alloc_major(int desired_major, int num_minors,
			      const char *name, int *pim_major)
{
	dev_t dev;
	int r;

	if (desired_major >= 0) {
		dev = MKDEV(desired_major, 0);
		r = register_chrdev_region(dev, num_minors, name);
		if (r) {
			VCRTCM_ERROR("can't allocate static major %d for %s\n",
				     desired_major, name);
			return r;
		} else
			VCRTCM_INFO("allocated static major %d for %s\n",
				    MAJOR(dev), name);
	} else {
		r = alloc_chrdev_region(&dev, 0, num_minors, name);
		if (r) {
			VCRTCM_ERROR("can't allocate dynamic major for %s\n",
				     name);
			return r;
		} else
			VCRTCM_INFO("allocated dynamic major %d for %s\n",
				    MAJOR(dev), name);
	}
	*pim_major = MAJOR(dev);
	return r;
}

/*
 * Helper function for vcrtcm_pim_del_major. Returns the major
 * device number to the system by freeing the chrdev region
 */
static void vcrtcm_free_major(int major, int num_minors)
{
	if (major >= 0) {
		dev_t dev = MKDEV(major, 0);
		unregister_chrdev_region(dev, num_minors);
	}
}

/*
 * Helper function for vcrtcm_pim_add/del_minor. Looks up vcrtcm_minor
 * structure within a pim that has a specified minor number
 */
static struct vcrtcm_minor *vcrtcm_get_minor(struct vcrtcm_pim *pim, int minor)
{
	struct vcrtcm_minor *vcrtcm_minor;

	list_for_each_entry(vcrtcm_minor, &pim->minors_in_pim_list,
			    minors_in_pim_list) {
		if (vcrtcm_minor->minor == minor)
			return vcrtcm_minor;
	}
	return NULL;
}

/*
 * Records major device number for a PIM. Only PIMs that interact
 * outside the VCRTCM context have major device numbers. Calling
 * this function is a pre-requisite for using vcrtcm_pim_add_minor
 * and vcrtcm_pim_del_minor
 */
int vcrtcm_pim_add_major(int pimid, int desired_major, int max_minors)
{
	struct vcrtcm_pim *pim;
	int major, r;

	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (pim->has_major) {
		VCRTCM_ERROR("pim %d already has major %d\n",
			     pimid, pim->major);
		return -EBUSY;
	}
	r = vcrtcm_alloc_major(desired_major, max_minors, pim->name, &major);
	if (r)
		return r;
	INIT_LIST_HEAD(&pim->minors_in_pim_list);
	pim->major = major;
	pim->has_major = 1;
	pim->max_minors = max_minors;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_add_major);

/* Opposite of vcrtcm_pim_add_major */
int vcrtcm_pim_del_major(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (pim->has_major == 0) {
		VCRTCM_ERROR("pin %d has no major\n", pimid);
		return -ENOENT;
	}
	if (!list_empty(&pim->minors_in_pim_list)) {
		VCRTCM_ERROR("list of minors for pim %d not empty\n", pimid);
		return -EBUSY;
	}
	vcrtcm_free_major(pim->major, pim->max_minors);
	pim->major = 0;
	pim->has_major = 0;
	pim->max_minors = 0;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_del_major);

/* Returns major for a given PCON or -ENOENT on error */
int vcrtcm_pim_get_major(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim || pim->has_major == 0)
		return -ENOENT;
	return pim->major;
}
EXPORT_SYMBOL(vcrtcm_pim_get_major);

/*
 * Creates a device structure for a specified pcon and minor,
 * and adds it to vcrtcm class. This function is used by PIMs
 * that interact with user space outside VCRTCM context, and
 * thus need their own major/minor numbers and device files.
 * Call into this function will generate udev event that will
 * in turn create a device file. PIM is responsible for
 * maintaining minor device numbers and relationship between
 * PCON IDs and minor device numbers can be arbitrary
 * (it's up to the PIM to establish a relationship that is
 * meaningful for it). Prior to calling this function, PIM
 * must register the major device number using vcrtcm_pim_add_major
 * function.
 */
int vcrtcm_pim_add_minor(int pimid, int minor)
{
	struct vcrtcm_pim *pim;
	struct device *device;
	dev_t dev;
	struct vcrtcm_minor *vcrtcm_minor;

	if (minor < 0) {
		VCRTCM_ERROR("invalid minor");
		return -EINVAL;
	}
	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (!pim->has_major) {
		VCRTCM_ERROR("pim %d has no major\n", pimid);
		return -ENOENT;
	}
	vcrtcm_minor = vcrtcm_get_minor(pim, minor);
	if (vcrtcm_minor) {
		VCRTCM_ERROR("pim %d, minor %d, already added\n",
			     pimid, minor);
		return -EBUSY;
	}
	vcrtcm_minor = vcrtcm_kzalloc(sizeof(struct vcrtcm_minor), GFP_KERNEL,
				      VCRTCM_OWNER_PIM | pim->id);
	if (!vcrtcm_minor)
		return -ENOMEM;
	dev = MKDEV(pim->major, minor);
	device = device_create(vcrtcm_class, NULL, dev, NULL,
			       "%s%d", pim->name, minor);
	if (!device) {
		vcrtcm_kfree(vcrtcm_minor);
		return -EFAULT;
	}
	vcrtcm_minor->minor = minor;
	vcrtcm_minor->device = device;
	list_add_tail(&vcrtcm_minor->minors_in_pim_list,
		      &pim->minors_in_pim_list);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_add_minor);

/* Opposite of vcrtcm_pim_add_minor.*/
int vcrtcm_pim_del_minor(int pimid, int minor)
{

	struct vcrtcm_pim *pim;
	struct vcrtcm_minor *vcrtcm_minor;
	dev_t dev;

	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (!pim->has_major) {
		VCRTCM_ERROR("pim %d has no major\n", pimid);
		return -ENOENT;
	}
	vcrtcm_minor = vcrtcm_get_minor(pim, minor);
	if (!vcrtcm_minor) {
		VCRTCM_ERROR("pim %d, minor %d, not found\n",
			     pimid, minor);
		return -EBUSY;
	}
	list_del(&vcrtcm_minor->minors_in_pim_list);
	dev = MKDEV(pim->major, minor);
	device_destroy(vcrtcm_class, dev);
	vcrtcm_kfree(vcrtcm_minor);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_del_minor);
