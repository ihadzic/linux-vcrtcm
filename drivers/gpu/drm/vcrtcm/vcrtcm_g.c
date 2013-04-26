/*
 *  Copyright (C) 2011 Alcatel-Lucent, Inc.
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
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include "vcrtcm_g.h"
#include "vcrtcm_pim_table.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_pcon.h"
#include "vcrtcm_drmdev_table.h"
#include "vcrtcm_sysfs_priv.h"

int vcrtcm_g_detach(int pconid)
{
	struct vcrtcm_pcon *pcon;
	int saved_fps;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (!pcon->drm_crtc) {
		VCRTCM_WARNING("pcon 0x%08x already detached\n", pconid);
		return -EINVAL;
	}
	saved_fps = pcon->fps;
	vcrtcm_set_fps(pcon, 0);
	if (pcon->pim_funcs.detach &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		int r;
		r = pcon->pim_funcs.detach(pconid,
					    pcon->pcon_cookie, 0);
		if (r) {
			VCRTCM_ERROR("pim refuses to detach pcon 0x%08x\n",
				pcon->pconid);
			vcrtcm_set_fps(pcon, saved_fps);
			return r;
		}
	}
	vcrtcm_detach(pcon);
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.set_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		vcrtcm_wait_if_necessary(pcon);
		VCRTCM_DEBUG("calling set_fb backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.set_fb(pconid,
					    pcon->pcon_cookie, fb);
	} else {
		VCRTCM_WARNING("missing set_fb backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.get_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_fb backend, pcon 0x%08x\n", pconid);
		r = pcon->pim_funcs.get_fb(pconid,
			pcon->pcon_cookie, fb);
	} else {
		VCRTCM_WARNING("missing get_fb backend, pcon 0x%08x\n",
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
	spinlock_t *pcon_spinlock;
	struct vcrtcm_pcon *pcon;

	/*
	* NB: this function does not require that the mutex be locked,
	* because in typical use this function is called from an isr
	*
	* vcrtcm_check_mutex(__func__, pconid);
	*/
	pcon_spinlock = vcrtcm_get_pconid_spinlock(pconid);
	if (!pcon_spinlock)
		return -EINVAL;
	spin_lock_irqsave(pcon_spinlock, flags);
	vcrtcm_set_spinlock_owner(pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		r = -ENODEV;
		goto done;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		r = -EINVAL;
		goto done;
	}
	if (pcon->pim_funcs.page_flip &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		/*
		* NB: the pcon's page_flip callback is required
		* to be callable in interrupt context
		*/
		r = pcon->pim_funcs.page_flip(pconid,
			pcon->pcon_cookie, ioaddr);
	}
done:
	vcrtcm_clear_spinlock_owner(pconid);
	spin_unlock_irqrestore(pcon_spinlock, flags);
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.dirty_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling dirty_fb backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.dirty_fb(pconid,
			pcon->pcon_cookie);
	} else {
		VCRTCM_DEBUG("missing dirty_fb backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.wait_fb &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling wait_fb backend, pcon 0x%08x\n", pconid);
		r = pcon->pim_funcs.wait_fb(pconid,
			pcon->pcon_cookie);
	} else {
		VCRTCM_DEBUG("missing wait_fb backend, pcon 0x%08x\n", pconid);
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
	struct vcrtcm_pcon *pcon;
	spinlock_t *pcon_spinlock = NULL;
	unsigned long flags = 0;
	int already_locked_by_me;
	int r = 0;

	/*
	* NB: this function does not require that the mutex be locked
	*
	* vcrtcm_check_mutex(__func__, pconid);
	*/

	/*
	 * Vcrtcm api functions that do not require that the mutex
	 * be locked use the spin lock to protect against races.
	 * In the case of this particular function, it is possible
	 * (and legal) for this function to be called when the spin
	 * lock is already held.  Here is how that can happen:
	 *
	 *    1. pim calls vcrtcm_p_emulate_vblank()
	 *    2. vcrtcm_p_emulate_vblank() takes spin lock
	 *    2. vcrtcm_p_emulate_vblank() calls gpu driver's vblank() callback
	 *    3. gpu driver's vblank() callback calls this function
	 *
	 * It is *also* possible (and legal) for this function to
	 * be called when the spin lock is *not* already held.
	 *
	 * To handle both situations correctly, this function checks
	 * whether the current pid already has the spin lock, and
	 * if so, it does not attempt to re-lock it.
	 */
	already_locked_by_me = vcrtcm_current_pid_is_spinlock_owner(pconid);
	if (!already_locked_by_me) {
		pcon_spinlock = vcrtcm_get_pconid_spinlock(pconid);
		if (!pcon_spinlock)
			return -EINVAL;
		spin_lock_irqsave(pcon_spinlock, flags);
		vcrtcm_set_spinlock_owner(pconid);
	}
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		r = -ENODEV;
		goto done;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		r = -EINVAL;
		goto done;
	}
	if (pcon->xfer_mode == VCRTCM_PEER_PUSH ||
	    pcon->xfer_mode == VCRTCM_PUSH_PULL)
		*status = VCRTCM_FB_STATUS_PUSH;
	else if (pcon->pim_funcs.get_fb_status &&
		 pcon->pcon_callbacks_enabled &&
		 pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_fb_status backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.get_fb_status(pconid,
			pcon->pcon_cookie, status);
	}
done:
	if (!already_locked_by_me) {
		vcrtcm_clear_spinlock_owner(pconid);
		spin_unlock_irqrestore(pcon_spinlock, flags);
	}
	return r;
}
EXPORT_SYMBOL(vcrtcm_g_get_fb_status);

int vcrtcm_g_get_fps(int pconid, int *fps)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.set_cursor &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling set_cursor backend, pcon 0x%08x\n",
			     pconid);
		vcrtcm_wait_if_necessary(pcon);
		r = pcon->pim_funcs.set_cursor(pconid,
			pcon->pcon_cookie, cursor);
	} else {
		VCRTCM_DEBUG("missing set_cursor backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.get_cursor &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_cursor backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.get_cursor(pconid,
			pcon->pcon_cookie, cursor);
	} else {
		VCRTCM_DEBUG("missing get_cursor backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.set_dpms && pcon->pcon_callbacks_enabled &&
	    pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling set_dpms backend, pcon 0x%08x\n", pconid);
		r = pcon->pim_funcs.set_dpms(pconid, pcon->pcon_cookie, state);
	} else {
		VCRTCM_DEBUG("missing set_dpms backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.get_dpms &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_dpms backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.get_dpms(pconid,
			pcon->pcon_cookie, state);
	} else {
		VCRTCM_DEBUG("missing get_dpms backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.connected &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling connected backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.connected(pconid,
			pcon->pcon_cookie, status);
	} else {
		VCRTCM_DEBUG("missing connected backend, pcon 0x%08x\n",
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

static struct vcrtcm_mode common_modes[18] = {
	{640, 480, 60},
	{720, 480, 60},
	{800, 600, 60},
	{848, 480, 60},
	{1024, 768, 60},
	{1024, 600, 60},
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.get_modes &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling get_modes backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.get_modes(pconid,
			pcon->pcon_cookie, modes, count);
	} else {
		VCRTCM_DEBUG("missing get_modes backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.check_mode &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling check_mode backend, pcon 0x%08x\n",
			     pconid);
		r = pcon->pim_funcs.check_mode(pconid,
			pcon->pcon_cookie, mode, status);
	} else {
		VCRTCM_DEBUG("missing check_mode backend, pcon 0x%08x\n",
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
		VCRTCM_ERROR("no pcon 0x%08x\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->pim_funcs.disable &&
		pcon->pcon_callbacks_enabled &&
		pcon->pim->callbacks_enabled) {
		VCRTCM_DEBUG("calling disable backend, pcon 0x%08x\n",
			pconid);

		pcon->pim_funcs.disable(pconid,
					 pcon->pcon_cookie);
	} else {
		VCRTCM_DEBUG("missing disable backend, pcon 0x%08x\n",
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

/*
 * the gpu-driver-side version of the pcon locking function
 * does *not* also lock the attached crtc (if there is one),
 * because drm itself locks the crtc in the drm ioctl logic.
 */
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

int vcrtcm_g_register_drmdev(struct drm_device *dev,
	struct vcrtcm_g_drmdev_funcs *funcs)
{
	struct vcrtcm_drmdev *vdev;

	VCRTCM_INFO("registering device %d (%p)\n",
		vcrtcm_drmdev_minor(dev), dev);
	vdev = vcrtcm_add_drmdev(dev, funcs);
	if (!vdev)
		return -ENOMEM;
	vcrtcm_sysfs_add_card(vdev);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_register_drmdev);

int vcrtcm_g_unregister_drmdev(struct drm_device *dev)
{
	struct vcrtcm_drmdev *vdev;

	vdev = vcrtcm_get_drmdev(dev);
	if (!vdev)
		return -EINVAL;
	vcrtcm_sysfs_del_card(vdev);
	return vcrtcm_remove_drmdev(dev);
}
EXPORT_SYMBOL(vcrtcm_g_unregister_drmdev);

int vcrtcm_g_register_connector(struct drm_connector *drm_conn, int virtual)
{
	struct vcrtcm_conn *conn;

	VCRTCM_INFO("registering connector %d of device %d (%p)\n",
		drm_conn->base.id, vcrtcm_drmdev_minor(drm_conn->dev),
		drm_conn->dev);
	conn = vcrtcm_add_conn(drm_conn, virtual);
	if (IS_ERR(conn))
		return PTR_ERR(conn);
	vcrtcm_sysfs_add_conn(conn);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_register_connector);

int vcrtcm_g_unregister_connector(struct drm_connector *drm_conn)
{
	struct vcrtcm_conn *conn;

	conn = vcrtcm_get_conn(drm_conn);
	if (!conn)
		return -EINVAL;
	BUG_ON(atomic_read(&conn->num_attached_pcons) != 0);
	vcrtcm_sysfs_del_conn(conn);
	vcrtcm_free_conn(conn);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_g_unregister_connector);
