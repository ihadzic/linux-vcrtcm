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

#define MAX_NUM_PCONS 1024

/*
 * internal pcon ids must not use the VCRTCM_OWNER_BITMASK bits,
 * because if any of those bits were set, then allocations
 * on behalf of the pcon would incorrectly appear to be on
 * behalf of something other than the pcon.
 */
#define PCONID_MSW_MAX (~VCRTCM_OWNER_BITMASK >> 16)
#define MAKE_PCONID(msw, extid) (((u32)(msw) << 16) | (u32)(extid))

struct pconid_table_entry {
	spinlock_t spinlock;
	pid_t spinlock_owner;
	struct mutex mutex;
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
	int in_mutex;
	pid_t mutex_owner;
	spinlock_t mutex_owner_spinlock;
#endif
	struct vcrtcm_pcon *pcon;
};

static struct pconid_table_entry pconid_table[MAX_NUM_PCONS];
static int num_pcons;
static u16 next_pconid_msw;
static DEFINE_SPINLOCK(pconid_table_spinlock);

static struct pconid_table_entry *extid2entry(int extid)
{
	if (extid < 0 || extid >= MAX_NUM_PCONS) {
		VCRTCM_ERROR("bad extid %08x\n", extid);
		dump_stack();
		return NULL;
	}
	return &pconid_table[extid];
}

static struct pconid_table_entry *pconid2entry(int pconid)
{
	return extid2entry(PCONID_EXTID(pconid));
}

void vcrtcm_init_pcon_table(void)
{
	int k;

	for (k = 0; k < MAX_NUM_PCONS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];

		entry->spinlock_owner = -1;
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
	for (k = 0; k < MAX_NUM_PCONS; ++k) {
		struct pconid_table_entry *entry = &pconid_table[k];
		if (!entry->pcon) {
			pcon->pconid = MAKE_PCONID(next_pconid_msw, k);
			++next_pconid_msw;
			if (next_pconid_msw > PCONID_MSW_MAX)
				next_pconid_msw = 0;
			pcon->minor = -1;
			pcon->attach_minor = -1;
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
	entry = pconid2entry(pcon->pconid);
	if (!entry)
		return;
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	BUG_ON(pcon != entry->pcon);
	cnt = pcon->alloc_cnt;
	page_cnt = pcon->page_alloc_cnt;
	if (cnt != 0 || page_cnt != 0)
		VCRTCM_ERROR("pcon %08x (pim %s) is being destroyed, "
			"but it has not freed %d of its allocations, "
			"%d of which were page allocations\n",
			pcon->pconid, pcon->pim->name, cnt, page_cnt);
	entry->pcon = NULL;
	--num_pcons;
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
	vcrtcm_kfree(pcon);
}

struct vcrtcm_pcon *vcrtcm_get_pcon_extid(int extid)
{
	struct pconid_table_entry *entry;
	struct vcrtcm_pcon *ret;
	unsigned long flags;

	entry = extid2entry(extid);
	if (!entry)
		return NULL;
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	ret = entry->pcon;
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
	if (!ret) {
		VCRTCM_ERROR("no pcon w/ extid %08x\n", extid);
		return NULL;
	}
	return ret;
}

struct vcrtcm_pcon *vcrtcm_get_pcon(int pconid)
{
	struct vcrtcm_pcon *pcon;

	pcon = vcrtcm_get_pcon_extid(PCONID_EXTID(pconid));
	if (!pcon)
		return NULL;
	if (pcon->pconid != pconid) {
		VCRTCM_ERROR("stale pcon id: %08x vs %08x\n",
			pcon->pconid, pconid);
		return NULL;
	}
	return pcon;
}

spinlock_t *vcrtcm_get_pconid_spinlock(int pconid)
{
	struct pconid_table_entry *entry;

	entry = pconid2entry(pconid);
	if (!entry)
		return NULL;
	return &entry->spinlock;
}

int vcrtcm_lock_extid(int extid)
{
	struct pconid_table_entry *entry;

	entry = extid2entry(extid);
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

int vcrtcm_lock_pconid(int pconid)
{
	return vcrtcm_lock_extid(PCONID_EXTID(pconid));
}

int vcrtcm_unlock_extid(int extid)
{
	struct pconid_table_entry *entry;

	entry = extid2entry(extid);
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

int vcrtcm_unlock_pconid(int pconid)
{
	return vcrtcm_unlock_extid(PCONID_EXTID(pconid));
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
	BUG_ON(!in_mutex);
	BUG_ON(mutex_owner != current->pid);
}
#endif

void vcrtcm_set_spinlock_owner(int pconid)
{
	struct pconid_table_entry *entry;
	unsigned long flags;

	entry = pconid2entry(pconid);
	if (!entry)
		return;
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	entry->spinlock_owner = current->pid;
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
}

void vcrtcm_clear_spinlock_owner(int pconid)
{
	struct pconid_table_entry *entry;
	unsigned long flags;

	entry = pconid2entry(pconid);
	if (!entry)
		return;
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	entry->spinlock_owner = -1;
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
}

int vcrtcm_current_pid_is_spinlock_owner(int pconid)
{
	struct pconid_table_entry *entry;
	unsigned long flags;
	int ret;

	entry = pconid2entry(pconid);
	BUG_ON(!entry);
	spin_lock_irqsave(&pconid_table_spinlock, flags);
	ret = (entry->spinlock_owner == current->pid);
	spin_unlock_irqrestore(&pconid_table_spinlock, flags);
	return ret;
}
