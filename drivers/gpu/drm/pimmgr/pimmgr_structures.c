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
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/pimmgr.h>
#include "pimmgr_private.h"
#include "pimmgr_sysfs.h"

static uint32_t next_pimid;

struct pim_info *create_pim_info(char *name, struct pim_funcs *funcs)
{
	struct pim_info *info;

	if (!name || !funcs)
		return NULL;

	info = (struct pim_info *) vcrtcm_kmalloc(sizeof(struct pim_info),
							GFP_KERNEL,
							&pimmgr_kmalloc_track);

	if (!info)
		return NULL;

	strncpy(info->name, name, PIM_NAME_MAXLEN);
	memcpy(&info->funcs, funcs, sizeof(struct pim_funcs));
	INIT_LIST_HEAD(&info->active_pcon_list);
	memset(&info->kobj, 0, sizeof(struct kobject));

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
void update_pim_info(char *name, struct pim_funcs *funcs)
{
	struct pim_info *info = find_pim_info_by_name(name);

	if (!info)
		return;

	memcpy(&info->funcs, funcs, sizeof(struct pim_funcs));
}

void destroy_pim_info(struct pim_info *info)
{
	if (info)
		vcrtcm_kfree(info, &pimmgr_kmalloc_track);
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

struct pimmgr_pcon_info *find_pimmgr_pcon_info(struct pim_info *pim,
							uint32_t local_id)
{
	struct pimmgr_pcon_info *pcon;

	if (!pim)
		return NULL;

	list_for_each_entry(pcon, &pim->active_pcon_list, pcon_list)
	{
		if (pcon->pim == pim && pcon->local_id == local_id)
			return pcon;
	}

	return NULL;
}

int pimmgr_pim_register(char *name, struct pim_funcs *funcs)
{
	struct pim_info *info;

	info = find_pim_info_by_name(name);
	if (info) {
		update_pim_info(name, funcs);
		return 1;
	}

	info = create_pim_info(name, funcs);
	if (!info)
		return -ENOMEM;

	add_pim_info(info);
	vcrtcm_sysfs_add_pim(info);

	return 1;
}
EXPORT_SYMBOL(pimmgr_pim_register);

void pimmgr_pim_unregister(char *name)
{
	struct pim_info *info = find_pim_info_by_name(name);

	if (!info)
		return;

	remove_pim_info(info);
	vcrtcm_sysfs_del_pim(info);
	destroy_pim_info(info);
}
EXPORT_SYMBOL(pimmgr_pim_unregister);

void pimmgr_pcon_invalidate(char *name, uint32_t pcon_local_id)
{
	struct pim_info *info;
	struct pimmgr_pcon_info *pcon;
	uint32_t pconid;

	info = find_pim_info_by_name(name);
	if (!info)
		return;

	pcon = find_pimmgr_pcon_info(info, pcon_local_id);
	if (!pcon)
		return;

	pconid = CREATE_PCONID(info->id, pcon_local_id);
	VCRTCM_INFO("Invalidating pcon, info %p, pconid %u\n", info, pconid);

	vcrtcm_sysfs_del_pcon(pcon);
	vcrtcm_p_del(pconid);
	list_del(&pcon->pcon_list);
	vcrtcm_kfree(pcon, &pimmgr_kmalloc_track);
}
EXPORT_SYMBOL(pimmgr_pcon_invalidate);
