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

#ifndef __PIMMGR_H__
#define __PIMMGR_H__

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <vcrtcm/vcrtcm_common.h>

#define PIM_NAME_MAXLEN 33
#define PCON_DESC_MAXLEN 512

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
	void (*destroy)(int local_pconid);

	int (*get_properties)(struct pimmgr_pcon_properties *props,
						int local_pconid);
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
