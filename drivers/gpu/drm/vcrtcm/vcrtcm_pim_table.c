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
#include <vcrtcm/vcrtcm_pcon.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include "vcrtcm_pim_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_module.h"

static int next_pimid;

struct vcrtcm_pim_info *vcrtcm_create_pim_info(
	char *pim_name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *pim_info;

	if (!pim_name || !funcs)
		return NULL;

	pim_info = (struct vcrtcm_pim_info *)vcrtcm_kmalloc(
						sizeof(struct vcrtcm_pim_info),
							GFP_KERNEL,
							&vcrtcm_kmalloc_track);

	if (!pim_info)
		return NULL;

	strncpy(pim_info->name, pim_name, PIM_NAME_MAXLEN);
	memcpy(&pim_info->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
	INIT_LIST_HEAD(&pim_info->pcons_in_pim_list);
	memset(&pim_info->kobj, 0, sizeof(struct kobject));

	return pim_info;
}

struct vcrtcm_pim_info *vcrtcm_find_pim_info_by_name(char *pim_name)
{
	struct vcrtcm_pim_info *pim_info;

	if (!pim_name)
		return NULL;

	list_for_each_entry(pim_info, &vcrtcm_pim_list, pim_list) {
		if (strcmp(pim_info->name, pim_name) == 0)
			return pim_info;
	}

	return NULL;
}

struct vcrtcm_pim_info *vcrtcm_find_pim_info_by_id(int pimid)
{
	struct vcrtcm_pim_info *pim_info;

	list_for_each_entry(pim_info, &vcrtcm_pim_list, pim_list) {
		if (pim_info->id == pimid)
			return pim_info;
	}

	return NULL;
}

void vcrtcm_destroy_pim_info(struct vcrtcm_pim_info *info)
{
	if (info)
		vcrtcm_kfree(info, &vcrtcm_kmalloc_track);
}

void vcrtcm_add_pim_info(struct vcrtcm_pim_info *info)
{
	mutex_lock(&vcrtcm_pim_list_mutex);
	info->id = next_pimid;
	next_pimid++;
	list_add_tail(&info->pim_list, &vcrtcm_pim_list);
	mutex_unlock(&vcrtcm_pim_list_mutex);
}

void vcrtcm_remove_pim_info(struct vcrtcm_pim_info *info)
{
	mutex_lock(&vcrtcm_pim_list_mutex);
	list_del(&info->pim_list);
	mutex_unlock(&vcrtcm_pim_list_mutex);
}
