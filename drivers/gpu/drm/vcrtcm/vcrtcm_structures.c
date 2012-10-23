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
static struct vcrtcm_pim_info *create_pim_info(
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
	INIT_LIST_HEAD(&pim_info->active_pcon_list);
	memset(&pim_info->kobj, 0, sizeof(struct kobject));

	return pim_info;
}

static struct vcrtcm_pim_info *find_pim_info_by_name(char *pim_name)
{
	struct vcrtcm_pim_info *pim_info;

	if (!pim_name)
		return NULL;

	list_for_each_entry(pim_info, &pim_list, pim_list) {
		if (strcmp(pim_info->name, pim_name) == 0)
			return pim_info;
	}

	return NULL;
}

struct vcrtcm_pim_info *vcrtcm_find_pim_info_by_id(int pimid)
{
	struct vcrtcm_pim_info *pim_info;

	list_for_each_entry(pim_info, &pim_list, pim_list) {
		if (pim_info->id == pimid)
			return pim_info;
	}

	return NULL;
}

static void update_pim_info(char *pim_name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *pim_info = find_pim_info_by_name(pim_name);

	if (!pim_info)
		return;

	memcpy(&pim_info->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
}

static void destroy_pim_info(struct vcrtcm_pim_info *info)
{
	if (info)
		vcrtcm_kfree(info, &vcrtcm_kmalloc_track);
}

static void add_pim_info(struct vcrtcm_pim_info *info)
{
	mutex_lock(&pim_list_mutex);
	info->id = next_pimid;
	next_pimid++;
	list_add_tail(&info->pim_list, &pim_list);
	mutex_unlock(&pim_list_mutex);
}

static void remove_pim_info(struct vcrtcm_pim_info *info)
{
	mutex_lock(&pim_list_mutex);
	list_del(&info->pim_list);
	mutex_unlock(&pim_list_mutex);
}

struct vcrtcm_pcon_info *vcrtcm_find_pcon_info(struct vcrtcm_pim_info *pim,
							int pconid)
{
	struct vcrtcm_pcon_info *pcon_info;

	if (!pim)
		return NULL;

	list_for_each_entry(pcon_info, &pim->active_pcon_list, pcon_list)
	{
		if (pcon_info->pim == pim && pcon_info->pconid == pconid)
			return pcon_info;
	}

	return NULL;
}

int vcrtcm_alloc_pconid()
{
	int new_pconid = vcrtcm_id_generator_get(&pconid_generator,
							VCRTCM_ID_REUSE);

	if (new_pconid < 0)
		return -1;

	return new_pconid;
}

void vcrtcm_dealloc_pconid(int pconid)
{
	pconid_table[pconid].pimid = 0;
	pconid_table[pconid].local_pconid = 0;
	pconid_table[pconid].pconid = -1;
	pconid_table[pconid].valid = 0;
	vcrtcm_id_generator_put(&pconid_generator, pconid);
}

int vcrtcm_set_mapping(int pconid, int pimid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return -1;

	pconid_table[pconid].pimid = pimid;
	pconid_table[pconid].pconid = pconid;
	pconid_table[pconid].valid = 1;

	return 0;
}

int vcrtcm_get_pconid(int pimid, int pconid)
{
	int i;
	for (i = 0; i < MAX_NUM_PCONIDS; i++) {
		if (pconid_table[i].pimid == pimid &&
			pconid_table[i].pconid == pconid) {
			return i;
		}
	}
	return -1;
}

int vcrtcm_pconid_valid(int pconid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return 0;

	return pconid_table[pconid].valid;
}

int vcrtcm_get_pimid(int pconid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return -1;

	return pconid_table[pconid].pimid;
}

int vcrtcm_get_local_pconid(int pconid)
{
	if (pconid >= MAX_NUM_PCONIDS)
		return -1;

	return pconid_table[pconid].local_pconid;
}

/* Functions that are exported for PIMs to call */
int vcrtcm_pim_register(char *pim_name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *pim_info;

	VCRTCM_INFO("Registering PIM %s, funcs at %p\n", pim_name, funcs);

	pim_info = find_pim_info_by_name(pim_name);
	if (pim_info) {
		update_pim_info(pim_name, funcs);
		return 1;
	}

	pim_info = create_pim_info(pim_name, funcs);
	if (!pim_info)
		return -ENOMEM;

	add_pim_info(pim_info);
	vcrtcm_sysfs_add_pim(pim_info);

	return 1;
}
EXPORT_SYMBOL(vcrtcm_pim_register);

void vcrtcm_pim_unregister(char *pim_name)
{
	struct vcrtcm_pim_info *pim_info = find_pim_info_by_name(pim_name);
	struct vcrtcm_pcon_info *pcon_info, *tmp;

	if (!pim_info)
		return;

	VCRTCM_INFO("Unregistering PIM %s\n", pim_name);

	list_for_each_entry_safe(pcon_info, tmp,
				&pim_info->active_pcon_list, pcon_list) {
		VCRTCM_ERROR("PIM %s's PCON %i "
			"was not invalidated before calling "
			"vcrtcm_pim_unregister(). Doing that now...\n",
			pim_name, pcon_info->pconid);
		vcrtcm_p_destroy(pim_name, pcon_info->pconid);
	}

	remove_pim_info(pim_info);
	vcrtcm_sysfs_del_pim(pim_info);
	destroy_pim_info(pim_info);
}
EXPORT_SYMBOL(vcrtcm_pim_unregister);

void vcrtcm_p_destroy(char *pim_name, int pconid)
{
	struct vcrtcm_pim_info *pim_info;
	struct vcrtcm_pcon_info *pcon_info;

	pim_info = find_pim_info_by_name(pim_name);
	if (!pim_info)
		return;
	pcon_info = vcrtcm_find_pcon_info(pim_info, pconid);
	if (!pcon_info)
		return;
	VCRTCM_INFO("Invalidating pcon %d, info %p\n", pconid, pim_info);
	vcrtcm_sysfs_del_pcon(pcon_info);
	vcrtcm_del_pcon(pconid);
	list_del(&pcon_info->pcon_list);
	vcrtcm_kfree(pcon_info, &vcrtcm_kmalloc_track);
}
EXPORT_SYMBOL(vcrtcm_p_destroy);

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

