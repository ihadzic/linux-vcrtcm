/*
 * Copyright (C) 2012 Alcatel-Lucent, Inc.
 * Authors:
 *      Ilija Hadzic <ihadzic@research.bell-labs.com>
 *      Martin Carroll <martin.carroll@research.bell-labs.com>
 *      William Katsak <wkatsak@cs.rutgers.edu>
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

#define MAX_NUM_PCONIDS 1024

struct pconid_table_entry {
	spinlock_t spinlock;
	struct mutex mutex;
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
	int in_mutex;
	pid_t mutex_owner;
	spinlock_t mutex_owner_spinlock;
#endif
	struct vcrtcm_pcon *pcon;
};

static struct pconid_table_entry pconid_table[MAX_NUM_PCONIDS];
static int num_pcons;
static DEFINE_SPINLOCK(pconid_table_spinlock);

static struct pconid_table_entry *pconid2entry(int pconid)
{
	if (pconid < 0 || pconid >= MAX_NUM_PCONIDS) {
		VCRTCM_ERROR("bad pcon id %d\n", pconid);
		dump_stack();
		return NULL;
	}
	return &pconid_table[pconid];
}

void init_pcon_table(void)
{
	int k;

	for (k = 0; k < MAX_NUM_PCONIDS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];

		spin_lock_init(&entry->spinlock);
		mutex_init(&entry->mutex);
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
		entry->in_mutex = 0;
		spin_lock_init(&entry->mutex_owner_spinlock);
#endif
	}
}

int vcrtcm_num_pcons(void)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&pconid_table_spinlock, flags);
	ret = num_pcons;
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
	return ret;
}

struct vcrtcm_pcon *vcrtcm_alloc_pcon(struct vcrtcm_pim *pim)
{
	int k;
	unsigned long flags;
	struct vcrtcm_pcon *pcon;

	pcon = vcrtcm_kzalloc(sizeof(struct vcrtcm_pcon),
		GFP_KERNEL, VCRTCM_OWNER_VCRTCM);
	if (!pcon) {
		VCRTCM_ERROR("allocate of pcon failed\n");
		return NULL;
	}
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	for (k = 0; k < MAX_NUM_PCONIDS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];
		if (!entry->pcon) {
			pcon->pconid = k;
			pcon->minor = -1;
			pcon->log_alloc_bugs = 1;
			pcon->pcon_callbacks_enabled = 1;
			INIT_DELAYED_WORK(&pcon->vblank_work,
				vcrtcm_vblank_work_fcn);
			entry->pcon = pcon;
			++num_pcons;
			spin_unlock_irqrestore(&pconid_table_spinlock, flags);
			return pcon;
		}
	}
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
	vcrtcm_kfree(pcon);
	VCRTCM_ERROR("no free pconid\n");
	return NULL;
}

void vcrtcm_dealloc_pcon(struct vcrtcm_pcon *pcon)
{
	struct pconid_table_entry *entry;
	unsigned long flags;
	int cnt;
	int page_cnt;

	if (!pcon)
		return;
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	entry = pconid2entry(pcon->pconid);
	if (!entry)
		return;
	BUG_ON(pcon != entry->pcon);
	cnt = pcon->alloc_cnt;
	page_cnt = pcon->page_alloc_cnt;
	if (cnt != 0 || page_cnt != 0)
		VCRTCM_ERROR("pcon %d (pim %s) is being destroyed, "
			"but it has not freed %d of its allocations, "
			"%d of which were page allocations\n",
			pcon->pconid, pcon->pim->name, cnt, page_cnt);
	entry->pcon = NULL;
	--num_pcons;
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
	vcrtcm_kfree(pcon);
}

struct vcrtcm_pcon *vcrtcm_get_pcon(int pconid)
{
	struct pconid_table_entry *entry;
	struct vcrtcm_pcon *ret;
	unsigned long flags;

	entry = pconid2entry(pconid);
	if (!entry)
		return NULL;
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	ret = entry->pcon;
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
	if (!ret)
		VCRTCM_ERROR("no pcon %d\n", pconid);
	return ret;
}

spinlock_t *vcrtcm_get_pconid_spinlock(int pconid)
{
	struct pconid_table_entry *entry;

	entry = pconid2entry(pconid);
	if (!entry)
		return NULL;
	return &entry->spinlock;
}

int vcrtcm_lock_pconid(int pconid)
{
	struct pconid_table_entry *entry;

	entry = pconid2entry(pconid);
	if (!entry)
		return -EINVAL;
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

	entry = pconid2entry(pconid);
	if (!entry)
		return -EINVAL;
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
vcrtcm_check_mutex(const char *func, int pconid)
{
	unsigned long flags;
	int in_mutex;
	pid_t mutex_owner;
	struct pconid_table_entry *entry;

	entry = pconid2entry(pconid);
	if (!entry)
		return;
	spin_lock_irqsave(&entry->mutex_owner_spinlock, flags);
	in_mutex = entry->in_mutex;
	mutex_owner = entry->mutex_owner;
	spin_unlock_irqrestore(&entry->mutex_owner_spinlock, flags);
	if (!in_mutex) {
		VCRTCM_ERROR("mutex violation: not in mutex: %s\n", func);
		dump_stack();
	} else if (mutex_owner != current->pid) {
		VCRTCM_ERROR("mutex violation: pcon 0x%08x locked by other: %s\n",
			pconid, func);
		dump_stack();
	}
}
#endif
