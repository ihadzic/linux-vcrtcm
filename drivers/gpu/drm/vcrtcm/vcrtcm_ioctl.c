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
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include "vcrtcm_ioctl_priv.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pcon_methods.h"

long vcrtcm_ioctl_pimtest(int pimid, int testarg)
{
	struct vcrtcm_pim *pim;
	int r;

	VCRTCM_INFO("in pimtest: %d, %d\n", pimid, testarg);
	pim = vcrtcm_find_pim_by_id(pimid);
	if (!pim) {
		VCRTCM_INFO("invalid pimid\n");
		return -EINVAL;
	}
	if (pim->funcs.test) {
		r = pim->funcs.test(testarg);
		VCRTCM_INFO("pimtest returned %d\n", r);
		return r;
	}
	return 0;
}

/* TODO: Need better errors. */
long vcrtcm_ioctl_instantiate_pcon(int pimid, uint32_t hints, int *pconid)
{
	struct vcrtcm_pim *pim;
	struct vcrtcm_pcon *pcon;
	int r;

	VCRTCM_INFO("in instantiate pcon: %d, %d\n", pimid, hints);
	pim = vcrtcm_find_pim_by_id(pimid);
	if (!pim) {
		VCRTCM_INFO("invalid pimid\n");
		return -EINVAL;
	}
	pcon = vcrtcm_alloc_pcon();
	if (!pcon) {
		VCRTCM_ERROR("no pconids available");
		return -ENODEV;
	}
	pcon->pim = pim;
	if (pim->funcs.instantiate) {
		r = pim->funcs.instantiate(pcon->pconid, hints,
						&pcon->pcon_cookie,
						&pcon->funcs,
						&pcon->xfer_mode,
						&pcon->minor,
						pcon->description);
		if (r) {
			VCRTCM_INFO("no pcons of type %s available...\n",
				    pim->name);
			vcrtcm_dealloc_pcon(pcon->pconid);
			return r;
		}
	}
	VCRTCM_INFO("new pcon created, id %i\n", pcon->pconid);
	vcrtcm_sysfs_add_pcon(pcon);
	list_add_tail(&pcon->pcons_in_pim_list,
		      &pim->pcons_in_pim_list);
	*pconid = pcon->pconid;
	return 0;
}

/* NB: if you change the implementation of this function, you might
 * also need to change the implementation of vcrtcm_p_detach_pcon()
 */
long do_vcrtcm_ioctl_detach_pcon(struct vcrtcm_pcon *pcon,
	int explicit)
{
	unsigned long flags;

	mutex_lock(&pcon->mutex);
	spin_lock_irqsave(&pcon->lock, flags);
	if (!(pcon->status & VCRTCM_STATUS_PCON_IN_USE)) {
		spin_unlock_irqrestore(&pcon->lock, flags);
		mutex_unlock(&pcon->mutex);
		return 0;
	}
	pcon->status &= ~VCRTCM_STATUS_PCON_IN_USE;
	spin_unlock_irqrestore(&pcon->lock, flags);
	if (explicit)
		VCRTCM_INFO("detaching pcon id %i\n", pcon->pconid);
	else
		VCRTCM_INFO("doing implicit detach of pcon id %i\n",
			pcon->pconid);
	if (pcon->funcs.detach) {
		int r;

		r = pcon->funcs.detach(pcon->pconid,
						pcon->pcon_cookie);
		if (r) {
			VCRTCM_ERROR("pim refuses to detach pcon %d\n",
				pcon->pconid);
			spin_lock_irqsave(&pcon->lock, flags);
			pcon->status |= VCRTCM_STATUS_PCON_IN_USE;
			spin_unlock_irqrestore(&pcon->lock, flags);
			mutex_unlock(&pcon->mutex);
			return r;
		}
	}
	if (pcon->gpu_funcs.detach)
		pcon->gpu_funcs.detach(pcon->drm_crtc);
	mutex_unlock(&pcon->mutex);
	return 0;
}

long vcrtcm_ioctl_destroy_pcon(int pconid)
{
	struct vcrtcm_pcon *pcon;
	void *cookie;
	struct vcrtcm_pim_funcs funcs;
	int r;

	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon)
		return -EINVAL;
	r = do_vcrtcm_ioctl_detach_pcon(pcon, 0);
	if (r) {
		VCRTCM_INFO("detach failed, not destroying pcon %i\n", pconid);
		return r;
	}
	VCRTCM_INFO("destroying pcon id %i\n", pconid);
	cookie = pcon->pcon_cookie;
	funcs = pcon->pim->funcs;
	vcrtcm_destroy_pcon(pcon);
	if (funcs.destroy)
		funcs.destroy(pconid, cookie);
	return 0;
}

long vcrtcm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct vcrtcm_ioctl_args ioctl_args;
	long result = 0;

	VCRTCM_INFO("IOCTL entered\n");

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
	} else if (cmd == VCRTCM_IOC_DESTROY) {
		void *ptr = (void *)arg;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		result = vcrtcm_ioctl_destroy_pcon(ioctl_args.arg1.pconid);

		if (result)
			return result;

		return 0;
	} else if (cmd == VCRTCM_IOC_PIMTEST) {
		void *ptr = (void *)arg;

		if (copy_from_user(&ioctl_args, ptr,
				sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		result = vcrtcm_ioctl_pimtest(ioctl_args.arg1.pimid,
			ioctl_args.arg2.testarg);
		if (result)
			return result;

		if (copy_to_user(ptr, &ioctl_args,
					sizeof(struct vcrtcm_ioctl_args)))
			return -EFAULT;

		return 0;
	} else
		VCRTCM_INFO("Bad IOCTL\n");

	return 0;
}
