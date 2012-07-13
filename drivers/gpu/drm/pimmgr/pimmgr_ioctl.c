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

#include <vcrtcm/vcrtcm_pcon.h>
#include <vcrtcm/pimmgr.h>
#include "pimmgr_utils.h"
#include "pimmgr_ioctl.h"
#include "pimmgr_sysfs.h"

long pimmgr_ioctl_core(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct pimmgr_ioctl_args ioctl_args;

	PR_DEBUG("IOCTL entered...\n");

	if (cmd == PIMMGR_IOC_INSTANTIATE) {
		void *ptr = (void *)arg;
		if (!access_ok(VERIFY_READ, arg,
				sizeof(struct pimmgr_ioctl_args)))
			return -EFAULT;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct pimmgr_ioctl_args)))
			return -EFAULT;

		return (long) pimmgr_ioctl_instantiate_pcon(
							ioctl_args.pim_name,
							ioctl_args.hints);
	} else if (cmd == PIMMGR_IOC_DESTROY) {
		uint32_t pconid = (uint32_t) arg;
		return (long) pimmgr_ioctl_destroy_pcon(pconid);
	} else
		PR_INFO("Bad IOCTL\n");

	return 0;
}

/* TODO: Need better errors. */
uint32_t pimmgr_ioctl_instantiate_pcon(char *name, uint32_t hints)
{
	struct pim_info *info;
	struct pcon_instance_info *pcon;
	uint32_t pconid;
	int value = 0;

	PR_INFO("in instantiate pcon...\n");
	info = find_pim_info_by_name(name);

	if (!info) {
		PR_INFO("Invalid pim identifier\n");
		return -1;
	}

	pcon = pimmgr_kzalloc(sizeof(struct pcon_instance_info), GFP_KERNEL);
	if (!pcon) {
		PR_INFO("Could not allocate memory\n");
		return -ENOMEM;
	}

	pcon->pim = info;
	value = info->funcs.instantiate(pcon, info->data, hints);

	if (!value) {
		PR_INFO("No pcons of type %s available...\n", name);
		pimmgr_kfree(pcon);
		return -2;
	}
	pconid = CREATE_PCONID(info->id, pcon->local_id);

	PR_INFO("New pcon created, id %u\n", pconid);

	if (vcrtcm_p_add(pcon->funcs, pcon->props, pconid, pcon->cookie)) {
		PR_INFO("Error registering pcon with vcrtcm\n");
		pimmgr_kfree(pcon);
		return -3;
	}

	vcrtcm_sysfs_add_pcon(pcon);
	list_add_tail(&pcon->instance_list, &pcon_instance_list);
	return pconid;
}

int pimmgr_ioctl_destroy_pcon(uint32_t pconid)
{
	struct pim_info *info;
	struct pcon_instance_info *instance;

	uint32_t pim_id = PCONID_PIMID(pconid);
	uint32_t local_pcon_id = PCONID_LOCALID(pconid);

	PR_INFO("in destroy pcon id %u...\n", pconid);

	info = find_pim_info_by_id(pim_id);
	if (!info)
		return 0;

	instance = find_pcon_instance_info(info, local_pcon_id);
	if (!instance)
		return 0;

	vcrtcm_sysfs_del_pcon(instance);
	vcrtcm_p_del(pconid);
	info->funcs.destroy(local_pcon_id, info->data);
	list_del(&instance->instance_list);
	pimmgr_kfree(instance);

	return 1;
}
