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
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include "vcrtcm_pim_methods.h"
#include "vcrtcm_pcon_methods.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_sysfs_priv.h"

int vcrtcm_pim_register(char *pim_name,
	struct vcrtcm_pim_funcs *funcs, int *pimid)
{
	struct vcrtcm_pim *pim;

	VCRTCM_INFO("registering PIM %s\n", pim_name);
	pim = vcrtcm_create_pim(pim_name, funcs);
	if (IS_ERR(pim))
		return PTR_ERR(pim);
	*pimid = pim->id;
	vcrtcm_sysfs_add_pim(pim);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_register);

void vcrtcm_pim_unregister(int pimid)
{
	struct vcrtcm_pim *pim;
	struct vcrtcm_pcon *pcon, *tmp;

	pim = vcrtcm_find_pim(pimid);
	if (!pim)
		return;
	VCRTCM_INFO("unregistering %s\n", pim->name);
	list_for_each_entry_safe(pcon, tmp,
			&pim->pcons_in_pim_list, pcons_in_pim_list)
		do_vcrtcm_p_destroy(pcon, 0);
	vcrtcm_sysfs_del_pim(pim);
	vcrtcm_destroy_pim(pim);
	VCRTCM_INFO("finished unregistering pim\n");
}
EXPORT_SYMBOL(vcrtcm_pim_unregister);
