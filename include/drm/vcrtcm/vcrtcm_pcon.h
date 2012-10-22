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
  Public interface for Virtual CRTC Manager to the PCON
  PCONs should include this file only
*/

#ifndef __VCRTCM_PCON_H__
#define __VCRTCM_PCON_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <vcrtcm/vcrtcm_common.h>
#include "vcrtcm_common.h"

#define PIM_NAME_MAXLEN 33
#define PCON_DESC_MAXLEN 512

/* setup/config functions */
int vcrtcm_p_add(struct vcrtcm_pcon_funcs *vcrtcm_pcon_funcs,
		  struct vcrtcm_pcon_props *vcrtcm_pcon_props,
		  uint32_t pconid, void *pcon_cookie);
int vcrtcm_p_del(uint32_t pconid);

/* functions for use by PCON in operational state */
void vcrtcm_p_emulate_vblank(struct vcrtcm_pcon_info *pcon_info);
void vcrtcm_p_wait_fb(struct vcrtcm_pcon_info *pcon_info);
int vcrtcm_p_register_prime(struct vcrtcm_pcon_info *pcon_info,
			    struct vcrtcm_push_buffer_descriptor *pbd);
void vcrtcm_p_unregister_prime(struct vcrtcm_pcon_info *pcon_info,
			       struct vcrtcm_push_buffer_descriptor *pbd);
int vcrtcm_p_push(struct vcrtcm_pcon_info *pcon_info,
		struct vcrtcm_push_buffer_descriptor *fpbd,
		struct vcrtcm_push_buffer_descriptor *cpbd);
void vcrtcm_p_hotplug(struct vcrtcm_pcon_info *pcon_info);
struct vcrtcm_push_buffer_descriptor *
vcrtcm_p_realloc_pb(struct vcrtcm_pcon_info *pcon_info,
		    struct vcrtcm_push_buffer_descriptor *pbd, int npages,
		    gfp_t gfp_mask,
		    atomic_t *kmalloc_track, atomic_t *page_track);
struct vcrtcm_push_buffer_descriptor *
vcrtcm_p_alloc_pb(struct vcrtcm_pcon_info *pcon_info, int npages,
		  gfp_t gfp_mask, atomic_t *kmalloc_track,
		  atomic_t *page_track);
void vcrtcm_p_free_pb(struct vcrtcm_pcon_info *pcon_info,
		      struct vcrtcm_push_buffer_descriptor *pbd,
		      atomic_t *kmalloc_track, atomic_t *page_track);

struct pimmgr_pcon_info;
struct pimmgr_pcon_properties;

/* Each PIM must implement these functions. */
struct pim_funcs {
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

	int (*get_properties)(struct pimmgr_pcon_info *pcon_info, struct pimmgr_pcon_properties *props);
};

struct pim_info {
	char name[PIM_NAME_MAXLEN];
	int id;
	struct pim_funcs funcs;
	struct list_head active_pcon_list;

	struct kobject kobj;
	struct list_head pim_list;
};

struct pimmgr_pcon_info {
	char description[PCON_DESC_MAXLEN];
	struct pim_info *pim;
	struct vcrtcm_pcon_funcs *funcs;
	struct vcrtcm_pcon_props *props;
	void *cookie;
	int pconid;
	int local_pconid;
	int minor; /* -1 if pcon has no user-accessible minor */
	struct kobject kobj;
	struct list_head pcon_list;
};

struct pimmgr_pcon_properties {
	int fps;
	int attached;
};

/* Called from inside a new PIM to register with pimmgr. */
int pimmgr_pim_register(char *name, struct pim_funcs *funcs);

/* Called from inside a new PIM to unregister from pimmgr. */
void pimmgr_pim_unregister(char *name);

/* Called from inside a PIM if a PCON becomes invalid */
/* (due to disconnect, etc.) */
void pimmgr_pcon_invalidate(char *name, int local_pconid);

#endif
