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
#include <vcrtcm/vcrtcm_utils.h>
#include "vcrtcm_pcon.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_sysfs_priv.h"

void vcrtcm_lock_pcon(struct vcrtcm_pcon *pcon)
{
	mutex_lock(&pcon->mutex);
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
	{
		unsigned long flags;
		spin_lock_irqsave(&pcon->mutex_owner_spinlock, flags);
		pcon->in_mutex = 1;
		pcon->mutex_owner = current->pid;
		spin_unlock_irqrestore(&pcon->mutex_owner_spinlock, flags);
	}
#endif
}

void vcrtcm_unlock_pcon(struct vcrtcm_pcon *pcon)
{
#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
	BUG_ON(!pcon->in_mutex);
	{
		unsigned long flags;
		spin_lock_irqsave(&pcon->mutex_owner_spinlock, flags);
		pcon->in_mutex = 0;
		spin_unlock_irqrestore(&pcon->mutex_owner_spinlock, flags);
	}
#endif
	mutex_unlock(&pcon->mutex);
}

int vcrtcm_lock_mutex(int pconid)
{
	struct vcrtcm_pcon *pcon;

	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	vcrtcm_lock_pcon(pcon);
	return 0;
}

int vcrtcm_unlock_mutex(int pconid)
{
	struct vcrtcm_pcon *pcon;

	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	vcrtcm_unlock_pcon(pcon);
	return 0;
}

#ifdef CONFIG_DRM_VCRTCM_DEBUG_MUTEXES
void
vcrtcm_check_mutex(const char *func, struct vcrtcm_pcon *pcon)
{
	unsigned long flags;
	int in_mutex;
	pid_t mutex_owner;

	spin_lock_irqsave(&pcon->mutex_owner_spinlock, flags);
	in_mutex = pcon->in_mutex;
	mutex_owner = pcon->mutex_owner;
	spin_unlock_irqrestore(&pcon->mutex_owner_spinlock, flags);
	if (!in_mutex)
		VCRTCM_ERROR("mutex violation: not in mutex: %s\n", func);
	else if (mutex_owner != current->pid)
		VCRTCM_ERROR("mutex violation: not owner: %s\n", func);
}
#endif

void vcrtcm_destroy_pcon(struct vcrtcm_pcon *pcon)
{
	cancel_delayed_work_sync(&pcon->vblank_work);
	list_del(&pcon->pcons_in_pim_list);
	vcrtcm_sysfs_del_pcon(pcon);
	vcrtcm_dealloc_pcon(pcon->pconid);
}
