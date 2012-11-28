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
#include <vcrtcm/vcrtcm_alloc.h>
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_ioctl_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pcon_methods.h"

static long vcrtcm_ioctl_pimtest(int pimid, int testarg)
{
	struct vcrtcm_pim *pim;
	int r;

	VCRTCM_DEBUG("pimid %d, testarg %d\n", pimid, testarg);
	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("invalid pimid\n");
		return -EINVAL;
	}
	if (!pim->callbacks_enabled) {
		VCRTCM_ERROR("pim %s has callbacks disabled\n", pim->name);
		return -ECANCELED;
	}
	if (pim->funcs.test) {
		r = pim->funcs.test(testarg);
		VCRTCM_DEBUG("pimtest returned %d\n", r);
		return r;
	}
	return 0;
}

/* TODO: Need better errors. */
static long
vcrtcm_ioctl_instantiate_pcon(int pimid, uint32_t hints, int *pconid)
{
	struct vcrtcm_pim *pim;
	struct vcrtcm_pcon *pcon;
	int r;

	VCRTCM_DEBUG("pimid %d, hints %d\n", pimid, hints);
	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("invalid pimid %d\n", pimid);
		return -EINVAL;
	}
	VCRTCM_INFO("pim is %s\n", pim->name);
	if (!pim->callbacks_enabled) {
		VCRTCM_ERROR("pim %s has callbacks disabled\n", pim->name);
		return -ECANCELED;
	}
	if (!pim->funcs.instantiate) {
		VCRTCM_ERROR("pim %s has no instantiate callback\n", pim->name);
		return -ENOSYS;
	}
	pcon = vcrtcm_alloc_pcon(pim);
	if (!pcon) {
		VCRTCM_ERROR("no pconids available");
		return -ENODEV;
	}
	vcrtcm_lock_pcon(pcon);
	pcon->pim = pim;
	r = pim->funcs.instantiate(pcon->pconid, hints,
					&pcon->pcon_cookie,
					&pcon->pcon_funcs,
					&pcon->xfer_mode,
					&pcon->minor,
					&pcon->vblank_slack_jiffies,
					pcon->description);
	if (r) {
		VCRTCM_ERROR("no pcons of type %s available...\n",
			     pim->name);
		vcrtcm_dealloc_pcon(pcon->pconid);
		vcrtcm_unlock_pcon(pcon);
		vcrtcm_kfree(pcon);
		return r;
	}
	VCRTCM_INFO("new pcon created, id %i\n", pcon->pconid);
	vcrtcm_sysfs_add_pcon(pcon);
	list_add_tail(&pcon->pcons_in_pim_list,
		      &pim->pcons_in_pim_list);
	*pconid = pcon->pconid;
	vcrtcm_unlock_pcon(pcon);
	return 0;
}

/* NB: if you change the implementation of this function, you might
 * also need to change the implementation of vcrtcm_p_detach_pcon()
 */
static long do_vcrtcm_ioctl_detach_pcon(struct vcrtcm_pcon *pcon, int explicit)
{
	cancel_delayed_work_sync(&pcon->vblank_work);
	pcon->vblank_period_jiffies = 0;
	if (!pcon->drm_crtc)
		return 0;
	if (explicit)
		VCRTCM_INFO("detaching pcon %i\n", pcon->pconid);
	else
		VCRTCM_INFO("doing implicit detach of pcon %i\n",
			pcon->pconid);
	if (pcon->pcon_funcs.detach &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		int r;

		r = pcon->pcon_funcs.detach(pcon->pconid,
						pcon->pcon_cookie);
		if (r) {
			VCRTCM_ERROR("pim refuses to detach pcon %d\n",
				pcon->pconid);
			return r;
		}
	}
	if (pcon->gpu_funcs.detach)
		pcon->gpu_funcs.detach(pcon->drm_crtc);
	return 0;
}

static long vcrtcm_ioctl_destroy_pcon(int pconid)
{
	struct vcrtcm_pcon *pcon;
	void *cookie;
	struct vcrtcm_pim_funcs funcs;
	unsigned long flags;
	int r;

	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -EINVAL;
	}
	vcrtcm_lock_pcon(pcon);
	if (!pcon->pim->callbacks_enabled) {
		vcrtcm_unlock_pcon(pcon);
		VCRTCM_ERROR("pim %s has callbacks disabled\n", pcon->pim->name);
		return -ECANCELED;
	}
	r = do_vcrtcm_ioctl_detach_pcon(pcon, 0);
	if (r) {
		vcrtcm_unlock_pcon(pcon);
		VCRTCM_ERROR("detach failed, not destroying pcon %i\n",
			     pconid);
		return r;
	}
	VCRTCM_INFO("destroying pcon %i\n", pconid);
	spin_lock_irqsave(&pcon->page_flip_spinlock, flags);
	pcon->being_destroyed = 1;
	spin_unlock_irqrestore(&pcon->page_flip_spinlock, flags);
	cookie = pcon->pcon_cookie;
	funcs = pcon->pim->funcs;
	/* NB: must tell pim to destroy pcon before destroying it myself,
	 * to give the pcon a chance to returns its allocated buffers
	 */
	if (funcs.destroy)
		funcs.destroy(pconid, cookie);
	vcrtcm_destroy_pcon(pcon);
	vcrtcm_unlock_pcon(pcon);
	vcrtcm_kfree(pcon);
	return 0;
}

long vcrtcm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct vcrtcm_ioctl_args ioctl_args;
	long result = 0;

	VCRTCM_DEBUG("cmd = %u, arg = %lu\n", cmd, arg);

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

		return result;
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
	} else {
		VCRTCM_ERROR("bad IOCTL\n");
		return -EINVAL;
	}
}
