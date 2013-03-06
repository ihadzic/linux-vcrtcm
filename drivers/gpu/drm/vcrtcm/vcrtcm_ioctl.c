/*
 * Copyright (C) 2012 Alcatel-Lucent, Inc.
 * Authors:
 *      Ilija Hadzic <ihadzic@research.bell-labs.com>
 *      Martin Carroll <martin.carroll@research.bell-labs.com>
 *      William Katsak <wkatsak@cs.rutgers.edu>
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
#include <drm/drm_vcrtcm.h>
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_ioctl_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pcon.h"
#include "vcrtcm_drmdev_table.h"

DEFINE_MUTEX(vcrtcm_ioctl_mutex);

static long vcrtcm_ioctl_pimtest(int pimid, int testarg)
{
	struct vcrtcm_pim *pim;
	int r;

	VCRTCM_DEBUG("pimid %d, testarg %d\n", pimid, testarg);
	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("invalid pimid %d\n", pimid);
		return -EINVAL;
	}
	if (pim->being_deregistered) {
		VCRTCM_ERROR("pim %s is deregistering\n", pim->name);
		return -ECANCELED;
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

static long
vcrtcm_ioctl_instantiate(int pimid, uint32_t requested_xfer_mode,
	uint32_t hints, int *ret_extid)
{
	struct vcrtcm_pim *pim;
	struct vcrtcm_pcon *pcon;
	int pconid;
	int r;

	VCRTCM_DEBUG("inst: pimid %d, hints %d\n", pimid, hints);
	if (requested_xfer_mode != VCRTCM_XFERMODE_UNSPECIFIED &&
		requested_xfer_mode != VCRTCM_PEER_PULL &&
		requested_xfer_mode != VCRTCM_PEER_PUSH &&
		requested_xfer_mode != VCRTCM_PUSH_PULL) {
		VCRTCM_ERROR("invalid xfer mode %d\n", requested_xfer_mode);
		return -EINVAL;
	}
	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("invalid pimid %d\n", pimid);
		return -EINVAL;
	}
	VCRTCM_INFO("pim is %s\n", pim->name);
	if (pim->being_deregistered) {
		VCRTCM_ERROR("pim %s is deregistering\n", pim->name);
		return -ECANCELED;
	}
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
	pconid = pcon->pconid;
	vcrtcm_lock_pconid(pconid);
	pcon->pim = pim;
	r = pim->funcs.instantiate(pconid, requested_xfer_mode, hints,
					&pcon->pcon_cookie,
					&pcon->pim_funcs,
					&pcon->xfer_mode,
					&pcon->minor,
					&pcon->vblank_slack_jiffies,
					pcon->description);
	if (r) {
		VCRTCM_ERROR("pim %s instantiate failed\n", pim->name);
		vcrtcm_dealloc_pcon(pcon);
		vcrtcm_unlock_pconid(pconid);
		return r;
	}
	VCRTCM_INFO("new pcon created, id 0x%08x\n", pconid);
	vcrtcm_sysfs_add_pcon(pcon);
	list_add_tail(&pcon->pcons_in_pim_list,
		      &pim->pcons_in_pim_list);
	*ret_extid = PCONID_EXTID(pconid);
	vcrtcm_unlock_pconid(pconid);
	return 0;
}

static long vcrtcm_ioctl_destroy(int extid, int force)
{
	struct vcrtcm_pcon *pcon;
	void *cookie;
	struct vcrtcm_pim_funcs funcs;
	unsigned long flags;
	spinlock_t *pcon_spinlock;

	if (vcrtcm_lock_crtc_and_extid(extid, 1))
		return -EINVAL;
	pcon = vcrtcm_get_pcon_extid(extid);
	if (!pcon) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("no pcon 0x%08x\n", extid);
		return -ENODEV;
	}
	if (pcon->pim->being_deregistered) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s is deregistering\n", pcon->pim->name);
		return -ECANCELED;
	}
	if (!pcon->pim->callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s has callbacks disabled\n",
			pcon->pim->name);
		return -ECANCELED;
	}
	if (pcon->being_destroyed) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x already being destroyed\n", pcon->pconid);
		return -EINVAL;
	}
	if (!pcon->pcon_callbacks_enabled) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x has callbacks disabled\n",
			pcon->pconid);
		return -ECANCELED;
	}
	if (pcon->drm_crtc) {
		int saved_fps = pcon->fps;

		vcrtcm_set_fps(pcon, 0);
		if (pcon->pim_funcs.detach) {
			int r;

			r = pcon->pim_funcs.detach(pcon->pconid,
				pcon->pcon_cookie, force);
			if (r) {
				VCRTCM_ERROR("pim refuses to detach pcon 0x%08x\n",
					pcon->pconid);
				vcrtcm_set_fps(pcon, saved_fps);
				vcrtcm_unlock_crtc_and_extid(extid);
				return r;
			}
		}
		if (pcon->gpu_funcs.detach)
			pcon->gpu_funcs.detach(pcon->drm_crtc);
		vcrtcm_detach(pcon);
	}
	VCRTCM_INFO("destroying pcon 0x%08x\n", pcon->pconid);
	pcon_spinlock = vcrtcm_get_pconid_spinlock(pcon->pconid);
	BUG_ON(!pcon_spinlock);
	spin_lock_irqsave(pcon_spinlock, flags);
	vcrtcm_set_spinlock_owner(pcon->pconid);
	pcon->being_destroyed = 1;
	vcrtcm_clear_spinlock_owner(pcon->pconid);
	spin_unlock_irqrestore(pcon_spinlock, flags);
	cookie = pcon->pcon_cookie;
	funcs = pcon->pim->funcs;
	/*
	 * NB: destroy() and page_flip() are the only callbacks
	 * for which the mutex is not held
	 */
	vcrtcm_unlock_crtc_and_extid(extid);
	/*
	 * NB: must tell pim to destroy pcon before destroying it myself,
	 * to give the pcon a chance to returns its allocated buffers
	 */
	if (funcs.destroy)
		funcs.destroy(pcon->pconid, cookie);
	vcrtcm_destroy_pcon(pcon);
	return 0;
}

static long vcrtcm_ioctl_attach(int extid, int connid, int major, int minor)
{
	struct vcrtcm_pcon *pcon;
	int crtc_drmid;
	int crtc_index;
	struct drm_crtc *drm_crtc;
	struct drm_connector *drm_conn;
	struct vcrtcm_drmdev *vdev;
	dev_t dev;
	int r;
	struct vcrtcm_conn *conn;

	VCRTCM_INFO("attach pcon 0x%08x to conn %d of dev %d:%d\n",
		extid, connid, major, minor);
	dev = MKDEV(major, minor);
	drm_conn = drm_vcrtcm_get_crtc_for_attach(dev, connid, &crtc_drmid,
		&crtc_index);
	if (IS_ERR(drm_conn))
		return PTR_ERR(drm_conn);
	BUG_ON(drm_conn->base.id != connid);
	BUG_ON(crtc_drmid < 0);
	BUG_ON(crtc_index < 0);
	vdev = vcrtcm_get_drmdev(drm_conn->dev);
	if (!vdev) {
		VCRTCM_ERROR("device %d:%d not registered\n", major, minor);
		return -ENODEV;
	}
	conn = vcrtcm_get_conn(drm_conn);
	if (!conn) {
		VCRTCM_ERROR("connector %d of device %d:%d not registered\n",
			connid, major, minor);
		return -ENOMEM;
	}
	if (!vdev->funcs.attach) {
		VCRTCM_ERROR("device %d:%d has no attach callback\n",
			major, minor);
		return -EINVAL;
	}
	if (vcrtcm_lock_crtc_and_extid(extid, 1))
		return -EINVAL;
	pcon = vcrtcm_get_pcon_extid(extid);
	if (!pcon) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("no pcon 0x%08x\n", extid);
		return -ENODEV;
	}
	if (pcon->pim->being_deregistered) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s is deregistering\n", pcon->pim->name);
		return -ECANCELED;
	}
	if (!pcon->pim->callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s has callbacks disabled\n",
			pcon->pim->name);
		return -ECANCELED;
	}
	if (pcon->being_destroyed) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pcon->pconid);
		return -EINVAL;
	}
	if (!pcon->pcon_callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x has callbacks disabled\n",
			pcon->pconid);
		return -ECANCELED;
	}
	if (pcon->drm_crtc) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_WARNING("pcon 0x%08x already attached\n", pcon->pconid);
		return -EINVAL;
	}
	BUG_ON(pcon->conn);
	if (pcon->pim_funcs.attach) {
		r = pcon->pim_funcs.attach(pcon->pconid, pcon->pcon_cookie);
		if (r) {
			vcrtcm_unlock_crtc_and_extid(extid);
			return r;
		}
	}
	r = vdev->funcs.attach(pcon->pconid, drm_conn->dev, crtc_drmid,
		crtc_index, pcon->xfer_mode, &pcon->gpu_funcs, &drm_crtc);
	if (r) {
		memset(&pcon->gpu_funcs, 0,
			sizeof(struct vcrtcm_g_pcon_funcs));
		if (pcon->pim_funcs.detach) {
			pcon->pim_funcs.detach(pcon->pconid,
				pcon->pcon_cookie, 1);
		}
		vcrtcm_unlock_crtc_and_extid(extid);
		return r;
	}
	pcon->drm_crtc = drm_crtc;
	if (pcon->gpu_funcs.post_attach)
		pcon->gpu_funcs.post_attach(pcon->drm_crtc);
	pcon->attach_minor = minor;
	pcon->conn = conn;
	atomic_inc(&pcon->conn->num_attached_pcons);
	vcrtcm_sysfs_attach(pcon);
	vcrtcm_unlock_crtc_and_extid(extid);
	return 0;
}

static long vcrtcm_ioctl_detach(int extid, int force)
{
	struct vcrtcm_pcon *pcon;
	int saved_fps;

	if (vcrtcm_lock_crtc_and_extid(extid, 1))
		return -EINVAL;
	pcon = vcrtcm_get_pcon_extid(extid);
	if (!pcon) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("no pcon 0x%08x\n", extid);
		return -ENODEV;
	}
	if (pcon->pim->being_deregistered) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s is deregistering\n", pcon->pim->name);
		return -ECANCELED;
	}
	if (!pcon->pim->callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s has callbacks disabled\n",
			pcon->pim->name);
		return -ECANCELED;
	}
	if (pcon->being_destroyed) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pcon->pconid);
		return -EINVAL;
	}
	if (!pcon->pcon_callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x has callbacks disabled\n",
			pcon->pconid);
		return -ECANCELED;
	}
	if (!pcon->drm_crtc) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_WARNING("pcon 0x%08x already detached\n", pcon->pconid);
		return -EINVAL;
	}
	BUG_ON(!pcon->conn);
	saved_fps = pcon->fps;
	vcrtcm_set_fps(pcon, 0);
	if (pcon->pim_funcs.detach) {
		int r;
		r = pcon->pim_funcs.detach(pcon->pconid, pcon->pcon_cookie,
			force);
		if (r) {
			VCRTCM_ERROR("pim refuses to detach pcon 0x%08x\n",
				pcon->pconid);
			vcrtcm_set_fps(pcon, saved_fps);
			vcrtcm_unlock_crtc_and_extid(extid);
			return r;
		}
	}
	if (pcon->gpu_funcs.detach)
		pcon->gpu_funcs.detach(pcon->drm_crtc);
	vcrtcm_detach(pcon);
	vcrtcm_unlock_crtc_and_extid(extid);
	return 0;
}

static long vcrtcm_ioctl_fps(int extid, int fps)
{
	int r = 0;
	struct vcrtcm_pcon *pcon;

	if (vcrtcm_lock_crtc_and_extid(extid, 1))
		return -EINVAL;
	pcon = vcrtcm_get_pcon_extid(extid);
	if (!pcon) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("no pcon 0x%08x\n", extid);
		return -ENODEV;
	}
	if (!pcon->drm_crtc) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_WARNING("pcon 0x%08x not attached\n", pcon->pconid);
		return -EINVAL;
	}
	if (pcon->pim->being_deregistered) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s is deregistering\n", pcon->pim->name);
		return -ECANCELED;
	}
	if (!pcon->pim->callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s has callbacks disabled\n",
			pcon->pim->name);
		return -ECANCELED;
	}
	if (pcon->being_destroyed) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pcon->pconid);
		return -EINVAL;
	}
	if (!pcon->pcon_callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x has callbacks disabled\n",
			pcon->pconid);
		return -ECANCELED;
	}
	vcrtcm_set_fps(pcon, fps);
	if (pcon->pim_funcs.set_fps)
		r = pcon->pim_funcs.set_fps(pcon->pconid, pcon->pcon_cookie,
			fps);
	vcrtcm_unlock_crtc_and_extid(extid);
	return r;
}

static long vcrtcm_ioctl_xmit(int extid)
{
	int r = 0;
	struct vcrtcm_pcon *pcon;

	if (vcrtcm_lock_crtc_and_extid(extid, 1))
		return -EINVAL;
	pcon = vcrtcm_get_pcon_extid(extid);
	if (!pcon) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("no pcon 0x%08x\n", extid);
		return -ENODEV;
	}
	if (pcon->pim->being_deregistered) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s is deregistering\n", pcon->pim->name);
		return -ECANCELED;
	}
	if (!pcon->pim->callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pim %s has callbacks disabled\n",
			pcon->pim->name);
		return -ECANCELED;
	}
	if (pcon->being_destroyed) {
		vcrtcm_unlock_crtc_and_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pcon->pconid);
		return -EINVAL;
	}
	if (!pcon->pcon_callbacks_enabled) {
		vcrtcm_unlock_extid(extid);
		VCRTCM_ERROR("pcon 0x%08x has callbacks disabled\n",
			pcon->pconid);
		return -ECANCELED;
	}
	if (pcon->pim_funcs.dirty_fb)
		r = pcon->pim_funcs.dirty_fb(pcon->pconid, pcon->pcon_cookie);
	vcrtcm_unlock_crtc_and_extid(extid);
	return r;
}

long vcrtcm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct vcrtcm_ioctl_args args;
	long r;

	VCRTCM_DEBUG("ioctl: cmd = %u, arg = %lu, pid = %d\n", cmd, arg,
		current->pid);
	if (!access_ok(VERIFY_WRITE, arg, sizeof(args)))
		return -EFAULT;
	if (copy_from_user(&args, (void *)arg, sizeof(args)))
		return -EFAULT;
	mutex_lock(&vcrtcm_ioctl_mutex);
	switch (cmd) {
	case VCRTCM_IOC_INSTANTIATE:
		r = vcrtcm_ioctl_instantiate(args.arg1.pimid,
			args.arg2.xfer_mode, args.arg3.hints,
			&args.result1.pconid);
		break;
	case VCRTCM_IOC_DESTROY:
		r = vcrtcm_ioctl_destroy(args.arg1.pconid, args.arg2.force);
		break;
	case VCRTCM_IOC_ATTACH:
		r = vcrtcm_ioctl_attach(args.arg1.pconid, args.arg2.connid,
			args.arg3.major, args.arg4.minor);
		break;
	case VCRTCM_IOC_DETACH:
		r = vcrtcm_ioctl_detach(args.arg1.pconid, args.arg2.force);
		break;
	case VCRTCM_IOC_FPS:
		r = vcrtcm_ioctl_fps(args.arg1.pconid, args.arg2.fps);
		break;
	case VCRTCM_IOC_XMIT:
		r = vcrtcm_ioctl_xmit(args.arg1.pconid);
		break;
	case VCRTCM_IOC_PIMTEST:
		r = vcrtcm_ioctl_pimtest(args.arg1.pimid, args.arg2.testarg);
		break;
	default:
		VCRTCM_ERROR("bad ioctl: %d\n", cmd);
		r = -EINVAL;
		break;
	}
	mutex_unlock(&vcrtcm_ioctl_mutex);
	if (r)
		return r;
	if (copy_to_user((void *)arg, &args, sizeof(args)))
		return -EFAULT;
	return 0;
}
