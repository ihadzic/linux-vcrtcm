/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Author: Ilija Hadzic <ihadzic@research.bell-labs.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
     The VCRTCM-GPU API
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

struct vcrtcm_g_pcon_funcs {
	/* callback into GPU driver when detach is called */
	void (*detach)(int pconid, struct drm_crtc *drm_crtc);

	/* VBLANK emulation function  */
	void (*vblank)(int pconid, struct drm_crtc *drm_crtc);

	/* synchronization with GPU rendering (e.g. fence wait) */
	void (*wait_fb)(int pconid, struct drm_crtc *drm_crtc);

	/* PCON requests from GPU to push the buffer to it */
	int (*push)(int pconid, struct drm_crtc *drm_crtc,
			 struct drm_gem_object *dbuf_fb,
			 struct drm_gem_object *dbuf_cursor);

	/* PCON signals a hotplug event to GPU */
	void (*hotplug)(int pconid, struct drm_crtc *drm_crtc);
};

/* currently empty */
struct vcrtcm_gpu_funcs {
};

/*
 * If a function is stated to be atomic, then it is guaranteed
 * to be callable in atomic context.  If its atomicness is
 * stated to be "unspecified," then it is not currently guaranteed
 * to be atomic, although its current implementation might be
 * atomic.
 */

/*
 * atomic: unspecified
 */
int vcrtcm_g_register(char *gpu_name,
	struct vcrtcm_gpu_funcs *funcs, int *gpuid);

/*
* atomic: unspecified
*/
int vcrtcm_g_unregister(int gpuid);

/*
 * atomic: unspecified
 */
int vcrtcm_g_attach(int pconid, struct drm_crtc *drm_crtc,
	struct vcrtcm_g_pcon_funcs *funcs,
	enum vcrtcm_xfer_mode *xfer_mode);
/*
 * atomic: unspecified
 */
int vcrtcm_g_detach(int pconid);

/*
 * atomic: unspecified
 */
int vcrtcm_g_set_fb(int pconid, struct vcrtcm_fb *fb);

/*
 * atomic: unspecified
 */
int vcrtcm_g_get_fb(int pconid, struct vcrtcm_fb *fb);

/*
 * atomic: unspecified
 */
int vcrtcm_g_dirty_fb(int pconid);

/*
 * atomic: unspecified
 */
int vcrtcm_g_wait_fb(int pconid);

/*
 * atomic: YES
 */
int vcrtcm_g_get_fb_status(int pconid, u32 *status);

/*
 * atomic: unspecified
 */
int vcrtcm_g_get_fps(int pconid, int *fps);

/*
 * atomic: unspecified
 */
int vcrtcm_g_set_fps(int pconid, int fps);

/*
 * atomic: unspecified
 */
int vcrtcm_g_set_cursor(int pconid,

		      struct vcrtcm_cursor *cursor);
/*
 * atomic: unspecified
 */
int vcrtcm_g_get_cursor(int pconid,

		      struct vcrtcm_cursor *cursor);
/*
 * atomic: unspecified
 */
int vcrtcm_g_set_dpms(int pconid, int state);

/*
 * atomic: unspecified
 */
int vcrtcm_g_get_dpms(int pconid, int *state);

/*
 * atomic: YES
 */
int vcrtcm_g_get_vblank_time(int pconid,
			   struct timeval *vblank_time);

/*
 * atomic: unspecified
 */
int vcrtcm_g_set_vblank_time(int pconid);

/*
 * atomic: unspecified
 */
int vcrtcm_g_pcon_connected(int pconid, int *status);

/*
 * atomic: unspecified
 */
int vcrtcm_g_get_modes(int pconid,
		     struct vcrtcm_mode **modes, int *count);

/*
 * atomic: unspecified
 */
int vcrtcm_g_check_mode(int pconid,
		      struct vcrtcm_mode *mode, int *status);

/*
 * atomic: unspecified
 */
int vcrtcm_g_disable(int pconid);

/*
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
int vcrtcm_g_get_vblank_time_l(int pconid,
			   struct timeval *vblank_time);
int vcrtcm_g_set_vblank_time_l(int pconid);
int vcrtcm_g_pcon_connected_l(int pconid, int *status);
int vcrtcm_g_get_modes_l(int pconid,
		     struct vcrtcm_mode **modes, int *count);
int vcrtcm_g_check_mode_l(int pconid,
		      struct vcrtcm_mode *mode, int *status);
int vcrtcm_g_disable_l(int pconid);

int vcrtcm_g_lock_pconid(int pconid);
int vcrtcm_g_unlock_pconid(int pconid);

#endif
