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

/* Debug flag */
extern int pimmgr_debug;

/* List of registered PIMs */
extern struct list_head pim_list;
extern struct mutex pim_list_mutex;
extern struct list_head pcon_list;

/* Counter for tracking kmallocs. */
extern atomic_t pimmgr_kmalloc_track;


/* This is the function that handles IOCTL from userspace. */
long pimmgr_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Functions to find PIM info structs. */
struct pim_info *find_pim_info_by_name(char *name);
struct pim_info *find_pim_info_by_id(uint32_t pim_id);

/* Function to find an individual PCON instance info struct. */
struct pimmgr_pcon_info *find_pimmgr_pcon_info(struct pim_info *pim,
							uint32_t local_id);

/* Function to initialize the pimmgr sysfs stuff */
void pimmgr_sysfs_init(struct device *pimmgr_device);

#endif
