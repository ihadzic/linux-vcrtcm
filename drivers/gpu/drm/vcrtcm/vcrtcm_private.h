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

#define PIMMGR_DEBUG(fmt, args...) VCRTCM_DBG(1, pimmgr_debug, fmt, ## args)

#define MAX_NUM_PCONIDS 1024

struct pconid_mapping {
	int pimid;
	int local_pconid;
	int valid;
};

/* Debug flag */
extern int pimmgr_debug;

/* List of registered PIMs */
extern struct list_head pim_list;
extern struct mutex pim_list_mutex;

/* Counter for tracking kmallocs. */
extern atomic_t pimmgr_kmalloc_track;

/* This is the function that handles IOCTL from userspace. */
long pimmgr_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Functions to find PIM info structs. */
struct pim_info *find_pim_info_by_name(char *name);
struct pim_info *find_pim_info_by_id(int pimid);

/* Function to find an individual PCON instance info struct. */
struct pimmgr_pcon_info *find_pimmgr_pcon_info(struct pim_info *pim,
							int local_pconid);

/* Function to initialize the pimmgr sysfs stuff */
void pimmgr_sysfs_init(struct device *pimmgr_device);

/* Functions called from module init to set up/destroy structures */
int pimmgr_structures_init(void);
void pimmgr_structures_destroy(void);

/* Functions for managing mappings between pconids and pimids/local_pconids */
int alloc_pconid(void);
void dealloc_pconid(int pconid);
int pconid_set_mapping(int pconid, int pimid, int local_pconid);
int get_pconid(int pimid, int local_pconid);
int pconid_valid(int pconid);
int pconid_get_pimid(int pconid);
int pconid_get_local_pconid(int pconid);
int pimmgr_init(void);
void pimmgr_exit(void);

#endif
