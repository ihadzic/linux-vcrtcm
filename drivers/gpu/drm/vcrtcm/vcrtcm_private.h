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
#define MAX_NUM_PCONIDS 1024

struct pconid_table_entry {
	struct vcrtcm_pcon_info *pcon_info;
};

extern struct list_head vcrtcm_pcon_list;
extern struct mutex vcrtcm_pcon_list_mutex;
extern int vcrtcm_debug;
extern struct class *vcrtcm_class;

/* List of registered PIMs */
extern struct list_head pim_list;
extern struct mutex pim_list_mutex;

/* Counter for tracking kmallocs. */
extern atomic_t vcrtcm_kmalloc_track;

/* This is the function that handles IOCTL from userspace. */
long vcrtcm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Function to find PIM info struct. */
struct vcrtcm_pim_info *vcrtcm_find_pim_info_by_id(int pimid);

/* Function to initialize the pimmgr sysfs stuff */
void vcrtcm_sysfs_init(struct device *vcrtcm_device);

/* Functions called from module init to set up/destroy structures */
int vcrtcm_structures_init(void);
void vcrtcm_structures_destroy(void);

/* Functions for managing mappings between pconids and pcon_infos */
struct vcrtcm_pcon_info *vcrtcm_alloc_pconid(void);
struct vcrtcm_pcon_info *vcrtcm_get_pcon_info(int pconid);
void vcrtcm_dealloc_pconid(int pconid);
int vcrtcm_pconid_valid(int pconid);
int vcrtcm_del_pcon(int pconid);

#define VCRTCM_DEBUG(fmt, args...) VCRTCM_DBG(1, vcrtcm_debug, fmt, ## args)

#endif
