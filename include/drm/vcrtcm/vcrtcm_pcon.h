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

void vcrtcm_p_invalidate(char *pim_name, int local_pconid);
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

struct vcrtcm_pim_info {
	char name[PIM_NAME_MAXLEN];
	int id;
	struct vcrtcm_pim_funcs funcs;
	struct list_head active_pcon_list;
	struct kobject kobj;
	struct list_head pim_list;
};

int vcrtcm_pim_register(char *pim_name, struct vcrtcm_pim_funcs *funcs);
void vcrtcm_pim_unregister(char *pim_name);

#endif
