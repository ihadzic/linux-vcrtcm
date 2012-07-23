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
	long result = 0;

	PR_DEBUG("IOCTL entered...\n");

	if (!access_ok(VERIFY_READ, arg, sizeof(struct pimmgr_ioctl_args)))
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE, arg, sizeof(struct pimmgr_ioctl_args)))
		return -EFAULT;

	if (cmd == PIMMGR_IOC_INSTANTIATE) {
		void *ptr = (void *)arg;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct pimmgr_ioctl_args)))
			return -EFAULT;

		result = pimmgr_ioctl_instantiate_pcon(
						ioctl_args.arg1.pim_name,
						ioctl_args.arg2.hints,
						&ioctl_args.result1.pconid);
		if (result)
			return result;

		if (copy_to_user(ptr, &ioctl_args,
					sizeof(struct pimmgr_ioctl_args)))
			return -EFAULT;

		return 0;
	}

	else if (cmd == PIMMGR_IOC_DESTROY) {
		void *ptr = (void *)arg;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct pimmgr_ioctl_args)))
			return -EFAULT;

		result = pimmgr_ioctl_destroy_pcon(ioctl_args.arg1.pconid);

		if (result)
			return result;

		return 0;
	} else
		PR_INFO("Bad IOCTL\n");

	return 0;
}

/* TODO: Need better errors. */
uint32_t pimmgr_ioctl_instantiate_pcon(char *name, uint32_t hints,
							uint32_t *pconid)
{
	struct pim_info *info;
	struct pcon_instance_info *pcon;
	uint32_t new_pconid;
	int value = 0;

	PR_INFO("in instantiate pcon...\n");
	info = find_pim_info_by_name(name);

	if (!info) {
		PR_INFO("Invalid pim identifier\n");
		return PIMMGR_ERR_INVALID_PIM;
	}

	pcon = pimmgr_kzalloc(sizeof(struct pcon_instance_info), GFP_KERNEL);
	if (!pcon) {
		PR_INFO("Could not allocate memory\n");
		return PIMMGR_ERR_NOMEM;
	}

	pcon->pim = info;
	value = info->funcs.instantiate(pcon, info->data, hints);

	if (!value) {
		PR_INFO("No pcons of type %s available...\n", name);
		pimmgr_kfree(pcon);
		return PIMMGR_ERR_NOT_AVAILABLE;
	}
	new_pconid = CREATE_PCONID(info->id, pcon->local_id);

	PR_INFO("New pcon created, id %u\n", new_pconid);

	if (vcrtcm_p_add(pcon->funcs, pcon->props, new_pconid, pcon->cookie)) {
		PR_INFO("Error registering pcon with vcrtcm\n");
		pimmgr_kfree(pcon);
		return PIMMGR_ERR_CANNOT_REGISTER;
	}

	vcrtcm_sysfs_add_pcon(pcon);
	list_add_tail(&pcon->instance_list, &pcon_instance_list);
	*pconid = new_pconid;
	return 0;
}

int pimmgr_ioctl_destroy_pcon(uint32_t pconid)
{
	struct pim_info *info;
	struct pcon_instance_info *instance;
	int r = 0;

	uint32_t pim_id = PCONID_PIMID(pconid);
	uint32_t local_pcon_id = PCONID_LOCALID(pconid);

	PR_INFO("in destroy pcon id %u...\n", pconid);

	if (!PCONID_VALID(pconid))
		return PIMMGR_ERR_INVALID_PCON;

	info = find_pim_info_by_id(pim_id);
	if (!info)
		return PIMMGR_ERR_INVALID_PCON;

	instance = find_pcon_instance_info(info, local_pcon_id);
	if (!instance)
		return PIMMGR_ERR_INVALID_PCON;

	r = vcrtcm_p_del(pconid);
	if (r)
		return PIMMGR_ERR_CANNOT_DESTROY;

	vcrtcm_sysfs_del_pcon(instance);
	info->funcs.destroy(local_pcon_id, info->data);
	list_del(&instance->instance_list);
	pimmgr_kfree(instance);
	return 0;
}
