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
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pcon.h"
#include "vcrtcm_conn.h"
#include "vcrtcm_drmdev_table.h"

struct class *vcrtcm_class;

struct class *vcrtcm_sysfs_get_class()
{
	return vcrtcm_class;
}
EXPORT_SYMBOL(vcrtcm_sysfs_get_class);

static ssize_t pim_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t pim_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);
static ssize_t pcon_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t pcon_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);
static ssize_t conn_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t conn_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);
static ssize_t conn_pcons_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t conn_pcons_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);
static ssize_t pim_pcons_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t pim_pcons_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);
static ssize_t card_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t card_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);
static ssize_t card_conns_show(struct kobject *kobj, struct attribute *attr,
						char *buf);
static ssize_t card_conns_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size);

static struct kobj_type empty_type;
static struct kobject pims_kobj;
static struct kobject pcons_kobj;
static struct kobject conns_kobj;
static struct kobject cards_kobj;

static const struct sysfs_ops pim_ops = {
	.show = pim_show,
	.store = pim_store
};

static struct attribute pim_name_attr = {
	.name = "name",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pim_id_attr = {
	.name = "id",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pim_desc_attr = {
	.name = "description",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute *pim_attributes[] = {
	&pim_name_attr,
	&pim_id_attr,
	&pim_desc_attr,
	NULL
};

static struct kobj_type pim_type = {
	.sysfs_ops = &pim_ops,
	.default_attrs = pim_attributes,
};

static const struct sysfs_ops pcon_ops = {
	.show = pcon_show,
	.store = pcon_store
};

static struct attribute pcon_desc_attr = {
	.name = "description",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pcon_minor_attr = {
	.name = "minor",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pcon_attach_minor_attr = {
	.name = "attach_minor",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pcon_fps_attr = {
	.name = "fps",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pcon_attached_attr = {
	.name = "attached",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pcon_internal_pconid_attr = {
	.name = "internal_pconid",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute pcon_xfer_mode_attr = {
	.name = "xfer_mode",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute *pcon_attributes[] = {
	&pcon_desc_attr,
	&pcon_fps_attr,
	&pcon_attached_attr,
	&pcon_minor_attr,
	&pcon_attach_minor_attr,
	&pcon_internal_pconid_attr,
	&pcon_xfer_mode_attr,
	NULL
};

static struct kobj_type pcon_type = {
	.sysfs_ops = &pcon_ops,
	.default_attrs = pcon_attributes,
};

static const struct sysfs_ops conn_ops = {
	.show = conn_show,
	.store = conn_store
};

static struct attribute conn_virtual_attr = {
	.name = "virtual",
	.mode = S_IRUSR | S_IRGRP | S_IROTH
};

static struct attribute *conn_attributes[] = {
	&conn_virtual_attr,
	NULL
};

static struct kobj_type conn_type = {
	.sysfs_ops = &conn_ops,
	.default_attrs = conn_attributes,
};

static const struct sysfs_ops conn_pcons_ops = {
	.show = conn_pcons_show,
	.store = conn_pcons_store
};

static struct attribute *conn_pcons_attributes[] = {
	NULL
};

static struct kobj_type conn_pcons_type = {
	.sysfs_ops = &conn_pcons_ops,
	.default_attrs = conn_pcons_attributes,
};

static const struct sysfs_ops pim_pcons_ops = {
	.show = pim_pcons_show,
	.store = pim_pcons_store
};

static struct attribute *pim_pcons_attributes[] = {
	NULL
};

static struct kobj_type pim_pcons_type = {
	.sysfs_ops = &pim_pcons_ops,
	.default_attrs = pim_pcons_attributes,
};

static const struct sysfs_ops card_ops = {
	.show = card_show,
	.store = card_store
};

static struct attribute *card_attributes[] = {
	NULL
};

static struct kobj_type card_type = {
	.sysfs_ops = &card_ops,
	.default_attrs = card_attributes,
};

static const struct sysfs_ops card_conns_ops = {
	.show = card_conns_show,
	.store = card_conns_store
};

static struct attribute *card_conns_attributes[] = {
	NULL
};

static struct kobj_type card_conns_type = {
	.sysfs_ops = &card_conns_ops,
	.default_attrs = card_conns_attributes,
};

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

static ssize_t pim_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

static ssize_t pcon_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct vcrtcm_pcon *pcon;

	pcon = (struct vcrtcm_pcon *)
			container_of(kobj, struct vcrtcm_pcon, kobj);
	if (!pcon)
		return 0;
	if (attr == &pcon_desc_attr) {
		return scnprintf(buf, PAGE_SIZE, "%s\n", pcon->description);
	} else if (attr == &pcon_minor_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n", pcon->minor);
	} else if (attr == &pcon_fps_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n", pcon->fps);
	} else if (attr == &pcon_attached_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n",
				pcon->drm_crtc ? 1 : 0);
	} else if (attr == &pcon_attach_minor_attr) {
		return scnprintf(buf, PAGE_SIZE, "%d\n",
			pcon->attach_minor);
	} else if (attr == &pcon_internal_pconid_attr) {
		return scnprintf(buf, PAGE_SIZE, "0x%08x\n", pcon->pconid);
	} else if (attr == &pcon_xfer_mode_attr) {
		return scnprintf(buf, PAGE_SIZE, "%s\n",
			vcrtcm_xfer_mode_string(pcon->xfer_mode));
	}
	return 0;
}

static ssize_t pcon_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

static ssize_t conn_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct vcrtcm_conn *conn = (struct vcrtcm_conn *)
				container_of(kobj, struct vcrtcm_conn, kobj);

	if (!conn)
		return 0;
	if (attr == &conn_virtual_attr)
		return scnprintf(buf, PAGE_SIZE, "%d\n", conn->virtual);
	return 0;
}

static ssize_t conn_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

static ssize_t conn_pcons_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct vcrtcm_conn *conn = (struct vcrtcm_conn *)
		container_of(kobj, struct vcrtcm_conn, pcons_kobj);

	if (!conn)
		return 0;
	return 0;
}

static ssize_t conn_pcons_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

static ssize_t pim_pcons_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct vcrtcm_pim *pim = (struct vcrtcm_pim *)
				container_of(kobj, struct vcrtcm_pim, pcons_kobj);

	if (!pim)
		return 0;
	return 0;
}

static ssize_t pim_pcons_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

static ssize_t card_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct vcrtcm_drmdev *vdev = (struct vcrtcm_drmdev *)
				container_of(kobj, struct vcrtcm_drmdev, kobj);

	if (!vdev)
		return 0;
	return 0;
}

static ssize_t card_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

static ssize_t card_conns_show(struct kobject *kobj, struct attribute *attr,
						char *buf)
{
	struct vcrtcm_drmdev *vdev = (struct vcrtcm_drmdev *)
		container_of(kobj, struct vcrtcm_drmdev, conns_kobj);

	if (!vdev)
		return 0;
	return 0;
}

static ssize_t card_conns_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t size)
{
	return 0;
}

int vcrtcm_sysfs_init(struct device *vcrtcm_device)
{
	int ret = 0;

	if (!vcrtcm_device) {
		VCRTCM_ERROR("Invalid device, could not set up sysfs...\n");
		return -EINVAL;
	}
	memset(&pims_kobj, 0, sizeof(struct kobject));
	memset(&pcons_kobj, 0, sizeof(struct kobject));
	memset(&empty_type, 0, sizeof(struct kobj_type));
	memset(&conns_kobj, 0, sizeof(struct kobject));
	memset(&cards_kobj, 0, sizeof(struct kobject));
	ret = kobject_init_and_add(&pims_kobj, &empty_type,
					&vcrtcm_device->kobj, "pims");
	if (ret < 0) {
		VCRTCM_ERROR("Error creating sysfs pim node...\n");
		return ret;
	}
	ret = kobject_init_and_add(&pcons_kobj, &empty_type,
					&vcrtcm_device->kobj, "pcons");
	if (ret < 0) {
		VCRTCM_ERROR("Error creating sysfs pcon node...\n");
		return ret;
	}
	ret = kobject_init_and_add(&conns_kobj, &empty_type,
					&vcrtcm_device->kobj, "connectors");
	if (ret < 0) {
		VCRTCM_ERROR("Error creating sysfs conn node...\n");
		return ret;
	}
	ret = kobject_init_and_add(&cards_kobj, &empty_type,
					&vcrtcm_device->kobj, "cards");
	if (ret < 0) {
		VCRTCM_ERROR("Error creating sysfs card node...\n");
		return ret;
	}
	return 0;
}

int vcrtcm_sysfs_add_pim(struct vcrtcm_pim *pim)
{
	int ret = 0;

	if (!pim)
		return -EINVAL;
	ret = kobject_init_and_add(&pim->kobj, &pim_type,
					&pims_kobj, "%s", pim->name);
	if (ret < 0) {
		VCRTCM_ERROR("Error adding pim to sysfs\n");
		return ret;
	}
	ret = kobject_init_and_add(&pim->pcons_kobj, &pim_pcons_type,
					&pim->kobj, "pcons");
	if (ret < 0) {
		VCRTCM_ERROR("Error adding pim/pcons to sysfs\n");
		return ret;
	}
	return 0;
}

void vcrtcm_sysfs_del_pim(struct vcrtcm_pim *pim)
{
	if (!pim)
		return;
	kobject_del(&pim->pcons_kobj);
	kobject_del(&pim->kobj);
}

int vcrtcm_sysfs_add_pcon(struct vcrtcm_pcon *pcon)
{
	int ret = 0;

	if (!pcon)
		return -EINVAL;
	ret = kobject_init_and_add(&pcon->kobj, &pcon_type,
		&pcons_kobj, "%i", PCONID_EXTID(pcon->pconid));
	if (ret < 0) {
		VCRTCM_ERROR("Error adding pcon to sysfs\n");
		return ret;
	}
	ret = sysfs_create_link(&pcon->pim->pcons_kobj, &pcon->kobj,
						pcon->kobj.name);
	if (ret < 0) {
		VCRTCM_ERROR("Error linking pim->pcon in sysfs\n");
		return ret;
	}
	ret = sysfs_create_link(&pcon->kobj, &pcon->pim->kobj, "pim");
	if (ret < 0) {
		VCRTCM_ERROR("Error linking pcon->pim in sysfs\n");
		return ret;
	}
	return 0;
}

void vcrtcm_sysfs_del_pcon(struct vcrtcm_pcon *pcon)
{
	if (!pcon)
		return;
	sysfs_remove_link(&pcon->pim->pcons_kobj, pcon->kobj.name);
	kobject_del(&pcon->kobj);
}

#define NAMEBUFLEN 64

int vcrtcm_sysfs_add_conn(struct vcrtcm_conn *conn)
{
	int ret = 0;
	char namebuf[NAMEBUFLEN];

	snprintf(namebuf, NAMEBUFLEN, "%d:%d",
		MINOR(conn->drm_conn->dev->dev->devt), conn->drm_conn->base.id);
	ret = kobject_init_and_add(&conn->kobj, &conn_type,
		&conns_kobj, "%s", namebuf);
	if (ret < 0) {
		VCRTCM_ERROR("Error adding conn to sysfs\n");
		return ret;
	}
	ret = kobject_init_and_add(&conn->pcons_kobj, &conn_pcons_type,
		&conn->kobj, "pcons");
	if (ret < 0) {
		VCRTCM_ERROR("Error adding conn/pcons to sysfs\n");
		return ret;
	}
	ret = sysfs_create_link(&conn->kobj, &conn->drm_conn->kdev.kobj,
		"drm_connector");
	if (ret < 0) {
		VCRTCM_ERROR("Error linking conn->drmconn in sysfs\n");
		return ret;
	}
	ret = sysfs_create_link(&conn->kobj, &conn->vdev->kobj,
		"card");
	if (ret < 0) {
		VCRTCM_ERROR("Error linking conn->card in sysfs\n");
		return ret;
	}
	snprintf(namebuf, NAMEBUFLEN, "%d", conn->drm_conn->base.id);
	ret = sysfs_create_link(&conn->vdev->conns_kobj, &conn->kobj,
			namebuf);
	if (ret < 0) {
		VCRTCM_ERROR("Error linking card->conn in sysfs\n");
		return ret;
	}
	return 0;
}

void vcrtcm_sysfs_del_conn(struct vcrtcm_conn *conn)
{
	char namebuf[NAMEBUFLEN];

	if (!conn)
		return;
	snprintf(namebuf, NAMEBUFLEN, "%d", conn->drm_conn->base.id);
	sysfs_remove_link(&conn->vdev->conns_kobj, namebuf);
	sysfs_remove_link(&conn->kobj, "drm_connector");
	sysfs_remove_link(&conn->kobj, "card");
	kobject_del(&conn->pcons_kobj);
	kobject_del(&conn->kobj);
}

int vcrtcm_sysfs_attach(struct vcrtcm_pcon *pcon)
{
	int ret = 0;

	ret = sysfs_create_link(&pcon->conn->pcons_kobj, &pcon->kobj,
		pcon->kobj.name);
	if (ret < 0) {
		VCRTCM_ERROR("Error linking conn->pcon in sysfs\n");
		return ret;
	}
	ret = sysfs_create_link(&pcon->kobj, &pcon->conn->kobj, "connector");
	if (ret < 0) {
		VCRTCM_ERROR("Error linking pcon->conn in sysfs\n");
		return ret;
	}
	return 0;
}

int vcrtcm_sysfs_detach(struct vcrtcm_pcon *pcon)
{
	sysfs_remove_link(&pcon->conn->pcons_kobj, pcon->kobj.name);
	sysfs_remove_link(&pcon->kobj, "connector");
	return 0;
}

int vcrtcm_sysfs_add_card(struct vcrtcm_drmdev *vdev)
{
	int ret = 0;
	char namebuf[NAMEBUFLEN];

	snprintf(namebuf, NAMEBUFLEN, "%d", MINOR(vdev->dev->dev->devt));
	ret = kobject_init_and_add(&vdev->kobj, &card_type,
		&cards_kobj, "%s", namebuf);
	if (ret < 0) {
		VCRTCM_ERROR("Error adding card to sysfs\n");
		return ret;
	}
	ret = kobject_init_and_add(&vdev->conns_kobj, &card_conns_type,
					&vdev->kobj, "connectors");
	if (ret < 0) {
		VCRTCM_ERROR("Error adding card/conns to sysfs\n");
		return ret;
	}
	ret = sysfs_create_link(&vdev->kobj, &vdev->dev->dev->kobj,
		"drm_device");
	if (ret < 0) {
		VCRTCM_ERROR("Error linking card->drmdev in sysfs\n");
		return ret;
	}
	return 0;
}

void vcrtcm_sysfs_del_card(struct vcrtcm_drmdev *vdev)
{
	if (!vdev)
		return;
	sysfs_remove_link(&vdev->kobj, "drm_device");
	kobject_del(&vdev->conns_kobj);
	kobject_del(&vdev->kobj);
}

