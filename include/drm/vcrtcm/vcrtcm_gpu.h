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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <linux/dma-buf.h>
#include <vcrtcm/vcrtcm_common.h>

struct vcrtcm_pcon;
struct vcrtcm_cursor;
struct vcrtcm_fb;
struct vcrtcm_mode;

/* functional interface to GPU driver */
struct vcrtcm_gpu_funcs {
	/* callback into GPU driver when detach is called */
	void (*detach) (struct drm_crtc *drm_crtc);

	/* VBLANK emulation function  */
	void (*vblank) (struct drm_crtc *drm_crtc);

	/* synchronization with GPU rendering (e.g. fence wait) */
	void (*wait_fb) (struct drm_crtc *drm_crtc);

	/* PCON requests from GPU to push the buffer to it */
	int (*push) (struct drm_crtc *scrtc,
			 struct drm_gem_object *dbuf_fb,
			 struct drm_gem_object *dbuf_cursor);
	/* PCON signals a hotplug event to GPU */
	void (*hotplug) (struct drm_crtc *crtc);
};

struct vcrtcm_pcon {
	char description[PCON_DESC_MAXLEN];
	struct vcrtcm_pim *pim;
	struct vcrtcm_pcon_funcs pcon_funcs;
	struct vcrtcm_gpu_funcs gpu_funcs;
	int pcon_callbacks_enabled;
	enum vcrtcm_xfer_mode xfer_mode;
	void *pcon_cookie;
	int pconid; /* index into table maintained by vcrtcm */
	int minor; /* -1 if pcon has no user-accessible minor */
	struct kobject kobj;
	struct list_head pcons_in_pim_list;
	struct mutex mutex;
	spinlock_t lock;
	/* see VCRTCM_STATUS_PCON constants above for possible status bits */
	int status;
	/* records the time when last (emulated) vblank occurred */
	struct timeval vblank_time;
	int vblank_time_valid;
	/* identifies the CRTC using this PCON */
	struct drm_crtc *drm_crtc;
	int alloc_cnt;
	int page_alloc_cnt;
	int log_alloc_cnts;
	int log_alloc_bugs;
	struct delayed_work vblank_work;
	int fps;
	unsigned long vblank_period_jiffies;
	unsigned long last_vblank_jiffies;
	unsigned long next_vblank_jiffies;
	int vblank_slack_jiffies;
};

int vcrtcm_g_attach(int pconid,
		  struct drm_crtc *drm_crtc,
		  struct vcrtcm_gpu_funcs *gpu_callbacks,
		  struct vcrtcm_pcon **pcon);
int vcrtcm_g_detach(struct vcrtcm_pcon *pcon);
int vcrtcm_g_set_fb(struct vcrtcm_pcon *pcon, struct vcrtcm_fb *fb);
int vcrtcm_g_get_fb(struct vcrtcm_pcon *pcon, struct vcrtcm_fb *fb);
int vcrtcm_g_page_flip(struct vcrtcm_pcon *pcon, u32 ioaddr);
int vcrtcm_g_dirty_fb(struct vcrtcm_pcon *pcon);
int vcrtcm_g_wait_fb(struct vcrtcm_pcon *pcon);
int vcrtcm_g_get_fb_status(struct vcrtcm_pcon *pcon, u32 *status);
int vcrtcm_g_get_fps(struct vcrtcm_pcon *pcon, int *fps);
int vcrtcm_g_set_fps(struct vcrtcm_pcon *pcon, int fps);
int vcrtcm_g_set_cursor(struct vcrtcm_pcon *pcon,
		      struct vcrtcm_cursor *cursor);
int vcrtcm_g_get_cursor(struct vcrtcm_pcon *pcon,
		      struct vcrtcm_cursor *cursor);
int vcrtcm_g_set_dpms(struct vcrtcm_pcon *pcon, int state);
int vcrtcm_g_get_dpms(struct vcrtcm_pcon *pcon, int *state);
int vcrtcm_g_get_vblank_time(struct vcrtcm_pcon *pcon,
			   struct timeval *vblank_time);
void vcrtcm_g_set_vblank_time(struct vcrtcm_pcon *pcon);
int vcrtcm_g_pcon_connected(struct vcrtcm_pcon *pcon, int *status);
int vcrtcm_g_get_modes(struct vcrtcm_pcon *pcon,
		     struct vcrtcm_mode **modes, int *count);
int vcrtcm_g_check_mode(struct vcrtcm_pcon *pcon,
		      struct vcrtcm_mode *mode, int *status);
void vcrtcm_g_disable(struct vcrtcm_pcon *pcon);

#endif
