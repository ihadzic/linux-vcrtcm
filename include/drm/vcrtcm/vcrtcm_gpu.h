/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
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

/*
 *     The VCRTCM-GPU API
 */

#ifndef __VCRTCM_GPU_H__
#define __VCRTCM_GPU_H__

#include <vcrtcm/vcrtcm_common.h>

struct vcrtcm_pcon;
struct vcrtcm_cursor;
struct vcrtcm_fb;
struct vcrtcm_mode;
struct drm_gem_object;
struct drm_crtc;
struct drm_device;

/*
 * callback functions that the gpu driver must implement for a given pcon
 */
struct vcrtcm_g_pcon_funcs {
	/*
	 * called to tell the gpu driver to detach the pcon
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	void (*detach)(int pconid, struct drm_crtc *drm_crtc);

	/*
	 * called to tell the gpu driver to emulate a vblank on the pcon
	 *
	 * mutex locked: not necessarily
	 * must be atomic: YES
	 *
	 * additional exclusion guarantee: regardless of whether
	 * the mutex is locked, it is guaranteed that the pcon
	 * will not be destroyed while this function is executing
	 */
	void (*vblank)(int pconid, struct drm_crtc *drm_crtc);

	/*
	 * called to tell the gpu driver to that the pim wishes
	 * to synchronize itself with the GPU's rendering to the
	 * pcon, typically by having the driver do a fence wait
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	void (*wait_fb)(int pconid, struct drm_crtc *drm_crtc);

	/*
	 * called to tell the gpu driver to push a frame buffer
	 * for the pcon
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*push)(int pconid, struct drm_crtc *drm_crtc,
			 struct drm_gem_object *dbuf_fb,
			 struct drm_gem_object *dbuf_cursor);

	/*
	 * called to tell the gpu driver that a hotplug event
	 * on the pcon has occurred
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	void (*hotplug)(int pconid, struct drm_crtc *drm_crtc);
};

/* currently empty */
struct vcrtcm_gpu_funcs {
};

/*
 * If a function is stated to be atomic, then it is guaranteed
 * to be callable in atomic context.  If its atomicity is stated
 * to be "unspecified," then it is not currently guaranteed to
 * be atomic, although its current implementation might be atomic.
 */

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_register(char *gpu_name,
	struct vcrtcm_gpu_funcs *funcs, int *gpuid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_unregister(int gpuid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_attach(int pconid, struct drm_crtc *drm_crtc,
	struct vcrtcm_g_pcon_funcs *funcs,
	enum vcrtcm_xfer_mode *xfer_mode);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_detach(int pconid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_set_fb(int pconid, struct vcrtcm_fb *fb);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_get_fb(int pconid, struct vcrtcm_fb *fb);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_dirty_fb(int pconid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_wait_fb(int pconid);

/*
 * mutex: need not be locked before calling this function
 * atomic: YES
 */
int vcrtcm_g_get_fb_status(int pconid, u32 *status);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_get_fps(int pconid, int *fps);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_set_fps(int pconid, int fps);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_set_cursor(int pconid,

		      struct vcrtcm_cursor *cursor);
/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_get_cursor(int pconid,

		      struct vcrtcm_cursor *cursor);
/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_set_dpms(int pconid, int state);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_get_dpms(int pconid, int *state);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_pcon_connected(int pconid, int *status);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_get_modes(int pconid,
		     struct vcrtcm_mode **modes, int *count);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_check_mode(int pconid,
		      struct vcrtcm_mode *mode, int *status);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_g_disable(int pconid);

/*
 * mutex: need not be locked before calling this function
 * atomic: YES
 */
int vcrtcm_g_page_flip(int pconid, u32 ioaddr);

/*
 * locking variants of above functions.  each one locks
 * the pcon, then calls the nonlocking variant, then unlocks
 * the pcon.
 */
int vcrtcm_g_attach_l(int pconid,
		  struct drm_crtc *drm_crtc,
		  struct vcrtcm_g_pcon_funcs *funcs,
		  enum vcrtcm_xfer_mode *xfer_mode);
int vcrtcm_g_detach_l(int pconid);
int vcrtcm_g_set_fb_l(int pconid, struct vcrtcm_fb *fb);
int vcrtcm_g_get_fb_l(int pconid, struct vcrtcm_fb *fb);
int vcrtcm_g_page_flip_l(int pconid, u32 ioaddr);
int vcrtcm_g_dirty_fb_l(int pconid);
int vcrtcm_g_wait_fb_l(int pconid);
int vcrtcm_g_get_fb_status_l(int pconid, u32 *status);
int vcrtcm_g_get_fps_l(int pconid, int *fps);
int vcrtcm_g_set_fps_l(int pconid, int fps);
int vcrtcm_g_set_cursor_l(int pconid,
		      struct vcrtcm_cursor *cursor);
int vcrtcm_g_get_cursor_l(int pconid,
		      struct vcrtcm_cursor *cursor);
int vcrtcm_g_set_dpms_l(int pconid, int state);
int vcrtcm_g_get_dpms_l(int pconid, int *state);
int vcrtcm_g_pcon_connected_l(int pconid, int *status);
int vcrtcm_g_get_modes_l(int pconid,
		     struct vcrtcm_mode **modes, int *count);
int vcrtcm_g_check_mode_l(int pconid,
		      struct vcrtcm_mode *mode, int *status);
int vcrtcm_g_disable_l(int pconid);

int vcrtcm_g_lock_pconid(int pconid);
int vcrtcm_g_unlock_pconid(int pconid);

#endif
