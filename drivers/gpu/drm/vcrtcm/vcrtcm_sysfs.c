/*
 * Copyright (C) 2012 Alcatel-Lucent, Inc.
 * Authors:
 *      Ilija Hadzic <ihadzic@research.bell-labs.com>
 *      Martin Carroll <martin.carroll@research.bell-labs.com>
 *      William Katsak <wkatsak@cs.rutgers.edu>
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
#include <vcrtcm/vcrtcm_sysfs.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_pcon.h"

struct class *vcrtcm_class;

struct class *vcrtcm_sysfs_get_class()
{
	return vcrtcm_class;
}
EXPORT_SYMBOL(vcrtcm_sysfs_get_class);

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

/* PIM name attribute */
static struct attribute pim_name_attr = {
	.name = "name",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* PIM id attribute. */
static struct attribute pim_id_attr = {
	.name = "id",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* PIM description attribute. */
static struct attribute pim_desc_attr = {
	.name = "description",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* Array of PIM attributes. */
static struct attribute *pim_attributes[] = {
	&pim_name_attr,
	&pim_id_attr,
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

/* PCON minor attribute. */
static struct attribute pcon_minor_attr = {
	.name = "minor",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* PCON localid attribute. */
static struct attribute pcon_local_pconid_attr = {
	.name = "local_pconid",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* PCON fps attribute. */
static struct attribute pcon_fps_attr = {
	.name = "fps",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* PCON attached attribute */
static struct attribute pcon_attached_attr = {
	.name = "attached",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

/* Array of PCON attributes. */
static struct attribute *pcon_attributes[] = {
	&pcon_desc_attr,
	&pcon_local_pconid_attr,
	&pcon_fps_attr,
	&pcon_attached_attr,
	&pcon_minor_attr,
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
	struct vcrtcm_pim *pim = (struct vcrtcm_pim *)
				container_of(kobj, struct vcrtcm_pim, kobj);

	if (!pim)
		return 0;
	if (attr == &pim_name_attr) {
		return scnprintf(buf, PAGE_SIZE, "%s\n", pim->name);
	} else if (attr == &pim_id_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n", pim->id);
	} else if (attr == &pim_desc_attr) {
		return scnprintf(buf,
				PAGE_SIZE,
				"This is the PIM that handles type %s\n",
				pim->name);
	}

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
	struct vcrtcm_pcon *pcon;
	struct vcrtcm_pim *pim;

	pcon = (struct vcrtcm_pcon *)
			container_of(kobj, struct vcrtcm_pcon, kobj);
	if (!pcon)
		return 0;

	if (attr == &pcon_desc_attr) {
		return scnprintf(buf, PAGE_SIZE, "%s\n", pcon->description);
	} else if (attr == &pcon_minor_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n", pcon->minor);
	} else if (attr == &pcon_local_pconid_attr) {
		pim = pcon->pim;
		if (!pim)
			return 0;

		return scnprintf(buf, PAGE_SIZE, "unknown\n");
	} else if (attr == &pcon_fps_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n", pcon->fps);
	} else if (attr == &pcon_attached_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n",
				pcon->drm_crtc ? 1 : 0);
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
void vcrtcm_sysfs_init(struct device *vcrtcm_device)
{
	int ret = 0;

	if (!vcrtcm_device) {
		VCRTCM_ERROR("Invalid device, could not set up sysfs...\n");
		return;
	}

	memset(&pims_kobj, 0, sizeof(struct kobject));
	memset(&pcons_kobj, 0, sizeof(struct kobject));
	memset(&empty_type, 0, sizeof(struct kobj_type));

	ret = kobject_init_and_add(&pims_kobj, &empty_type,
					&vcrtcm_device->kobj, "pims");
	if (ret < 0)
		VCRTCM_ERROR("Error creating sysfs pim node...\n");

	ret = kobject_init_and_add(&pcons_kobj, &empty_type,
					&vcrtcm_device->kobj, "pcons");
	if (ret < 0)
		VCRTCM_ERROR("Error creating sysfs pcon node...\n");
}

int vcrtcm_sysfs_add_pim(struct vcrtcm_pim *pim)
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

void vcrtcm_sysfs_del_pim(struct vcrtcm_pim *pim)
{
	if (!pim)
		return;

	kobject_del(&pim->kobj);
}

int vcrtcm_sysfs_add_pcon(struct vcrtcm_pcon *pcon)
{
	int ret = 0;

	if (!pcon)
		return -EINVAL;

	ret = kobject_init_and_add(&pcon->kobj, &pcon_type,
		&pcons_kobj, "%i", pcon->pconid);
	if (ret < 0) {
		VCRTCM_ERROR("Error adding pcon to sysfs\n");
		return ret;
	}

	ret = sysfs_create_link(&pcon->pim->kobj, &pcon->kobj,
						pcon->kobj.name);
	if (ret < 0)
		VCRTCM_ERROR("Error linking pcon to pim in sysfs\n");

	ret = sysfs_create_link(&pcon->kobj, &pcon->pim->kobj, "pim");

	if (ret < 0)
		VCRTCM_ERROR("Error linking pim to pcon in sysfs\n");

	return 1;
}

void vcrtcm_sysfs_del_pcon(struct vcrtcm_pcon *pcon)
{
	if (!pcon)
		return;

	sysfs_remove_link(&pcon->pim->kobj, pcon->kobj.name);
	kobject_del(&pcon->kobj);
}
