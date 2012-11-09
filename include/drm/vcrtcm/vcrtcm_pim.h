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
     The VCRTCM-PIM API
*/

#ifndef __VCRTCM_PIM_H__
#define __VCRTCM_PIM_H__

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/highmem.h>
#include <vcrtcm/vcrtcm_common.h>

#define PIM_NAME_MAXLEN 33

struct drm_crtc;

/* describes properties of the attached PCON */
/* that GPU needs to know about */
struct vcrtcm_pcon_properties {
	int fps;
	int attached;
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
	int pconid;
	int virgin;
	struct page **pages;
	unsigned long num_pages;
};

/* every PIM must implement these functions */
struct vcrtcm_pim_funcs {
	/* Create a new PCON instance
	 */
	int (*instantiate)(int pconid, uint32_t hints, void **cookie,
		struct vcrtcm_pcon_funcs *funcs,
		enum vcrtcm_xfer_mode *xfer_mode, int *minor,
		char *description);

	/* Deallocate the given PCON instance and free resources used.
	 * The PIM can assume that the given PCON has been detached
	 * and removed from VCRTCM before this function is called.
	 */
	void (*destroy)(int pconid, void *cookie);

	/* Can do anything the user likes. */
	int (*test)(int arg);
};

void vcrtcm_p_destroy(int pconid);
void vcrtcm_p_emulate_vblank(int pconid);
void vcrtcm_p_wait_fb(int pconid);
int vcrtcm_p_register_prime(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd);
void vcrtcm_p_unregister_prime(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd);
int vcrtcm_p_push(int pconid, struct vcrtcm_push_buffer_descriptor *fpbd,
	struct vcrtcm_push_buffer_descriptor *cpbd);
void vcrtcm_p_hotplug(int pconid);
struct vcrtcm_push_buffer_descriptor *vcrtcm_p_alloc_pb(int pconid, int npages,
	gfp_t gfp_mask);
struct vcrtcm_push_buffer_descriptor *vcrtcm_p_realloc_pb(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd, int npages,
	gfp_t gfp_mask);
void vcrtcm_p_free_pb(int pconid, struct vcrtcm_push_buffer_descriptor *pbd);
void vcrtcm_p_disable_callbacks(int pconid);
void vcrtcm_p_log_alloc_cnts(int pconid, int on);

int vcrtcm_pim_register(char *pim_name, struct vcrtcm_pim_funcs *funcs,
	int *pimid);
void vcrtcm_pim_unregister(int pimid);
void vcrtcm_pim_enable_callbacks(int pimid);
void vcrtcm_pim_disable_callbacks(int pimid);
void vcrtcm_pim_log_alloc_cnts(int pimid, int on);

#endif
