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
#include <vcrtcm/vcrtcm_alloc.h>
#include "vcrtcm_pim_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_utils_priv.h"

static int next_pimid;
static LIST_HEAD(pim_list);
static DEFINE_MUTEX(pim_list_mutex);

struct vcrtcm_pim *vcrtcm_create_pim(char *pim_name,
	struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim *pim;

	if (!pim_name || !funcs)
		return NULL;
	mutex_lock(&pim_list_mutex);
	list_for_each_entry(pim, &pim_list, pim_list) {
		if (strcmp(pim->name, pim_name) == 0) {
			VCRTCM_ERROR("pim %s already exists; unregister it or use a different name\n", pim_name);
			mutex_unlock(&pim_list_mutex);
			return ERR_PTR(-EINVAL);
		}
	}
	pim = (struct vcrtcm_pim *)vcrtcm_kzalloc(
		sizeof(struct vcrtcm_pim), GFP_KERNEL, VCRTCM_OWNER_VCRTCM);
	if (!pim) {
		VCRTCM_ERROR("cannot allocate memory for pim %s\n", pim_name);
		mutex_unlock(&pim_list_mutex);
		return ERR_PTR(-ENOMEM);
	}
	strncpy(pim->name, pim_name, PIM_NAME_MAXLEN);
	memcpy(&pim->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
	INIT_LIST_HEAD(&pim->pcons_in_pim_list);
	memset(&pim->kobj, 0, sizeof(struct kobject));
	pim->id = next_pimid;
	pim->log_alloc_bugs = 1;
	next_pimid++;
	list_add_tail(&pim->pim_list, &pim_list);
	mutex_unlock(&pim_list_mutex);
	return pim;
}

struct vcrtcm_pim *vcrtcm_get_pim(int pimid)
{
	struct vcrtcm_pim *pim;

	mutex_lock(&pim_list_mutex);
	list_for_each_entry(pim, &pim_list, pim_list) {
		if (pim->id == pimid) {
			mutex_unlock(&pim_list_mutex);
			return pim;
		}
	}
	mutex_unlock(&pim_list_mutex);
	VCRTCM_ERROR("pim %d is not registered\n", pimid);
	return NULL;
}

void vcrtcm_destroy_pim(struct vcrtcm_pim *pim)
{
	int cnt;
	int page_cnt;

	mutex_lock(&pim_list_mutex);
	cnt = pim->alloc_cnt;
	page_cnt = pim->page_alloc_cnt;
	if (cnt != 0 || page_cnt != 0)
		VCRTCM_ERROR("pim %s is being destroyed, "
			"but it has not freed %d of its allocations, "
			"%d of which were page allocations\n",
			pim->name, cnt, page_cnt);
	list_del(&pim->pim_list);
	vcrtcm_kfree(pim);
	mutex_unlock(&pim_list_mutex);
}

void vcrtcm_free_pims()
{
	struct vcrtcm_pim *pim, *pim_tmp;

	mutex_lock(&pim_list_mutex);
	list_for_each_entry_safe(pim, pim_tmp, &pim_list, pim_list) {
		list_del(&pim->pim_list);
		vcrtcm_kfree(pim);
	}
	mutex_unlock(&pim_list_mutex);
}
