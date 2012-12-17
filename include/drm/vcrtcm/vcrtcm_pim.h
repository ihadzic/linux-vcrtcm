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
 *    The VCRTCM-PIM API
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
#define PCON_DESC_MAXLEN 512

/*
 * descriptor for push buffer; when push-method is used
 * the PCON must obtain the buffer from GPU because it
 * must be a proper buffer object (GEM or TTM or whatever
 * the specific GPU "likes"; the PCON, however only cares about the pages
 * so this is a "minimalistic" descriptor that satisfies the PCON
 * the only TTM-ish restriction is that the list of pages first
 * lists all lo-mem pages followed by all hi-mem pages
 * of course, we need an object pointer so that we can return the buffer
 * when we don't need it any more
 */
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

/*
 * callback functions that the pim must implement for a given pcon
 */
struct vcrtcm_p_pcon_funcs {
	/*
	 * called to tell the pim that the pcon has become attached
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*attach)(int pconid, void *cookie);

	/*
	 * called to tell the pim that the pcon has become detached
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*detach)(int pconid, void *cookie);

	/*
	 * called to tell the pim to set the pcon's frame buffer
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*set_fb)(int pconid, void *cookie, struct vcrtcm_fb *fb);

	/*
	 * called to ask the pim for the pcon's frame buffer
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*get_fb)(int pconid, void *cookie, struct vcrtcm_fb *fb);

	/*
	 * called to tell the pim that the pcon's frame buffer is dirty
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*dirty_fb)(int pconid, void *cookie);

	/*
	 * called to ask the pim whether the gpu driver should wait on
	 * the pcon
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*wait_fb)(int pconid, void *cookie);

	/*
	 * called to ask the pim for the frame buffer status
	 *
	 * mutex locked: not necessarily
	 * must be atomic: YES
	 */
	int (*get_fb_status)(int pconid, void *cookie, u32 *status);

	/*
	 * called to tell the pim that the pcon's rate has been set
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*set_fps)(int pconid, void *cookie, int fps);

	/*
	 * called to tell the pim to set the pcon's cursor
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*set_cursor)(int pconid, void *cookie,
		struct vcrtcm_cursor *cursor);

	/*
	 * called to ask the pim for the pcon's cursor
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*get_cursor)(int pconid, void *cookie,
		struct vcrtcm_cursor *cursor);

	/*
	 * called to tell the pim to set the pcon's dpms state
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*set_dpms)(int pconid, void *cookie, int state);

	/*
	 * called to ask the pim for the pcon's dpms state
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*get_dpms)(int pconid, void *cookie, int *state);

	/*
	 * called to ask the pim whether the pcon is connected
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*connected)(int pconid, void *cookie, int *status);

	/*
	 * called to ask the pim for the pcon's modes
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*get_modes)(int pconid, void *cookie, struct vcrtcm_mode **modes,
		int *count);

	/*
	 * called to ask the pim to check the given mode for the pcon
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*check_mode)(int pconid, void *cookie, struct vcrtcm_mode *mode,
		int *status);

	/*
	 * called to tell the pim that the pcon has been disabled
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	void (*disable)(int pconid, void *cookie);

	/*
	 * called to tell the pim that it is time for another vblank
	 * on the pcon
	 *
	 * mutex locked: yes
	 * must be atomic: no
	 */
	int (*vblank)(int pconid, void *cookie);

	/*
	 * called to tell the pim to do a page flip on the pcon
	 *
	 * mutex locked: not necessarily
	 * must be atomic: YES
	 *
	 * additional exclusion guarantee: regardless of whether
	 * the mutex is locked, it is guaranteed that the pcon
	 * will not be destroyed while this function is executing
	 */
	int (*page_flip)(int pconid, void *cookie, u32 ioaddr);
};

/*
 * callback functions that the pim must implement
 */
struct vcrtcm_pim_funcs {
	/*
	 * This function must try to create a new PCON instance.
	 */
	int (*instantiate)(int pconid, uint32_t hints, void **cookie,
		struct vcrtcm_p_pcon_funcs *funcs,
		enum vcrtcm_xfer_mode *xfer_mode, int *minor,
		int *vblank_slack, char *description);

	/*
	 * This function *must* deallocate the given PCON and free all
	 * its resources. The PIM can assume that the given PCON has
	 * already been detached before this function is called.
	 *
	 * NB: The pcon is NOT locked when this function is called.
	 * Not locking the pcon enables the PIM to wait_sync() for
	 * other threads to finish all pcon-related operations without
	 * deadlocking with the thread that is receiving this callback.
	 */
	void (*destroy)(int pconid, void *cookie);

	/* Can do anything the user likes. */
	int (*test)(int arg);
};

/*
 * If a function is stated to be atomic, then it is guaranteed
 * to be callable in atomic context.  If its atomicty is stated
 * to be "unspecified," then it is not currently guaranteed to
 * be atomic, although its current implementation might be atomic.
 */

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_destroy(int pconid);

/*
 * mutex: need not be locked before calling this function
 * atomic: YES
 */
int vcrtcm_p_emulate_vblank(int pconid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_wait_fb(int pconid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_register_prime(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_unregister_prime(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_push(int pconid, struct vcrtcm_push_buffer_descriptor *fpbd,
	struct vcrtcm_push_buffer_descriptor *cpbd);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_hotplug(int pconid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
struct vcrtcm_push_buffer_descriptor *vcrtcm_p_alloc_pb(int pconid, int npages,
	gfp_t gfp_mask);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
struct vcrtcm_push_buffer_descriptor *vcrtcm_p_realloc_pb(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd, int npages,
	gfp_t gfp_mask);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_free_pb(int pconid, struct vcrtcm_push_buffer_descriptor *pbd);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_disable_callbacks(int pconid);

/*
 * mutex: must be locked before calling this function
 * atomic: unspecified
 */
int vcrtcm_p_log_alloc_cnts(int pconid, int on);

/*
 * locking variants of above functions.  each one locks
 * the pcon, then calls the nonlocking variant, then unlocks
 * the pcon.
 */
int vcrtcm_p_destroy_l(int pconid);
int vcrtcm_p_emulate_vblank_l(int pconid);
int vcrtcm_p_wait_fb_l(int pconid);
int vcrtcm_p_register_prime_l(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd);
int vcrtcm_p_unregister_prime_l(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd);
int vcrtcm_p_push_l(int pconid, struct vcrtcm_push_buffer_descriptor *fpbd,
	struct vcrtcm_push_buffer_descriptor *cpbd);
int vcrtcm_p_hotplug_l(int pconid);
struct vcrtcm_push_buffer_descriptor *vcrtcm_p_alloc_pb_l(int pconid, int npages,
	gfp_t gfp_mask);
struct vcrtcm_push_buffer_descriptor *vcrtcm_p_realloc_pb_l(int pconid,
	struct vcrtcm_push_buffer_descriptor *pbd, int npages,
	gfp_t gfp_mask);
int vcrtcm_p_free_pb_l(int pconid, struct vcrtcm_push_buffer_descriptor *pbd);
int vcrtcm_p_disable_callbacks_l(int pconid);
int vcrtcm_p_log_alloc_cnts_l(int pconid, int on);

int vcrtcm_pim_add_major(int pimid, int desired_major, int max_minors);
int vcrtcm_pim_del_major(int pimid);
int vcrtcm_pim_get_major(int pimid);
int vcrtcm_pim_add_minor(int pimid, int minor);
int vcrtcm_pim_del_minor(int pimid, int minor);
int vcrtcm_pim_register(char *pim_name, struct vcrtcm_pim_funcs *funcs,
	int *pimid);
int vcrtcm_pim_unregister(int pimid);
int vcrtcm_pim_enable_callbacks(int pimid);
int vcrtcm_pim_disable_callbacks(int pimid);
int vcrtcm_pim_log_alloc_cnts(int pimid, int on);

int vcrtcm_p_lock_pconid(int pconid);
int vcrtcm_p_unlock_pconid(int pconid);

#endif
