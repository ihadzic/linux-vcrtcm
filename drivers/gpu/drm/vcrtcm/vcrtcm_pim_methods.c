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
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include "vcrtcm_pim_methods.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_sysfs_priv.h"

int vcrtcm_pim_register(char *pim_name, struct vcrtcm_pim_funcs *funcs)
{
	struct vcrtcm_pim_info *pim_info;

	VCRTCM_INFO("Registering PIM %s, funcs at %p\n", pim_name, funcs);

	pim_info = vcrtcm_find_pim_info_by_name(pim_name);
	if (pim_info) {
		memcpy(&pim_info->funcs, funcs, sizeof(struct vcrtcm_pim_funcs));
		return 1;
	}

	pim_info = vcrtcm_create_pim_info(pim_name, funcs);
	if (!pim_info)
		return -ENOMEM;

	vcrtcm_add_pim_info(pim_info);
	vcrtcm_sysfs_add_pim(pim_info);

	return 1;
}
EXPORT_SYMBOL(vcrtcm_pim_register);

void vcrtcm_pim_unregister(char *pim_name)
{
	struct vcrtcm_pim_info *pim_info;
	struct vcrtcm_pcon_info *pcon_info, *tmp;

	pim_info = vcrtcm_find_pim_info_by_name(pim_name);
	if (!pim_info)
		return;
	VCRTCM_INFO("unregistering PIM %s\n", pim_name);
	list_for_each_entry_safe(pcon_info, tmp,
			&pim_info->pcons_in_pim_list, pcons_in_pim_list) {
		vcrtcm_p_destroy(pcon_info->pconid);
	}
	vcrtcm_remove_pim_info(pim_info);
	vcrtcm_sysfs_del_pim(pim_info);
	vcrtcm_destroy_pim_info(pim_info);
}
EXPORT_SYMBOL(vcrtcm_pim_unregister);
