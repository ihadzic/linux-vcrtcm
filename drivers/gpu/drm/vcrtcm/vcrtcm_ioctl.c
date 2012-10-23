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

static int add_pcon(struct vcrtcm_pcon_funcs *pcon_funcs,
		enum vcrtcm_xfer_mode xfer_mode,
		int pconid, void *pcon_cookie)
{
	struct vcrtcm_pcon_info_private *pcon_info_private;

	/* first check whether we are already registered */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry(pcon_info_private, &vcrtcm_pcon_list, list) {
		if (pcon_info_private->pcon_info.pconid == pconid) {
		    /* if the PCON already exists, we just overwrite
		       the provided functions (assuming that the PCON
		       that called us knows what it's doing */
		    mutex_lock(&pcon_info_private->pcon_info.mutex);
		    VCRTCM_WARNING("found an existing pcon %i, "
				"refreshing its implementation\n",
				pcon_info_private->pcon_info.pconid);
		    pcon_info_private->pcon_info.funcs = *pcon_funcs;
		    pcon_info_private->pcon_info.xfer_mode = xfer_mode;
		    mutex_unlock(&pcon_info_private->pcon_info.mutex);
		    mutex_unlock(&vcrtcm_pcon_list_mutex);
		    return 0;
		}
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	/*
	 * If we got here, then we are dealing with a new implementation
	 * and we have to allocate and populate the PCON structure
	 */
	pcon_info_private =
		kmalloc(sizeof(struct vcrtcm_pcon_info_private), GFP_KERNEL);
	if (pcon_info_private == NULL)
		return -ENOMEM;

	/*
	 * populate the PCON structures (no need to hold the mutex
	 *  because no one else sees this structure yet)
	 */
	spin_lock_init(&pcon_info_private->lock);
	mutex_init(&pcon_info_private->pcon_info.mutex);
	pcon_info_private->pcon_info.funcs = *pcon_funcs;
	pcon_info_private->pcon_info.xfer_mode = xfer_mode;

	/* populate the info structure and link it to the PCON structure */
	pcon_info_private->status = 0;
	pcon_info_private->pcon_info.pconid = pconid;
	pcon_info_private->pcon_info.pcon_cookie = pcon_cookie;
	pcon_info_private->vblank_time_valid = 0;
	pcon_info_private->vblank_time.tv_sec = 0;
	pcon_info_private->vblank_time.tv_usec = 0;
	pcon_info_private->drm_crtc = NULL;
	memset(&pcon_info_private->gpu_funcs, 0,
		   sizeof(struct vcrtcm_gpu_funcs));

	VCRTCM_INFO("adding new pcon %i\n",
		    pcon_info_private->pcon_info.pconid);

	/* make the new PCON available to the rest of the system */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_add(&pcon_info_private->list, &vcrtcm_pcon_list);
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	return 0;
}

/* TODO: Need better errors. */
long vcrtcm_ioctl_instantiate_pcon(int pimid, uint32_t hints, int *pconid)
{
	struct vcrtcm_pim_info *pim_info;
	struct vcrtcm_pcon_info *pcon_info;
	int new_pconid;
	int value = 0;
	int r;

	VCRTCM_INFO("in instantiate pcon...\n");
	new_pconid = vcrtcm_alloc_pconid();
	if (new_pconid < 0) {
		VCRTCM_ERROR("No pconids available...");
		return -EMFILE;
	}

	pim_info = vcrtcm_find_pim_info_by_id(pimid);

	if (!pim_info) {
		VCRTCM_INFO("Invalid pimid\n");
		vcrtcm_dealloc_pconid(new_pconid);
		return -EINVAL;
	}

	pcon_info = vcrtcm_kzalloc(sizeof(struct vcrtcm_pcon_info), GFP_KERNEL,
					&vcrtcm_kmalloc_track);
	if (!pcon_info) {
		VCRTCM_INFO("Could not allocate memory\n");
		vcrtcm_dealloc_pconid(new_pconid);
		return -ENOMEM;
	}

	pcon_info->pconid = new_pconid;
	pcon_info->pim = pim_info;
	pcon_info->minor = -1;
	if (pim_info->funcs.instantiate)
		value = pim_info->funcs.instantiate(pcon_info, hints);
	else
		VCRTCM_INFO("No instantiate function...\n");

	if (!value) {
		VCRTCM_INFO("No pcons of type %s available...\n", pim_info->name);
		vcrtcm_kfree(pcon_info, &vcrtcm_kmalloc_track);
		vcrtcm_dealloc_pconid(new_pconid);
		return -ENODEV;
	}
	value = vcrtcm_set_mapping(new_pconid, pim_info->id);

	VCRTCM_INFO("New pcon created, id %i\n", new_pconid);

	r = add_pcon(&pcon_info->funcs, pcon_info->xfer_mode, new_pconid,
		pcon_info->pcon_cookie);
	if (r) {
		VCRTCM_INFO("Error registering pcon with vcrtcm\n");
		vcrtcm_kfree(pcon_info, &vcrtcm_kmalloc_track);
		vcrtcm_dealloc_pconid(new_pconid);
		return r;
	}

	vcrtcm_sysfs_add_pcon(pcon_info);
	list_add_tail(&pcon_info->pcon_list, &pim_info->active_pcon_list);

	*pconid = new_pconid;
	return 0;
}

long vcrtcm_ioctl_destroy_pcon(int pconid)
{
	struct vcrtcm_pim_info *pim_info;
	struct vcrtcm_pcon_info *pcon_info;
	int pimid;
	int r = 0;

	VCRTCM_INFO("in destroy pcon id %i...\n", pconid);

	if (!vcrtcm_pconid_valid(pconid))
		return -EINVAL;

	pimid = vcrtcm_get_pimid(pconid);
	pim_info = vcrtcm_find_pim_info_by_id(pimid);
	if (!pim_info)
		return -EINVAL;

	pcon_info = vcrtcm_find_pcon_info(pim_info, pconid);
	if (!pcon_info)
		return -EINVAL;

	r = vcrtcm_del_pcon(pconid);
	if (r)
		return -EBUSY;

	vcrtcm_sysfs_del_pcon(pcon_info);

	if (pim_info->funcs.destroy)
		pim_info->funcs.destroy(pcon_info);
	else
		VCRTCM_INFO("No destroy function...\n");

	list_del(&pcon_info->pcon_list);
	vcrtcm_kfree(pcon_info, &vcrtcm_kmalloc_track);
	vcrtcm_dealloc_pconid(pconid);
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
