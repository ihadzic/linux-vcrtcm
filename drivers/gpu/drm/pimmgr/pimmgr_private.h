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

/* List of registered PIMs */
extern struct list_head pim_list;
extern struct mutex pim_list_mutex;
extern struct list_head pcon_instance_list;

/* This is the main function called from the userspace IOCTL handler. */
long pimmgr_ioctl_core(struct file *filp, unsigned int cmd, unsigned long arg);

/* Functions to find PIM info structs. */
struct pim_info *find_pim_info_by_name(char *name);
struct pim_info *find_pim_info_by_id(uint32_t pim_id);

/* Function to find an individual PCON instance info struct. */
struct pcon_instance_info *find_pcon_instance_info(struct pim_info *pim,
							uint32_t local_id);

#endif
