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
#include <vcrtcm/vcrtcm_gpu.h>
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_module.h"

struct pconid_table_entry {
	struct vcrtcm_pcon_info *pcon_info;
};

static struct pconid_table_entry pconid_table[MAX_NUM_PCONIDS];
static struct mutex pconid_table_mutex;

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

int vcrtcm_pcon_table_init()
{
	mutex_init(&pconid_table_mutex);
	return 0;
}
