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
#include <vcrtcm/vcrtcm_alloc.h>
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_vblank.h"
#include "vcrtcm_pcon.h"

struct pconid_table_entry {
	struct mutex mutex;
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
	int in_mutex;
	pid_t mutex_owner;
	spinlock_t mutex_owner_spinlock;
#endif
	struct vcrtcm_pcon *pcon;
};

static struct pconid_table_entry pconid_table[MAX_NUM_PCONIDS];
static DEFINE_MUTEX(pconid_table_mutex);

void init_pcon_table(void)
{
	int k;

	for (k = 0; k < MAX_NUM_PCONIDS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];

		mutex_init(&entry->mutex);
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
		entry->in_mutex = 0;
		spin_lock_init(&entry->mutex_owner_spinlock);
#endif
	}
}

struct vcrtcm_pcon *vcrtcm_alloc_pcon(struct vcrtcm_pim *pim)
{
	int k;

	mutex_lock(&pconid_table_mutex);
	for (k = 0; k < MAX_NUM_PCONIDS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];
		if (!entry->pcon) {
			struct vcrtcm_pcon *pcon;
			pcon = vcrtcm_kzalloc(sizeof(struct vcrtcm_pcon),
				GFP_KERNEL, VCRTCM_OWNER_VCRTCM);
			if (!pcon) {
				VCRTCM_INFO("allocate of pcon failed\n");
				mutex_unlock(&pconid_table_mutex);
				return NULL;
			}
			pcon->pconid = k;
			pcon->minor = -1;
			pcon->log_alloc_bugs = 1;
			pcon->pcon_callbacks_enabled = 1;
			spin_lock_init(&pcon->page_flip_spinlock);
			INIT_DELAYED_WORK(&pcon->vblank_work,
				vcrtcm_vblank_work_fcn);
			entry->pcon = pcon;
			mutex_unlock(&pconid_table_mutex);
			return pcon;
		}
	}
	mutex_unlock(&pconid_table_mutex);
	return NULL;
}

/*
* NB: the caller is responsible for calling vcrtcm_kfree() on
* the pcon.  This is to permit the caller to release the pcon's
* mutex and spin lock before freeing the struct.
*/
void vcrtcm_dealloc_pcon(struct vcrtcm_pcon *pcon)
{
	struct pconid_table_entry *entry;

	mutex_lock(&pconid_table_mutex);
	entry = &pconid_table[pcon->pconid];
	pcon = entry->pcon;
	if (pcon != NULL) {
		int cnt;
		int page_cnt;

		cnt = pcon->alloc_cnt;
		page_cnt = pcon->page_alloc_cnt;
		if (cnt != 0 || page_cnt != 0)
			VCRTCM_ERROR("pcon %d (pim %s) is being destroyed, "
				"but it has not freed %d of its allocations, "
				"%d of which were page allocations\n",
				pcon->pconid, pcon->pim->name, cnt, page_cnt);
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

int vcrtcm_lock_pconid(int pconid)
{
	struct pconid_table_entry *entry;

	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS) {
		VCRTCM_ERROR("invalid pcon id %d\n", pconid);
		return -EINVAL;
	}
	entry = &pconid_table[pconid];
	mutex_lock(&entry->mutex);
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
	{
		unsigned long flags;
		spin_lock_irqsave(&entry->mutex_owner_spinlock, flags);
		entry->in_mutex = 1;
		entry->mutex_owner = current->pid;
		spin_unlock_irqrestore(&entry->mutex_owner_spinlock, flags);
	}
#endif
	return 0;
}

int vcrtcm_unlock_pconid(int pconid)
{
	struct pconid_table_entry *entry;

	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS) {
		VCRTCM_ERROR("invalid pcon id %d\n", pconid);
		return -EINVAL;
	}
	entry = &pconid_table[pconid];
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
	BUG_ON(!entry->in_mutex);
	{
		unsigned long flags;
		spin_lock_irqsave(&entry->mutex_owner_spinlock, flags);
		entry->in_mutex = 0;
		spin_unlock_irqrestore(&entry->mutex_owner_spinlock, flags);
	}
#endif
	mutex_unlock(&entry->mutex);
	return 0;
}

#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
void
vcrtcm_check_mutex(const char *func, struct vcrtcm_pcon *pcon)
{
	unsigned long flags;
	int in_mutex;
	pid_t mutex_owner;
	struct pconid_table_entry *entry = &pconid_table[pcon->pconid];

	spin_lock_irqsave(&entry->mutex_owner_spinlock, flags);
	in_mutex = entry->in_mutex;
	mutex_owner = entry->mutex_owner;
	spin_unlock_irqrestore(&entry->mutex_owner_spinlock, flags);
	if (!in_mutex)
		VCRTCM_ERROR("mutex violation: not in mutex: %s\n", func);
	else if (mutex_owner != current->pid)
		VCRTCM_ERROR("mutex violation: not owner: %s\n", func);
}
#endif
