/*
 *  Copyright (C) 2011 Alcatel-Lucent, Inc.
 *  Author: Ilija Hadzic <ihadzic@research.bell-labs.com>
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
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include "vcrtcm_gpu_methods.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_pcon.h"

/*
 * called by the GPU driver to attach its CRTC to the
 * pixel consumer (PCON)
 * GPU driver *must* supply the pointer to its own drm_crtc
 * structure that describes the CRTC and the pointer to a
 * callback function to be called on detach
 */
int vcrtcm_g_attach(int pconid,
		  struct drm_crtc *drm_crtc,
		  struct vcrtcm_gpu_funcs *gpu_funcs,
		  enum vcrtcm_xfer_mode *xfer_mode)
{

	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -EINVAL;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->drm_crtc) {
		VCRTCM_ERROR("pcon %i already attached to crtc_drm %p\n",
			     pconid, drm_crtc);
		return -EBUSY;
	}
	/*
	 * if we got here, then we have found the PCON
	 * and it's free for us to attach to
	 */
	if (pcon->pcon_funcs.attach &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		int r;
		r = pcon->pcon_funcs.attach(pcon->pconid,
					    pcon->pcon_cookie);
		if (r) {
			VCRTCM_ERROR("back-end attach call failed\n");
			return r;
		}
	}
	pcon->pconid = pconid;
	pcon->drm_crtc = drm_crtc;
	pcon->gpu_funcs = *gpu_funcs;
	*xfer_mode = pcon->xfer_mode;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_attach);

int vcrtcm_g_attach_l(int pconid,
		  struct drm_crtc *drm_crtc,
		  struct vcrtcm_gpu_funcs *gpu_funcs,
		  enum vcrtcm_xfer_mode *xfer_mode)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_attach(pconid, drm_crtc, gpu_funcs, xfer_mode);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_attach_l);

/*
 * called by the GPU driver to detach its CRTC from the
 * pixel consumer (PCON)
 * GPU driver must supply the PCON it is detaching
 * this function will also work if someoby else's
 * PCON is provided for detaching but this is not expected
 * to be used by the GPU driver (although VCRTCM may do
 * this if the PCON disappears from the system)
 *
 * after doing local cleanup, the detach function will
 * make a callback into the driver to finish up the
 * GPU driver-side cleanup (at least the GPU must
 * set the pointer to PCON to NULL and it may need to do
 * additional (driver-dependent) cleanup
 */
int vcrtcm_g_detach(int pconid)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (!pcon->drm_crtc) {
		VCRTCM_WARNING("pcon already detached\n");
		return -EINVAL;
	}
	pcon->vblank_period_jiffies = 0;
	pcon->fps = 0;
	cancel_delayed_work_sync(&pcon->vblank_work);

	/* NB: the pcon detach routine must be called before
	* the gpu detach routine, to give the pcon detach
	* routine a chance to return the pcon's push buffers
	* before the pcon is detached from the crtc.
	*/
	if (pcon->pcon_funcs.detach &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		int r;

		r = pcon->pcon_funcs.detach(pconid,
					    pcon->pcon_cookie);
		if (r)
			return r;
	}
	if (pcon->gpu_funcs.detach)
		pcon->gpu_funcs.detach(pconid, pcon->drm_crtc);
	memset(&pcon->gpu_funcs, 0, sizeof(struct vcrtcm_gpu_funcs));
	pcon->drm_crtc = NULL;
	return 0;

}
EXPORT_SYMBOL(vcrtcm_g_detach);

int vcrtcm_g_detach_l(int pconid)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_detach(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_detach_l);

/*
 * Emulates write/setup access to registers that
 * define where the frame buffer associated with
 * the CRTC is and what its geometry is
 *
 * GPU driver should pass the parameters (content to be written into
 * emulated registers); registers must be implemented
 * in the backend function; this function simply passes
 * the register content and flow information to the back-end
 * and lets the PCON deal with it
 */
int vcrtcm_g_set_fb(int pconid, struct vcrtcm_fb *fb)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.set_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling set_fb backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.set_fb(pconid,
					    pcon->pcon_cookie, fb);
	} else {
		VCRTCM_WARNING("missing set_fb backend, pcon %i\n",
			       pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_fb);

int vcrtcm_g_set_fb_l(int pconid, struct vcrtcm_fb *fb)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_set_fb(pconid, fb);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_fb_l);

/*
 * The opposite of vcrtcm_g_set_fb; GPU driver can read the content
 * of the emulated registers (implemented in the GTD driver) into
 * a structure pointed by fb argument
 */
int vcrtcm_g_get_fb(int pconid,
		  struct vcrtcm_fb *fb)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.get_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_fb backend, pcon %i\n", pconid);
		r = pcon->pcon_funcs.get_fb(pconid,
			pcon->pcon_cookie, fb);
	} else {
		VCRTCM_WARNING("missing get_fb backend, pcon %i\n",
			       pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_fb);

int vcrtcm_g_get_fb_l(int pconid, struct vcrtcm_fb *fb)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_get_fb(pconid, fb);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_fb_l);

/*
 * Emulates a page-flip call for a virtual CRTC
 * similar to vcrtcm_g_set_fb, but only the ioaddr is modified
 * and the backend is expected to make sure that frame tearing
 * is avoided
 *
 * GPU driver should pass the IO address of where the new frame buffer
 * is; backend must be able to deal with the address (FIXME: we may need
 * a 64-bit address); the function will return 0 if the flip was
 * done right away, VCRTCM_PFLIP_DEFERRED if the flip could not be
 * done immediately (backend must chache it and execute when possible)
 * or an error code when if the flip can't be done at all
 *
 * NB: This function is callable in atomic context.
 */
int vcrtcm_g_page_flip(int pconid, u32 ioaddr)
{
	int r = 0;
	unsigned long flags;
	struct vcrtcm_pcon *pcon;

	/*
	* NB: because this function is callable in atomic context,
	* the caller does not have to lock the mutex.
	*
	* vcrtcm_check_mutex(__func__, pconid);
	*/

	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	spin_lock_irqsave(&pcon->page_flip_spinlock, flags);
	if (pcon->being_destroyed)
		r = -ENODEV;
	else if (pcon->pcon_funcs.page_flip &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		/*
		* NB: the pcon's page_flip callback is required
		* to be callable in interrupt context
		*/
		r = pcon->pcon_funcs.page_flip(pconid,
			pcon->pcon_cookie, ioaddr);
	}
	spin_unlock_irqrestore(&pcon->page_flip_spinlock, flags);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_page_flip);

/*
 * GPU driver calls this function whenever the framebuffer
 * associated with a given CRTC has changed.  The PCON can
 * handle that change however it likes.  PCONs that do transmission
 * will typically simply record that the frame is dirty and then
 * transmit it at the next vblank.
 */
int vcrtcm_g_dirty_fb(int pconid)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.dirty_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling dirty_fb backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.dirty_fb(pconid,
			pcon->pcon_cookie);
	} else {
		VCRTCM_DEBUG("missing dirty_fb backend, pcon %i\n",
			     pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_dirty_fb);

int vcrtcm_g_dirty_fb_l(int pconid)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_dirty_fb(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_dirty_fb_l);

/*
 * GPU driver can use this function to wait for the PCON
 * to finish processing the frame.  The PCON can define
 * "finish processing" however it likes.  PCONs that do
 * transmission will typically wait until the frame is
 * finished being inserted into the transmission pipeline.
 */
int vcrtcm_g_wait_fb(int pconid)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.wait_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling wait_fb backend, pcon %i\n", pconid);
		r = pcon->pcon_funcs.wait_fb(pconid,
			pcon->pcon_cookie);
	} else {
		VCRTCM_DEBUG("missing wait_fb backend, pcon %i\n", pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_wait_fb);

int vcrtcm_g_wait_fb_l(int pconid)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_wait_fb(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_wait_fb_l);

/* retrieves the status of frame buffer */
int vcrtcm_g_get_fb_status(int pconid, u32 *status)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.get_fb_status &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_fb_status backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.get_fb_status(pconid,
			pcon->pcon_cookie, status);
	} else {
		VCRTCM_WARNING("missing get_fb_status backend, pcon %i\n",
			       pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_fb_status);

/* sets the frame rate */
int vcrtcm_g_set_fps(int pconid, int fps)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (fps <= 0) {
		pcon->fps = 0;
		pcon->vblank_period_jiffies = 0;
		cancel_delayed_work_sync(&pcon->vblank_work);
		VCRTCM_INFO("transmission disabled on pcon %d (fps == 0)\n",
			pconid);
	} else {
		unsigned long now;
		int old_fps = pcon->fps;
		pcon->fps = fps;
		pcon->vblank_period_jiffies = HZ/fps;
		now = jiffies;
		pcon->last_vblank_jiffies = now;
		pcon->next_vblank_jiffies = now + pcon->vblank_period_jiffies;
		if (old_fps == 0)
			schedule_delayed_work(&pcon->vblank_work, 0);
	}
	if (pcon->pcon_funcs.set_fps &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling set_fps backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.set_fps(pconid,
			pcon->pcon_cookie, fps);
	} else {
		VCRTCM_WARNING("missing set_fps backend, pcon %i\n",
			       pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_fps);

int vcrtcm_g_set_fps_l(int pconid, int fps)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_set_fps(pconid, fps);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_fps_l);

int vcrtcm_g_get_fps(int pconid, int *fps)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	*fps = pcon->fps;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_get_fps);

int vcrtcm_g_get_fps_l(int pconid, int *fps)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_get_fps(pconid, fps);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_fps_l);

/*
 * Emulates write/setup access to registers that
 * control the hardware cursor.
 *
 * GPU driver should pass the parameters (content to be written into
 * emulated registers); registers must be implemented
 * in the backend function; this function simply passes
 * the register content and flow information to the back-end
 * and lets the PCON deal with it
 */
int vcrtcm_g_set_cursor(int pconid,
		      struct vcrtcm_cursor *cursor)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.set_cursor &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling set_cursor backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.set_cursor(pconid,
			pcon->pcon_cookie, cursor);
	} else {
		VCRTCM_DEBUG("missing set_cursor backend, pcon %i\n",
			     pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_cursor);

int vcrtcm_g_set_cursor_l(int pconid,
		      struct vcrtcm_cursor *cursor)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_set_cursor(pconid, cursor);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_cursor_l);

/*
 * The opposite of vcrtcm_g_set_cursor; GPU driver can read the content
 * of the emulated registers (implemented in the GTD driver) into
 * a structure pointed by cursor argument.
 */
int vcrtcm_g_get_cursor(int pconid,
		      struct vcrtcm_cursor *cursor)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.set_cursor &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_fb backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.get_cursor(pconid,
			pcon->pcon_cookie, cursor);
	} else {
		VCRTCM_DEBUG("missing get_fb backend, pcon %i\n",
			     pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_cursor);

int vcrtcm_g_get_cursor_l(int pconid,
		      struct vcrtcm_cursor *cursor)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_get_cursor(pconid, cursor);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_cursor_l);

/* dpms manipulation functions */
int vcrtcm_g_set_dpms(int pconid, int state)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.set_dpms &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling set_dpms backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.set_dpms(pconid,
			pcon->pcon_cookie, state);
	} else {
		VCRTCM_DEBUG("missing set_dpms backend, pcon %i\n",
			     pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_dpms);

int vcrtcm_g_set_dpms_l(int pconid, int state)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_set_dpms(pconid, state);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_set_dpms_l);

/* dpms manipulation functions */
int vcrtcm_g_get_dpms(int pconid, int *state)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.get_dpms &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_dpms backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.get_dpms(pconid,
			pcon->pcon_cookie, state);
	} else {
		VCRTCM_DEBUG("missing get_dpms backend, pcon %i\n",
			     pconid);
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_dpms);

int vcrtcm_g_get_dpms_l(int pconid, int *state)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_get_dpms(pconid, state);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_dpms_l);

/* retrieve the last (fake) vblank time if it exists */
int vcrtcm_g_get_vblank_time(int pconid,
			   struct timeval *vblank_time)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (!pcon->vblank_time_valid)
		return -EAGAIN;
	*vblank_time = pcon->vblank_time;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_get_vblank_time);

/*
 * set new (fake) vblank time; used when vblank emulation
 * is generated internally by the GPU without involving the PCON
 * (typically after a successful push)
 */
int vcrtcm_g_set_vblank_time(int pconid)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	do_gettimeofday(&pcon->vblank_time);
	pcon->vblank_time_valid = 1;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_set_vblank_time);

/*
 * check if the attached PCON is in the connected state
 * some PCONs can be always connected (typically software
 * emulators), but some can feed real display devices
 * and may want to query the device they are driving for status
 */
int vcrtcm_g_pcon_connected(int pconid, int *status)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.connected &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling connected backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.connected(pconid,
			pcon->pcon_cookie, status);
	} else {
		VCRTCM_DEBUG("missing connected backend, pcon %i\n",
			     pconid);
		*status = VCRTCM_PCON_CONNECTED;
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_pcon_connected);

int vcrtcm_g_pcon_connected_l(int pconid, int *status)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_pcon_connected(pconid, status);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_pcon_connected_l);

static struct vcrtcm_mode common_modes[17] = {
	{640, 480, 60},
	{720, 480, 60},
	{800, 600, 60},
	{848, 480, 60},
	{1024, 768, 60},
	{1152, 768, 60},
	{1280, 720, 60},
	{1280, 800, 60},
	{1280, 854, 60},
	{1280, 960, 60},
	{1280, 1024, 60},
	{1440, 900, 60},
	{1400, 1050, 60},
	{1680, 1050, 60},
	{1600, 1200, 60},
	{1920, 1080, 60},
	{1920, 1200, 60}
};

/*
 * get the list of modes that the attached PCON supports
 * if the PCON does not implement the backend function, assume
 * that it can support anything and use a list of common modes
 */
int vcrtcm_g_get_modes(int pconid,
		     struct vcrtcm_mode **modes, int *count)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.get_modes &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_modes backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.get_modes(pconid,
			pcon->pcon_cookie, modes, count);
	} else {
		VCRTCM_DEBUG("missing get_modes backend, pcon %i\n",
			     pconid);
		*count = sizeof(common_modes) / sizeof(struct vcrtcm_mode);
		*modes = common_modes;
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_modes);

int vcrtcm_g_get_modes_l(int pconid,
		     struct vcrtcm_mode **modes, int *count)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_get_modes(pconid, modes, count);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_modes_l);

/*
 * check if the mode is acceprable by the attached PCON
 * if backed function is not implemented, assume the PCON
 * accepts everything and the mode is OK
 */
int vcrtcm_g_check_mode(int pconid,
		      struct vcrtcm_mode *mode, int *status)
{
	int r;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.check_mode &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling check_mode backend, pcon %i\n",
			     pconid);
		r = pcon->pcon_funcs.check_mode(pconid,
			pcon->pcon_cookie, mode, status);
	} else {
		VCRTCM_DEBUG("missing check_mode backend, pcon %i\n",
			     pconid);
		*status = VCRTCM_MODE_OK;
		r = 0;
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_check_mode);

int vcrtcm_g_check_mode_l(int pconid,
		      struct vcrtcm_mode *mode, int *status)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_check_mode(pconid, mode, status);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_check_mode_l);

/*
 * disable the specified PCON. Called when the CRTC associated with
 * the PCON is disabled from userland
 */
int vcrtcm_g_disable(int pconid)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pcon_funcs.disable &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling disable backend, pcon %i\n",
			pconid);

		pcon->pcon_funcs.disable(pconid,
					 pcon->pcon_cookie);
	} else {
		VCRTCM_DEBUG("missing disable backend, pcon %i\n",
			pconid);
	}
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_disable);

int vcrtcm_g_disable_l(int pconid)
{
	int r;

	if (vcrtcm_g_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_g_disable(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_disable_l);

int vcrtcm_g_lock_pconid(int pconid)
{
	return vcrtcm_lock_pconid(pconid);
}
EXPORT_SYMBOL(vcrtcm_g_lock_pconid);

int vcrtcm_g_unlock_pconid(int pconid)
{
	return vcrtcm_unlock_pconid(pconid);
}
EXPORT_SYMBOL(vcrtcm_g_unlock_pconid);
