/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Author: Ilija Hadzic <ihadzic@research.bell-labs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifndef __VCRTCM_PRIVATE_H__
#define __VCRTCM_PRIVATE_H__

#include <vcrtcm/vcrtcm_common.h>
#include <vcrtcm/vcrtcm_utils.h>

/*
 * Private data structures for Virtual CRTC Manager and modules
 * that use it: GPU driver and pixel consumer (PCON)
 */

#define VCRTCM_STATUS_PCON_IN_USE 0x01
#define VCRTCM_DMA_BUF_PERMS 0600

/* main structure for keeping track of each PCON-CRTC relationship */
struct vcrtcm_pcon_info_private {
	struct list_head list;
	/* general lock for fields subject to concurrent access */
	spinlock_t lock;
	/* see VCRTCM_STATUS_PCON constants above for possible status bits */
	int status;
	/* records the time when last (emulated) vblank occurred */
	struct timeval vblank_time;
	int vblank_time_valid;
	/* identifies the CRTC using this PCON */
	struct drm_crtc *drm_crtc;
	/* functional interface to GPU driver */
	struct vcrtcm_gpu_funcs gpu_funcs;
	/* public PCON information */
	struct vcrtcm_pcon_info pcon_info;
};

extern struct list_head vcrtcm_pcon_list;
extern struct mutex vcrtcm_pcon_list_mutex;
extern int vcrtcm_debug;
extern struct class *vcrtcm_class;

#define VCRTCM_DEBUG(fmt, args...) VCRTCM_DBG(1, vcrtcm_debug, fmt, ## args)

#endif
/*
 * Copyright (C) 2012 Alcatel-Lucent, Inc.
 * Author: Bill Katsak <william.katsak@alcatel-lucent.com>
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

#ifndef __PIMMGR_PRIVATE_H__
#define __PIMMGR_PRIVATE_H__

#define MAX_NUM_PCONIDS 1024

struct pconid_mapping {
	int pimid;
	int local_pconid;
	int pconid;
	int valid;
};

/* List of registered PIMs */
extern struct list_head pim_list;
extern struct mutex pim_list_mutex;

/* Counter for tracking kmallocs. */
extern atomic_t vcrtcm_kmalloc_track;

/* This is the function that handles IOCTL from userspace. */
long vcrtcm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Functions to find PIM info structs. */
struct vcrtcm_pim_info *vcrtcm_find_pim_info_by_id(int pimid);

/* Function to find an individual PCON instance info struct. */
struct vcrtcm_pcon_info *vcrtcm_find_pcon_info(struct vcrtcm_pim_info *pim,
							int pconid);

/* Function to initialize the pimmgr sysfs stuff */
void vcrtcm_sysfs_init(struct device *vcrtcm_device);

/* Functions called from module init to set up/destroy structures */
int vcrtcm_structures_init(void);
void vcrtcm_structures_destroy(void);

/* Functions for managing mappings between pconids and pimids/local_pconids */
int vcrtcm_alloc_pconid(void);
void vcrtcm_dealloc_pconid(int pconid);
int vcrtcm_set_mapping(int pconid, int pimid);
int vcrtcm_get_pconid(int pimid, int pconid);
int vcrtcm_pconid_valid(int pconid);
int vcrtcm_get_pimid(int pconid);
int vcrtcm_get_local_pconid(int pconid);
int vcrtcm_del_pcon(int pconid);

#endif
