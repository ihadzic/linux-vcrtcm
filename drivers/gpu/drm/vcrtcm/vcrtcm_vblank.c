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
#include <vcrtcm/vcrtcm_gpu.h>
#include "vcrtcm_vblank.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pcon.h"

void
vcrtcm_vblank_work_fcn(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct vcrtcm_pcon *pcon =
		container_of(delayed_work, struct vcrtcm_pcon, vblank_work);
	int next_vblank_delay;
	unsigned long now;

	vcrtcm_lock_pconid(pcon->pconid);
	if (!pcon->drm_crtc || pcon->being_destroyed || pcon->vblank_period_jiffies == 0) {
		vcrtcm_unlock_pconid(pcon->pconid);
		return;
	}
	now = jiffies;
	if (time_after_eq(now + pcon->vblank_slack_jiffies,
		pcon->next_vblank_jiffies)) {
		pcon->next_vblank_jiffies += pcon->vblank_period_jiffies;
		if (pcon->pcon_funcs.vblank &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled)
			pcon->pcon_funcs.vblank(pcon->pconid,
				pcon->pcon_cookie);
	}
	next_vblank_delay = pcon->next_vblank_jiffies - (int)now;
	if (next_vblank_delay <= pcon->vblank_slack_jiffies)
		next_vblank_delay = 0;
	schedule_delayed_work(&pcon->vblank_work, next_vblank_delay);
	vcrtcm_unlock_pconid(pcon->pconid);
}
