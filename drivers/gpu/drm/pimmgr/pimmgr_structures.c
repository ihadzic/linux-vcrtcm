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

#include <linux/string.h>
#include <linux/slab.h>
#include <vcrtcm/vcrtcm_pcon.h>
#include <vcrtcm/pimmgr.h>

#include "pimmgr_utils.h"

static uint32_t next_pimid;

struct pim_info *create_pim_info(char *name, struct pim_funcs *funcs,
						void *data)
{
	struct pim_info *info;

	if (!name || !funcs)
		return NULL;

	info = (struct pim_info *) pimmgr_kmalloc(sizeof(struct pim_info),
							GFP_KERNEL);

	strncpy(info->name, name, PIM_NAME_LEN);
	memcpy(&info->funcs, funcs, sizeof(struct pim_funcs));
	info->data = data;

	return info;
}

struct pim_info *find_pim_info_by_name(char *name)
{
	struct pim_info *info;

	if (!name)
		return NULL;

	list_for_each_entry(info, &pim_list, pim_list) {
		if (strcmp(info->name, name) == 0)
			return info;
	}

	return NULL;
}

struct pim_info *find_pim_info_by_id(uint32_t pim_id)
{
	struct pim_info *info;

	list_for_each_entry(info, &pim_list, pim_list) {
		if (info->id == pim_id)
			return info;
	}

	return NULL;
}
void update_pim_info(char *name, struct pim_funcs *funcs, void *data)
{
	struct pim_info *info = find_pim_info_by_name(name);

	if (!info)
		return;

	memcpy(&info->funcs, funcs, sizeof(struct pim_funcs));
	info->data = data;
}

void destroy_pim_info(struct pim_info *info)
{
	if (info)
		pimmgr_kfree(info);
}

void add_pim_info(struct pim_info *info)
{
	mutex_lock(&pim_list_mutex);
	info->id = next_pimid;
	next_pimid++;
	list_add_tail(&info->pim_list, &pim_list);
	mutex_unlock(&pim_list_mutex);
}

void remove_pim_info(struct pim_info *info)
{
	mutex_lock(&pim_list_mutex);
	list_del(&info->pim_list);
	mutex_unlock(&pim_list_mutex);
}

int pimmgr_pim_register(char *name, struct pim_funcs *funcs, void *data)
{
	struct pim_info *info;

	info = find_pim_info_by_name(name);
	if (info) {
		update_pim_info(name, funcs, data);
		return 1;
	}

	info = create_pim_info(name, funcs, data);
	create_pim_info(name, funcs, data);
	if (!info)
		return -ENOMEM;

	add_pim_info(info);

	return 1;
}
EXPORT_SYMBOL(pimmgr_pim_register);

void pimmgr_pim_unregister(char *name)
{
	struct pim_info *info = find_pim_info_by_name(name);

	if (!info)
		return;

	remove_pim_info(info);
}
EXPORT_SYMBOL(pimmgr_pim_unregister);

void pimmgr_pcon_invalidate(char *name, uint32_t pcon_local_id)
{
	struct pim_info *info;
	uint32_t pconid;

	info = find_pim_info_by_name(name);
	if (!info)
		return;

	pconid = CREATE_PCONID(info->id, pcon_local_id);

	PR_INFO("Invalidating pcon, info %p, pconid %u\n", info, pconid);

	vcrtcm_p_del(pconid);
}
EXPORT_SYMBOL(pimmgr_pcon_invalidate);
