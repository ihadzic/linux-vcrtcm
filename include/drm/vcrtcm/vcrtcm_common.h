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
     This file defines those things that are common to the VCRTCM-PIM
     and VCRTCM-GPU APIs.
*/

#ifndef __VCRTCM_COMMON_H__
#define __VCRTCM_COMMON_H__

#define VCRTCM_FB_STATUS_IDLE  0x0
#define VCRTCM_FB_STATUS_XMIT  0x1

#define VCRTCM_DPMS_STATE_ON  0x1
#define VCRTCM_DPMS_STATE_OFF 0x0

#define VCRTCM_MODE_OK  0
#define VCRTCM_MODE_BAD 1

#define VCRTCM_PCON_DISCONNECTED 0
#define VCRTCM_PCON_CONNECTED    1

#define VCRTCM_CURSOR_FLAG_HIDE 0x1
#define VCRTCM_PFLIP_DEFERRED 1

#define PCON_DESC_MAXLEN 512

struct vcrtcm_mode;
struct drm_crtc;

enum vcrtcm_xfer_mode {
	VCRTCM_PEER_PULL,
	VCRTCM_PEER_PUSH,
	VCRTCM_PUSH_PULL
};

/* framebuffer/CRTC (emulated) registers */
struct vcrtcm_fb {
	u32 ioaddr;
	unsigned int bpp;
	unsigned int width;
	unsigned int pitch;
	unsigned int height;
	unsigned int viewport_x;
	unsigned int viewport_y;
	unsigned int hdisplay;
	unsigned int vdisplay;
};

struct vcrtcm_cursor {
	u32 ioaddr;
	unsigned int bpp;
	int location_x;
	int location_y;
	unsigned int height;
	unsigned int width;
	unsigned int flag;
};

struct vcrtcm_mode {
	int w;
	int h;
	int refresh;
};

struct vcrtcm_pcon_funcs {
	int (*attach)(int pconid, void *cookie);
	int (*detach)(int pconid, void *cookie);
	int (*set_fb)(int pconid, void *cookie, struct vcrtcm_fb *fb);
	int (*get_fb)(int pconid, void *cookie, struct vcrtcm_fb *fb);
	int (*dirty_fb)(int pconid, void *cookie, struct drm_crtc *drm_crtc);
	int (*wait_fb)(int pconid, void *cookie, struct drm_crtc *drm_crtc);
	int (*get_fb_status)(int pconid, void *cookie,
		struct drm_crtc *drm_crtc, u32 *status);
	int (*set_fps)(int pconid, void *cookie, int fps);
	int (*set_cursor)(int pconid, void *cookie,
		struct vcrtcm_cursor *cursor);
	int (*get_cursor)(int pconid, void *cookie,
		struct vcrtcm_cursor *cursor);
	int (*set_dpms)(int pconid, void *cookie, int state);
	int (*get_dpms)(int pconid, void *cookie, int *state);
	int (*connected)(int pconid, void *cookie, int *status);
	int (*get_modes)(int pconid, void *cookie, struct vcrtcm_mode **modes,
		int *count);
	int (*check_mode)(int pconid, void *cookie, struct vcrtcm_mode *mode,
		int *status);
	void (*disable)(int pconid, void *cookie);
	int (*vblank)(int pconid, void *cookie);

	/* this function must be implemented to be callable in atomic context */
	int (*page_flip)(int pconid, void *cookie, u32 ioaddr);
};

#endif
