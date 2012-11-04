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
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include "vcrtcm_pim_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_module.h"

static int next_pimid;

struct vcrtcm_pim *vcrtcm_create_pim(
	char *pim_name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim *pim;

	if (!pim_name || !funcs)
		return NULL;

	pim = (struct vcrtcm_pim *)vcrtcm_kmalloc(
						sizeof(struct vcrtcm_pim),
							GFP_KERNEL,
							&vcrtcm_kmalloc_track);

	if (!pim)
		return NULL;

	strncpy(pim->name, pim_name, PIM_NAME_MAXLEN);
	memcpy(&pim->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
	INIT_LIST_HEAD(&pim->pcons_in_pim_list);
	memset(&pim->kobj, 0, sizeof(struct kobject));

	return pim;
}

struct vcrtcm_pim *vcrtcm_find_pim_by_name(char *pim_name)
{
	struct vcrtcm_pim *pim;

	if (!pim_name)
		return NULL;

	list_for_each_entry(pim, &vcrtcm_pim_list, pim_list) {
		if (strcmp(pim->name, pim_name) == 0)
			return pim;
	}

	return NULL;
}

struct vcrtcm_pim *vcrtcm_find_pim_by_id(int pimid)
{
	struct vcrtcm_pim *pim;

	list_for_each_entry(pim, &vcrtcm_pim_list, pim_list) {
		if (pim->id == pimid)
			return pim;
	}

	return NULL;
}

void vcrtcm_destroy_pim(struct vcrtcm_pim *pim)
{
	if (pim)
		vcrtcm_kfree(pim, &vcrtcm_kmalloc_track);
}

void vcrtcm_add_pim(struct vcrtcm_pim *pim)
{
	mutex_lock(&vcrtcm_pim_list_mutex);
	pim->id = next_pimid;
	next_pimid++;
	list_add_tail(&pim->pim_list, &vcrtcm_pim_list);
	mutex_unlock(&vcrtcm_pim_list_mutex);
}

void vcrtcm_remove_pim(struct vcrtcm_pim *pim)
{
	mutex_lock(&vcrtcm_pim_list_mutex);
	list_del(&pim->pim_list);
	mutex_unlock(&vcrtcm_pim_list_mutex);
}
