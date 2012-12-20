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
#include <vcrtcm/vcrtcm_utils.h>
#include "vcrtcm_pcon.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_sysfs_priv.h"

void vcrtcm_destroy_pcon(struct vcrtcm_pcon *pcon)
{
	unsigned long flags;
	spinlock_t *pcon_spinlock;
	int pconid;

	pconid = pcon->pconid;
	pcon->vblank_period_jiffies = 0;
	pcon->fps = 0;
	cancel_delayed_work_sync(&pcon->vblank_work);
	list_del(&pcon->pcons_in_pim_list);
	vcrtcm_sysfs_del_pcon(pcon);
	pcon_spinlock = vcrtcm_get_pconid_spinlock(pconid);
	BUG_ON(!pcon_spinlock);
	spin_lock_irqsave(pcon_spinlock, flags);
	vcrtcm_set_spinlock_owner(pconid);
	vcrtcm_dealloc_pcon(pcon);
	vcrtcm_clear_spinlock_owner(pconid);
	spin_unlock_irqrestore(pcon_spinlock, flags);
}

void vcrtcm_prepare_detach(struct vcrtcm_pcon *pcon)
{
	VCRTCM_INFO("detaching pcon %i\n", pcon->pconid);
	pcon->vblank_period_jiffies = 0;
	pcon->fps = 0;
	cancel_delayed_work_sync(&pcon->vblank_work);

	/*
	 * push-mode pims must wait_fb() before detaching,
	 * so do it for them to make sure it gets done
	 */
	if (pcon->xfer_mode == VCRTCM_PEER_PUSH ||
	    pcon->xfer_mode == VCRTCM_PUSH_PULL)
		vcrtcm_p_wait_fb(pcon->pconid);
}

void vcrtcm_set_crtc(struct vcrtcm_pcon *pcon, struct drm_crtc *crtc)
{
	unsigned long flags;
	spinlock_t *pcon_spinlock;

	/*
	 * the setting of the drm_crtc field has to be protected with
	 * the spin lock because vcrtcm_p_emulate_vblank() examines
	 * that field while not holding the pcon's mutex
	 */
	pcon_spinlock = vcrtcm_get_pconid_spinlock(pcon->pconid);
	BUG_ON(!pcon_spinlock);
	spin_lock_irqsave(pcon_spinlock, flags);
	vcrtcm_set_spinlock_owner(pcon->pconid);
	pcon->drm_crtc = crtc;
	vcrtcm_clear_spinlock_owner(pcon->pconid);
	spin_unlock_irqrestore(pcon_spinlock, flags);
}
