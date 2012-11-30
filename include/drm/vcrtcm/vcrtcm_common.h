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
   Public data structures for Virtual CRTC Manager and modules
   that use it (GPU driver and PCONs)
*/
#ifndef __VCRTCM_COMMON_H__
#define __VCRTCM_COMMON_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <linux/dma-buf.h>

#define PCON_DESC_MAXLEN 512

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

#define VCRTCM_CURSOR_FLAG_HIDE 0x1

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

#define VCRTCM_DPMS_STATE_ON  0x1
#define VCRTCM_DPMS_STATE_OFF 0x0

#define VCRTCM_FB_STATUS_IDLE  0x0
#define VCRTCM_FB_STATUS_XMIT  0x1

#define VCRTCM_PFLIP_DEFERRED 1

#define VCRTCM_PCON_DISCONNECTED 0
#define VCRTCM_PCON_CONNECTED    1

#define VCRTCM_MODE_OK  0
#define VCRTCM_MODE_BAD 1

struct vcrtcm_pcon_info;
struct pimmgr_pcon_info;
struct vcrtcm_pcon_properties;
struct vcrtcm_pcon_props;

/* every PIM must implement these functions */
struct vcrtcm_pim_funcs {
	/* Create a new PCON instance and populate a pimmgr_pcon_info
	 * structure with information about the new instance.
	 * Return 1 upon success. Return 0 upon failure.
	 */
	int (*instantiate)(struct pimmgr_pcon_info *pcon_info, uint32_t hints);

	/* Deallocate the given PCON instance and free resources used.
	 * The PIM can assume that the given PCON has been detached
	 * and removed from VCRTCM before this function is called.
	 */
	void (*destroy)(struct pimmgr_pcon_info *pcon_info);

	int (*get_properties)(struct pimmgr_pcon_info *pcon_info, struct vcrtcm_pcon_properties *props);
};

struct vcrtcm_pcon_funcs {
	int (*attach) (struct vcrtcm_pcon_info *pcon_info);
	int (*detach) (struct vcrtcm_pcon_info *pcon_info);
	int (*set_fb) (struct vcrtcm_pcon_info *pcon_info,
		       struct vcrtcm_fb *fb);
	int (*get_fb) (struct vcrtcm_pcon_info *pcon_info,
		       struct vcrtcm_fb *fb);
	int (*page_flip) (struct vcrtcm_pcon_info *pcon_info, u32 ioaddr);
	int (*dirty_fb) (struct vcrtcm_pcon_info *pcon_info,
			 struct drm_crtc *drm_crtc);
	int (*wait_fb) (struct vcrtcm_pcon_info *pcon_info,
			struct drm_crtc *drm_crtc);
	int (*get_fb_status)(struct vcrtcm_pcon_info *pcon_info,
			     struct drm_crtc *drm_crtc, u32 *status);
	int (*set_fps) (struct vcrtcm_pcon_info *pcon_info, int fps);
	int (*get_fps) (struct vcrtcm_pcon_info *pcon_info, int *fps);
	int (*set_cursor) (struct vcrtcm_pcon_info *pcon_info,
			   struct vcrtcm_cursor *cursor);
	int (*get_cursor) (struct vcrtcm_pcon_info *pcon_info,
			   struct vcrtcm_cursor *cursor);
	int (*set_dpms) (struct vcrtcm_pcon_info *pcon_info, int state);
	int (*get_dpms) (struct vcrtcm_pcon_info *pcon_info, int *state);
	int (*connected) (struct vcrtcm_pcon_info *pcon_info, int *status);
	int (*get_modes) (struct vcrtcm_pcon_info *pcon_info,
			  struct vcrtcm_mode **modes, int *count);
	int (*check_mode) (struct vcrtcm_pcon_info *pcon_info,
			   struct vcrtcm_mode *mode, int *status);
	void (*disable) (struct vcrtcm_pcon_info *pcon_info);
};

enum vcrtcm_xfer_mode {
	VCRTCM_PEER_PULL,
	VCRTCM_PEER_PUSH,
	VCRTCM_PUSH_PULL
};

/* describes properties of the attached PCON */
/* that GPU needs to know about */
/* TBD merge these */
struct vcrtcm_pcon_properties {
	int fps;
	int attached;
};
struct vcrtcm_pcon_props {
	enum vcrtcm_xfer_mode xfer_mode;
};

/* TBD merge pimmgr_pcon_info and vcrtcm_pcon_info */
struct pimmgr_pcon_info {
	char description[PCON_DESC_MAXLEN];
	struct vcrtcm_pim_info *pim;
	struct vcrtcm_pcon_funcs *funcs;
	struct vcrtcm_pcon_props *props;
	void *cookie;
	int pconid;
	int local_pconid;
	int minor; /* -1 if pcon has no user-accessible minor */
	struct kobject kobj;
	struct list_head pcon_list;
};

/* everything that vcrtcm knows about a PCON */
/* The PCON registers this structure by calling vcrtcm_hw_add() */
/* The GPU driver interacts with the PCON by calling the */
/* vcrtcm_pcon_funcs provided in this structure */
struct vcrtcm_pcon_info {
	struct mutex mutex;
	struct vcrtcm_pcon_funcs funcs;
	struct vcrtcm_pcon_props props;
	void *pcon_cookie;
	/* This is an index into a table maintained by pimmgr. */
	int pconid;
};

/* descriptor for push buffer; when push-method is used */
/* the PCON must obtain the buffer from GPU because it */
/* must be a proper buffer object (GEM or TTM or whatever */
/* the specific GPU "likes"; the PCON, however only cares about the pages */
/* so this is a "minimalistic" descriptor that satisfies the PCON */
/* the only TTM-ish restriction is that the list of pages first */
/* lists all lo-mem pages followed by all hi-mem pages */
/* of course, we need an object pointer so that we can return the buffer */
/* when we don't need it any more */
struct vcrtcm_push_buffer_descriptor {
	/* populated by VCRTCM */
	void *gpu_private;
	struct dma_buf *dma_buf;
	/* populated by PCON */
	struct vcrtcm_pcon_info *owner_pcon;
	int virgin;
	struct page **pages;
	unsigned long num_pages;
};

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

#endif
