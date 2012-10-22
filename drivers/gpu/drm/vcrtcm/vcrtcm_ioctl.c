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
#include <vcrtcm/vcrtcm_utils.h>
#include "vcrtcm_private.h"
#include "vcrtcm_ioctl.h"
#include "vcrtcm_sysfs.h"

/* TODO: Need better errors. */
long vcrtcm_ioctl_instantiate_pcon(int pimid, uint32_t hints, int *pconid)
{
	struct vcrtcm_pim_info *info;
	struct pimmgr_pcon_info *pcon;
	int new_pconid;
	int value = 0;

	VCRTCM_INFO("in instantiate pcon...\n");
	new_pconid = alloc_pconid();
	if (new_pconid < 0) {
		VCRTCM_ERROR("No pconids available...");
		return -EMFILE;
	}

	info = find_pim_info_by_id(pimid);

	if (!info) {
		VCRTCM_INFO("Invalid pimid\n");
		dealloc_pconid(new_pconid);
		return -EINVAL;
	}

	pcon = vcrtcm_kzalloc(sizeof(struct pimmgr_pcon_info), GFP_KERNEL,
					&vcrtcm_kmalloc_track);
	if (!pcon) {
		VCRTCM_INFO("Could not allocate memory\n");
		dealloc_pconid(new_pconid);
		return -ENOMEM;
	}

	pcon->pconid = new_pconid;
	pcon->pim = info;
	pcon->minor = -1;
	if (info->funcs.instantiate)
		value = info->funcs.instantiate(pcon, hints);
	else
		VCRTCM_INFO("No instantiate function...\n");

	if (!value) {
		VCRTCM_INFO("No pcons of type %s available...\n", info->name);
		vcrtcm_kfree(pcon, &vcrtcm_kmalloc_track);
		dealloc_pconid(new_pconid);
		return -ENODEV;
	}
	value = pconid_set_mapping(new_pconid, info->id, pcon->local_pconid);

	VCRTCM_INFO("New pcon created, id %i\n", new_pconid);

	if (vcrtcm_p_add(pcon->funcs, pcon->props, new_pconid, pcon->cookie)) {
		VCRTCM_INFO("Error registering pcon with vcrtcm\n");
		vcrtcm_kfree(pcon, &vcrtcm_kmalloc_track);
		dealloc_pconid(new_pconid);
		return -EBUSY;
	}

	vcrtcm_sysfs_add_pcon(pcon);
	list_add_tail(&pcon->pcon_list, &info->active_pcon_list);

	*pconid = new_pconid;
	return 0;
}

long vcrtcm_ioctl_destroy_pcon(int pconid)
{
	struct vcrtcm_pim_info *info;
	struct pimmgr_pcon_info *pcon;
	int pimid;
	int local_pconid;
	int r = 0;

	VCRTCM_INFO("in destroy pcon id %i...\n", pconid);

	if (!pconid_valid(pconid))
		return -EINVAL;

	pimid = pconid_get_pimid(pconid);
	local_pconid = pconid_get_local_pconid(pconid);

	info = find_pim_info_by_id(pimid);
	if (!info)
		return -EINVAL;

	pcon = find_pimmgr_pcon_info(info, local_pconid);
	if (!pcon)
		return -EINVAL;

	r = vcrtcm_p_del(pconid);
	if (r)
		return -EBUSY;

	vcrtcm_sysfs_del_pcon(pcon);

	if (info->funcs.destroy)
		info->funcs.destroy(pcon);
	else
		VCRTCM_INFO("No destroy function...\n");

	list_del(&pcon->pcon_list);
	vcrtcm_kfree(pcon, &vcrtcm_kmalloc_track);
	dealloc_pconid(pconid);
	return 0;
}

long vcrtcm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct vcrtcm_ioctl_args ioctl_args;
	long result = 0;

	VCRTCM_INFO("IOCTL entered...\n");

	if (!access_ok(VERIFY_READ, arg, sizeof(struct vcrtcm_ioctl_args)))
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE, arg, sizeof(struct vcrtcm_ioctl_args)))
		return -EFAULT;

	if (cmd == PIMMGR_IOC_INSTANTIATE) {
		void *ptr = (void *)arg;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		result = vcrtcm_ioctl_instantiate_pcon(
						ioctl_args.arg1.pimid,
						ioctl_args.arg2.hints,
						&ioctl_args.result1.pconid);
		if (result)
			return result;

		if (copy_to_user(ptr, &ioctl_args,
					sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		return 0;
	}

	else if (cmd == PIMMGR_IOC_DESTROY) {
		void *ptr = (void *)arg;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		result = vcrtcm_ioctl_destroy_pcon(ioctl_args.arg1.pconid);

		if (result)
			return result;

		return 0;
	} else
		VCRTCM_INFO("Bad IOCTL\n");

	return 0;
}
