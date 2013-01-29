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
#include "vcrtcm_vblank.h"

void vcrtcm_destroy_pcon(struct vcrtcm_pcon *pcon)
{
	unsigned long flags;
	spinlock_t *pcon_spinlock;
	int pconid;

	BUG_ON(pcon->vblank_period_jiffies != 0);
	BUG_ON(pcon->fps != 0);
	/*
	 * unlike in vcrtcm_set_fps() (see below), here we
	 * call cancel_delayed_work_sync(), because we must
	 * ensure that the vblank worker is really canceled
	 * before freeing the pcon.  Note that unlike in the
	 * case of vcrtcm_set_fps(), calling cancel_delayed_work_sync()
	 * here cannot lead to deadlock, because the current
	 * function is never called when the pcon lock is held.
	 */
	cancel_delayed_work_sync(&pcon->vblank_work);
	list_del(&pcon->pcons_in_pim_list);
	vcrtcm_sysfs_del_pcon(pcon);
	pconid = pcon->pconid;
	pcon_spinlock = vcrtcm_get_pconid_spinlock(pconid);
	BUG_ON(!pcon_spinlock);
	spin_lock_irqsave(pcon_spinlock, flags);
	vcrtcm_set_spinlock_owner(pconid);
	vcrtcm_dealloc_pcon(pcon);
	vcrtcm_clear_spinlock_owner(pconid);
	spin_unlock_irqrestore(pcon_spinlock, flags);
}

/*
 * push-mode pims must wait_fb() before doing certain things,
 * so do it for them to make sure it gets done
 */
void vcrtcm_wait_if_necessary(struct vcrtcm_pcon *pcon)
{
	if (pcon->xfer_mode == VCRTCM_PEER_PUSH ||
	    pcon->xfer_mode == VCRTCM_PUSH_PULL)
		vcrtcm_p_wait_fb(pcon->pconid);
}

void vcrtcm_set_fps(struct vcrtcm_pcon *pcon, int fps)
{
	if (fps == pcon->fps)
		return;
	if (fps <= 0) {
		pcon->fps = 0;
		pcon->vblank_period_jiffies = 0;
		/*
		 * do *not* call cancel_delayed_work_sync(), because
		 * if the vblank worker is sitting waiting to take
		 * the pcon lock (which we are currently holding)
		 * a deadlock would result.  Note that calling the
		 * non-syncing cancel_delayed_work() means that if
		 * the vblank worker *is* sitting waiting to take
		 * the pcon mutex, then the vblank work function
		 * will resume executing sometime after this set-fps
		 * operation completes.  That is ok, though, because
		 * the vblank work function checks whether
		 * vblank_period_jiffies is 0 and if so does nothing.
		 */
		cancel_delayed_work(&pcon->vblank_work);
		VCRTCM_INFO("transmission disabled on pcon 0x%08x\n",
			pcon->pconid);
	} else {
		unsigned long now;
		int old_fps = pcon->fps;

		pcon->fps = fps;
		pcon->vblank_period_jiffies = HZ/fps;
		now = jiffies;
		pcon->last_vblank_jiffies = now;
		pcon->next_vblank_jiffies = now + pcon->vblank_period_jiffies;
		if (old_fps == 0) {
			vcrtcm_schedule_vblank(pcon);
			VCRTCM_INFO("transmission enabled on pcon 0x%08x (%d f/s)\n",
				pcon->pconid, fps);
		}
	}
	vcrtcm_wait_if_necessary(pcon);
}

void vcrtcm_detach(struct vcrtcm_pcon *pcon)
{
	BUG_ON(!pcon->conn);
	memset(&pcon->gpu_funcs, 0, sizeof(struct vcrtcm_g_pcon_funcs));
	pcon->attach_minor = -1;
	vcrtcm_set_crtc(pcon, NULL);
	vcrtcm_sysfs_detach(pcon);
	atomic_dec(&pcon->conn->num_attached_pcons);
	pcon->conn = NULL;
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
