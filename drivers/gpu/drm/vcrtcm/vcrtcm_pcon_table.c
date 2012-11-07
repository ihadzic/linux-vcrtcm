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
#include <vcrtcm/vcrtcm_gpu.h>
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_pim_table.h"

struct pconid_table_entry {
	struct vcrtcm_pcon *pcon;
};

static struct pconid_table_entry pconid_table[MAX_NUM_PCONIDS];
static struct mutex pconid_table_mutex;

struct vcrtcm_pcon *vcrtcm_alloc_pcon(struct vcrtcm_pim *pim)
{
	int k;

	mutex_lock(&pconid_table_mutex);
	for (k = 0; k < MAX_NUM_PCONIDS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];
		if (!entry->pcon) {
			struct vcrtcm_pcon *pcon;
			pcon = vcrtcm_kzalloc_pim(sizeof(struct vcrtcm_pcon),
				GFP_KERNEL, pim);
			if (!pcon) {
				VCRTCM_INFO("allocate of pcon failed\n");
				mutex_unlock(&pconid_table_mutex);
				return NULL;
			}
			atomic_set(&pcon->alloc_cnt, 0);
			pcon->pconid = k;
			pcon->minor = -1;
			pcon->pcon_callbacks_enabled = 1;
			spin_lock_init(&pcon->lock);
			mutex_init(&pcon->mutex);
			entry->pcon = pcon;
			mutex_unlock(&pconid_table_mutex);
			return pcon;
		}
	}
	mutex_unlock(&pconid_table_mutex);
	return NULL;
}

void vcrtcm_dealloc_pcon(int pconid)
{
	struct pconid_table_entry *entry;
	struct vcrtcm_pcon *pcon;

	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS)
		return;
	mutex_lock(&pconid_table_mutex);
	entry = &pconid_table[pconid];
	pcon = entry->pcon;
	if (pcon != NULL) {
		int cnt;

		cnt = atomic_read(&pcon->alloc_cnt);
		VCRTCM_ERROR("ERROR: pcon %d (pim %s) is being destroyed, but it has not freed %d of its allocations\n", pconid, pcon->pim->name, cnt);
		vcrtcm_kfree(pcon);
		entry->pcon = NULL;
	}
	mutex_unlock(&pconid_table_mutex);
}

struct vcrtcm_pcon *vcrtcm_get_pcon(int pconid)
{
	struct vcrtcm_pcon *ret;

	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS) {
		VCRTCM_ERROR("invalid pcon id %d\n", pconid);
		return NULL;
	}
	mutex_lock(&pconid_table_mutex);
	ret = pconid_table[pconid].pcon;
	mutex_unlock(&pconid_table_mutex);
	if (!ret)
		VCRTCM_ERROR("no pcon %d\n", pconid);
	return ret;
}

int vcrtcm_init_pcon_table()
{
	mutex_init(&pconid_table_mutex);
	return 0;
}
