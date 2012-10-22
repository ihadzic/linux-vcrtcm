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
#include "vcrtcm_private.h"
#include "vcrtcm_sysfs.h"

static int next_pimid;

static struct pconid_mapping *pconid_table;
static struct mutex pconid_table_mutex;
static struct vcrtcm_id_generator pconid_generator;

/* Helper functions to manage PIMMGR structures */
struct vcrtcm_pim_info *create_pim_info(char *name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *info;

	if (!name || !funcs)
		return NULL;

	info = (struct vcrtcm_pim_info *) vcrtcm_kmalloc(sizeof(struct vcrtcm_pim_info),
							GFP_KERNEL,
							&vcrtcm_kmalloc_track);

	if (!info)
		return NULL;

	strncpy(info->name, name, PIM_NAME_MAXLEN);
	memcpy(&info->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
	INIT_LIST_HEAD(&info->active_pcon_list);
	memset(&info->kobj, 0, sizeof(struct kobject));

	return info;
}

struct vcrtcm_pim_info *find_pim_info_by_name(char *name)
{
	struct vcrtcm_pim_info *info;

	if (!name)
		return NULL;

	list_for_each_entry(info, &pim_list, pim_list) {
		if (strcmp(info->name, name) == 0)
			return info;
	}

	return NULL;
}

struct vcrtcm_pim_info *find_pim_info_by_id(int pimid)
{
	struct vcrtcm_pim_info *info;

	list_for_each_entry(info, &pim_list, pim_list) {
		if (info->id == pimid)
			return info;
	}

	return NULL;
}
void update_pim_info(char *name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *info = find_pim_info_by_name(name);

	if (!info)
		return;

	memcpy(&info->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
}

void destroy_pim_info(struct vcrtcm_pim_info *info)
{
	if (info)
		vcrtcm_kfree(info, &vcrtcm_kmalloc_track);
}

void add_pim_info(struct vcrtcm_pim_info *info)
{
	mutex_lock(&pim_list_mutex);
	info->id = next_pimid;
	next_pimid++;
	list_add_tail(&info->pim_list, &pim_list);
	mutex_unlock(&pim_list_mutex);
}

void remove_pim_info(struct vcrtcm_pim_info *info)
{
	mutex_lock(&pim_list_mutex);
	list_del(&info->pim_list);
	mutex_unlock(&pim_list_mutex);
}

struct vcrtcm_pcon_info *find_pcon_info(struct vcrtcm_pim_info *pim,
							int local_pconid)
{
	struct vcrtcm_pcon_info *pcon;

	if (!pim)
		return NULL;

	list_for_each_entry(pcon, &pim->active_pcon_list, pcon_list)
	{
		if (pcon->pim == pim && pcon->local_pconid == local_pconid)
			return pcon;
	}

	return NULL;
}

int alloc_pconid()
{
	int new_pconid = vcrtcm_id_generator_get(&pconid_generator,
							VCRTCM_ID_REUSE);

	if (new_pconid < 0)
		return -1;

	return new_pconid;
}

void dealloc_pconid(int pconid)
{
	pconid_table[pconid].pimid = 0;
	pconid_table[pconid].local_pconid = 0;
	pconid_table[pconid].valid = 0;
	vcrtcm_id_generator_put(&pconid_generator, pconid);
}

int pconid_set_mapping(int pconid, int pimid, int local_pconid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return -1;

	pconid_table[pconid].pimid = pimid;
	pconid_table[pconid].local_pconid = local_pconid;
	pconid_table[pconid].valid = 1;

	return 0;
}

int get_pconid(int pimid, int local_pconid)
{
	int i;
	for (i = 0; i < MAX_NUM_PCONIDS; i++) {
		if (pconid_table[i].pimid == pimid &&
			pconid_table[i].local_pconid == local_pconid) {
			return i;
		}
	}
	return -1;
}

int pconid_valid(int pconid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return 0;

	return pconid_table[pconid].valid;
}

int pconid_get_pimid(int pconid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return -1;

	return pconid_table[pconid].pimid;
}

int pconid_get_local_pconid(int pconid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return -1;

	return pconid_table[pconid].local_pconid;
}

/* Functions that are exported for PIMs to call */
int vcrtcm_pim_register(char *name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *info;

	VCRTCM_INFO("Registering PIM %s, funcs at %p\n", name, funcs);

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
EXPORT_SYMBOL(vcrtcm_pim_register);

void vcrtcm_pim_unregister(char *name)
{
	struct vcrtcm_pim_info *info = find_pim_info_by_name(name);
	struct vcrtcm_pcon_info *pcon, *tmp;

	if (!info)
		return;

	VCRTCM_INFO("Unregistering PIM %s\n", name);

	list_for_each_entry_safe(pcon, tmp,
				&info->active_pcon_list, pcon_list) {
		VCRTCM_ERROR("PIM %s's PCON with local id %i "
			"was not invalidated before calling "
			"vcrtcm_pim_unregister(). Doing that now...\n",
			name, pcon->local_pconid);
		vcrtcm_p_invalidate(name, pcon->local_pconid);
	}

	remove_pim_info(info);
	vcrtcm_sysfs_del_pim(info);
	destroy_pim_info(info);
}
EXPORT_SYMBOL(vcrtcm_pim_unregister);

void vcrtcm_p_invalidate(char *name, int local_pconid)
{
	struct vcrtcm_pim_info *info;
	struct vcrtcm_pcon_info *pcon;
	int pconid;

	info = find_pim_info_by_name(name);
	if (!info)
		return;

	pcon = find_pcon_info(info, local_pconid);
	if (!pcon)
		return;

	pconid = get_pconid(info->id, local_pconid);
	if (pconid < 0)
		return;

	VCRTCM_INFO("Invalidating pcon, info %p, pconid %i\n", info, pconid);

	vcrtcm_sysfs_del_pcon(pcon);
	vcrtcm_p_del(pconid);
	list_del(&pcon->pcon_list);
	vcrtcm_kfree(pcon, &vcrtcm_kmalloc_track);
}
EXPORT_SYMBOL(vcrtcm_p_invalidate);

/* Functions for module init to call to set things up. */
int vcrtcm_structures_init()
{
	int result = 0;
	pconid_table = (struct pconid_mapping *) vcrtcm_kmalloc(
			sizeof(struct pconid_mapping) * MAX_NUM_PCONIDS,
			GFP_KERNEL,
			&vcrtcm_kmalloc_track);

	if (!pconid_table)
		return -ENOMEM;

	mutex_init(&pconid_table_mutex);

	result = vcrtcm_id_generator_init(&pconid_generator, MAX_NUM_PCONIDS);

	if (result < 0) {
		vcrtcm_structures_destroy();
		return result;
	}

	return 0;
}

void vcrtcm_structures_destroy()
{
	if (pconid_table)
		vcrtcm_kfree(pconid_table, &vcrtcm_kmalloc_track);
	vcrtcm_id_generator_destroy(&pconid_generator);
}

