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
static struct pconid_table_entry pconid_table[MAX_NUM_PCONIDS];
static struct mutex pconid_table_mutex;

/* Helper functions to manage VCRTCM structures */
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
	INIT_LIST_HEAD(&pim_info->pcons_in_pim_list);
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

struct vcrtcm_pcon_info *vcrtcm_alloc_pcon_info()
{
	int k;

	mutex_lock(&pconid_table_mutex);
	for (k = 0; k < MAX_NUM_PCONIDS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];
		if (!entry->pcon_info) {
			struct vcrtcm_pcon_info *pcon_info;
			pcon_info = vcrtcm_kzalloc(sizeof(struct vcrtcm_pcon_info),
				GFP_KERNEL, &vcrtcm_kmalloc_track);
			if (!pcon_info) {
				VCRTCM_INFO("allocate of pcon_info failed\n");
				mutex_unlock(&pconid_table_mutex);
				return NULL;
			}
			pcon_info->pconid = k;
			pcon_info->minor = -1;
			spin_lock_init(&pcon_info->lock);
			mutex_init(&pcon_info->mutex);
			entry->pcon_info = pcon_info;
			mutex_unlock(&pconid_table_mutex);
			return pcon_info;
		}
	}
	mutex_unlock(&pconid_table_mutex);
	return NULL;
}

void vcrtcm_dealloc_pcon_info(int pconid)
{
	struct pconid_table_entry *entry;

	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS)
		return;
	mutex_lock(&pconid_table_mutex);
	entry = &pconid_table[pconid];
	if (entry->pcon_info != NULL)
		vcrtcm_kfree(entry->pcon_info, &vcrtcm_kmalloc_track);
	entry->pcon_info = NULL;
	mutex_unlock(&pconid_table_mutex);
}

struct vcrtcm_pcon_info *vcrtcm_get_pcon_info(int pconid)
{
	struct vcrtcm_pcon_info *ret;

	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS)
		return NULL;
	mutex_lock(&pconid_table_mutex);
	ret = pconid_table[pconid].pcon_info;
	mutex_unlock(&pconid_table_mutex);
	return ret;
}

int vcrtcm_pconid_valid(int pconid)
{
	int ret;

	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS)
		return 0;
	mutex_lock(&pconid_table_mutex);
	ret = pconid_table[pconid].pcon_info != NULL;
	mutex_unlock(&pconid_table_mutex);
	return ret;
}

/* Functions that are exported for PIMs to call */
int vcrtcm_pim_register(char *pim_name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *pim_info;

	VCRTCM_INFO("Registering PIM %s, funcs at %p\n", pim_name, funcs);

	pim_info = find_pim_info_by_name(pim_name);
	if (pim_info) {
		memcpy(&pim_info->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
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
	struct vcrtcm_pim_info *pim_info;
	struct vcrtcm_pcon_info *pcon_info, *tmp;

	pim_info = find_pim_info_by_name(pim_name);
	if (!pim_info)
		return;
	VCRTCM_INFO("unregistering PIM %s\n", pim_name);
	list_for_each_entry_safe(pcon_info, tmp,
			&pim_info->pcons_in_pim_list, pcons_in_pim_list) {
		vcrtcm_p_destroy(pcon_info->pconid);
	}
	remove_pim_info(pim_info);
	vcrtcm_sysfs_del_pim(pim_info);
	destroy_pim_info(pim_info);
}
EXPORT_SYMBOL(vcrtcm_pim_unregister);

void vcrtcm_p_destroy(int pconid)
{
	struct vcrtcm_pcon_info *pcon_info;

	pcon_info = vcrtcm_get_pcon_info(pconid);
	if (!pcon_info)
		return;
	VCRTCM_INFO("destroying pcon %d\n", pconid);
	vcrtcm_sysfs_del_pcon(pcon_info);
	vcrtcm_del_pcon(pconid);
	list_del(&pcon_info->pcons_in_pim_list);
	vcrtcm_dealloc_pcon_info(pconid);
}
EXPORT_SYMBOL(vcrtcm_p_destroy);

int vcrtcm_structures_init()
{
	mutex_init(&pconid_table_mutex);
	return 0;
}

void vcrtcm_structures_destroy()
{
}
