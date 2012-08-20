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

#include <vcrtcm/vcrtcm_utils.h>
#include "pimmgr.h"
#include "pimmgr_sysfs.h"

/* Prototypes of sysfs property read/write functions. */
static ssize_t pim_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t pim_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);
static ssize_t pcon_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t pcon_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);

/* Empty kobj_type to use for the "pims" and "pcons" directory. */
static struct kobj_type empty_type;

/* Kobject for "pims" directory. */
static struct kobject pims_kobj;

/* Kobject for "pcons" directory. */
static struct kobject pcons_kobj;

/* Operations to read/write properties on a PIM entry. */
static const struct sysfs_ops pim_ops = {
	.show = pim_show,
	.store = pim_store
};

/* PIM description attribute. */
static struct attribute pim_desc_attr = {
	.name = "description",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* Array of PIM attributes. */
static struct attribute *pim_attributes[] = {
	&pim_desc_attr,
	NULL
};

/* Definition of PIM kobj_type. */
static struct kobj_type pim_type = {
	.sysfs_ops = &pim_ops,
	.default_attrs = pim_attributes,
};

/* Operations to read/write properties on a PCON entry. */
static const struct sysfs_ops pcon_ops = {
	.show = pcon_show,
	.store = pcon_store
};

/* PCON description attribute. */
static struct attribute pcon_desc_attr = {
	.name = "description",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* PCON localid attribute. */
static struct attribute pcon_localid_attr = {
	.name = "localid",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* Array of PCON attributes. */
static struct attribute *pcon_attributes[] = {
	&pcon_desc_attr,
	&pcon_localid_attr,
	NULL
};

/* Definition of PCON kobj_type. */
static struct kobj_type pcon_type = {
	.sysfs_ops = &pcon_ops,
	.default_attrs = pcon_attributes,
};

/* Implementation of the function to read PIM properties. */
static ssize_t pim_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct pim_info *pim = (struct pim_info *)
				container_of(kobj, struct pim_info, kobj);

	if (pim)
		return scnprintf(buf,
				PAGE_SIZE,
				"This is the PIM that handles type %s\n",
				pim->name);

	return 0;
}

/* Implementation of the function to write PIM properties (unused). */
static ssize_t pim_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

/* Implementation of the function to read PCON properties. */
static ssize_t pcon_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct pimmgr_pcon_info *pcon;
	struct pim_info *pim;

	pcon = (struct pimmgr_pcon_info *)
			container_of(kobj, struct pimmgr_pcon_info, kobj);
	if (!pcon)
		return 0;

	if (attr == &pcon_desc_attr) {
		return scnprintf(buf, PAGE_SIZE, "%s\n", pcon->description);
	} else if (attr == &pcon_localid_attr) {
		pim = pcon->pim;
		if (!pim)
			return 0;

		return scnprintf(buf, PAGE_SIZE, "%s:%u\n", pim->name,
							pcon->local_id);
	}

	return 0;
}

/* Implementation of the function to write PCON properties (unused). */
static ssize_t pcon_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

/* Initialize the pimmgr sysfs stuff (called from init). */
void pimmgr_sysfs_init(struct device *pimmgr_device)
{
	int ret = 0;

	if (!pimmgr_device) {
		VCRTCM_ERROR("Invalid device, could not set up sysfs...\n");
		return;
	}

	memset(&pims_kobj, 0, sizeof(struct kobject));
	memset(&pcons_kobj, 0, sizeof(struct kobject));
	memset(&empty_type, 0, sizeof(struct kobj_type));

	ret = kobject_init_and_add(&pims_kobj, &empty_type,
					&pimmgr_device->kobj, "pims");
	if (ret < 0)
		VCRTCM_ERROR("Error creating sysfs pim node...\n");

	ret = kobject_init_and_add(&pcons_kobj, &empty_type,
					&pimmgr_device->kobj, "pcons");
	if (ret < 0)
		VCRTCM_ERROR("Error creating sysfs pcon node...\n");
}

/* Add a PIM's info to sysfs. */
int vcrtcm_sysfs_add_pim(struct pim_info *pim)
{
	int ret = 0;

	if (!pim)
		return -EINVAL;

	ret = kobject_init_and_add(&pim->kobj, &pim_type,
					&pims_kobj, "%s", pim->name);

	if (ret < 0)
		VCRTCM_ERROR("Error adding pim to sysfs\n");

	return ret;
}

/* Delete a PIM's info from sysfs. */
void vcrtcm_sysfs_del_pim(struct pim_info *pim)
{
	if (!pim)
		return;

	kobject_del(&pim->kobj);
}

/* Add a PCON's info to sysfs. */
int vcrtcm_sysfs_add_pcon(struct pimmgr_pcon_info *pcon)
{
	int ret = 0;

	if (!pcon)
		return -EINVAL;

	ret = kobject_init_and_add(&pcon->kobj, &pcon_type, &pcons_kobj, "%u",
		CREATE_PCONID(pcon->pim->id, pcon->local_id));
	if (ret < 0) {
		VCRTCM_ERROR("Error adding pcon to sysfs\n");
		return ret;
	}

	ret = sysfs_create_link(&pcon->pim->kobj, &pcon->kobj,
						pcon->kobj.name);
	if (ret < 0)
		VCRTCM_ERROR("Error linking pcon to pim in sysfs\n");

	ret = sysfs_create_link(&pcon->kobj, &pcon->pim->kobj,
						pcon->pim->kobj.name);
	if (ret < 0)
		VCRTCM_ERROR("Error linking pim to pcon in sysfs\n");

	return 1;
}

/* Delete a PCON's info from sysfs. */
void vcrtcm_sysfs_del_pcon(struct pimmgr_pcon_info *pcon)
{
	if (!pcon)
		return;

	sysfs_remove_link(&pcon->pim->kobj, pcon->kobj.name);
	kobject_del(&pcon->kobj);
}
