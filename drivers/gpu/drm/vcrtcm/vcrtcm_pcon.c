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

void vcrtcm_destroy_pcon(struct vcrtcm_pcon *pcon)
{
	pcon->vblank_period_jiffies = 0;
	pcon->fps = 0;
	cancel_delayed_work_sync(&pcon->vblank_work);
	list_del(&pcon->pcons_in_pim_list);
	vcrtcm_sysfs_del_pcon(pcon);
	vcrtcm_dealloc_pcon(pcon);
}

void vcrtcm_prepare_detach(struct vcrtcm_pcon *pcon)
{
	VCRTCM_INFO("detaching pcon %i\n", pcon->pconid);
	pcon->vblank_period_jiffies = 0;
	pcon->fps = 0;
	cancel_delayed_work_sync(&pcon->vblank_work);
}
