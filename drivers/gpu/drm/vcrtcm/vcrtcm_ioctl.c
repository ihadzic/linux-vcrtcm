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
	struct vcrtcm_pim_info *pim_info;
	struct vcrtcm_pcon_info *pcon_info;
	int r;

	VCRTCM_INFO("in instantiate pcon...\n");
	pim_info = vcrtcm_find_pim_info_by_id(pimid);
	if (!pim_info) {
		VCRTCM_INFO("invalid pimid\n");
		return -EINVAL;
	}
	pcon_info = vcrtcm_alloc_pcon_info();
	if (!pcon_info) {
		VCRTCM_ERROR("no pconids available");
		return -ENODEV;
	}
	pcon_info->pim = pim_info;
	if (pim_info->funcs.instantiate) {
		r = pim_info->funcs.instantiate(pcon_info->pconid, hints,
						&pcon_info->pcon_cookie,
						&pcon_info->funcs,
						&pcon_info->xfer_mode,
						&pcon_info->minor,
						pcon_info->description);
		if (r) {
			VCRTCM_INFO("no pcons of type %s available...\n",
				    pim_info->name);
			vcrtcm_dealloc_pcon_info(pcon_info->pconid);
			return r;
		}
	}
	VCRTCM_INFO("new pcon created, id %i\n", pcon_info->pconid);
	vcrtcm_sysfs_add_pcon(pcon_info);
	list_add_tail(&pcon_info->pcons_in_pim_list,
		      &pim_info->pcons_in_pim_list);
	*pconid = pcon_info->pconid;
	return 0;
}

long vcrtcm_ioctl_destroy_pcon(int pconid)
{
	struct vcrtcm_pcon_info *pcon_info;
	int r = 0;

	VCRTCM_INFO("in destroy pcon id %i...\n", pconid);
	pcon_info = vcrtcm_get_pcon_info(pconid);
	if (!pcon_info)
		return -EINVAL;
	r = vcrtcm_del_pcon(pconid);
	if (r)
		return -EBUSY;
	vcrtcm_sysfs_del_pcon(pcon_info);
	if (!pcon_info->pim)
		VCRTCM_ERROR("pcon has no pim!\n");
	else {
		if (pcon_info->pim->funcs.destroy)
			pcon_info->pim->funcs.destroy(pcon_info->pconid,
						      pcon_info->pcon_cookie);
		else
			VCRTCM_INFO("No destroy function...\n");
	}
	list_del(&pcon_info->pcons_in_pim_list);
	vcrtcm_dealloc_pcon_info(pconid);
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

	if (cmd == VCRTCM_IOC_INSTANTIATE) {
		void *ptr = (void *)arg;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		result = vcrtcm_ioctl_instantiate_pcon(ioctl_args.arg1.pimid,
						ioctl_args.arg2.hints,
						&ioctl_args.result1.pconid);
		if (result)
			return result;

		if (copy_to_user(ptr, &ioctl_args,
					sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		return 0;
	}

	else if (cmd == VCRTCM_IOC_DESTROY) {
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
