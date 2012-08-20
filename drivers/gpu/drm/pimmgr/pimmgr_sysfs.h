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

#ifndef __PIMMGR_SYSFS_H__
#define __PIMMGR_SYSFS_H__

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include "pimmgr.h"

ssize_t pim_show(struct kobject *kobj, struct attribute *attr, char *buf);
ssize_t pim_store(struct kobject *kobj, struct attribute *attr,
					const char *buf, size_t size);
ssize_t pcon_show(struct kobject *kobj, struct attribute *attr, char *buf);
ssize_t pcon_store(struct kobject *kobj, struct attribute *attr,
					const char *buf, size_t size);

extern struct kobject pims_kobj;
extern struct kobject pcons_kobj;
extern struct kobj_type pim_type;
extern struct kobj_type pcon_type;

int vcrtcm_sysfs_add_pim(struct pim_info *pim);
void vcrtcm_sysfs_del_pim(struct pim_info *pim);
int vcrtcm_sysfs_add_pcon(struct pimmgr_pcon_info *pcon);
void vcrtcm_sysfs_del_pcon(struct pimmgr_pcon_info *pcon);

#endif

