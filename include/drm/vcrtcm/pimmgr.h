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

/* A PCON ID is a system-wide identifier for an individual PCON.
 * The IDs themselves are constructed from a PIM ID and a *local*
 * PCON ID. In this case, the word "local" means "local to the PIM".
 * The local IDs are created by the PIMs internally and pimmgr has
 * no control over how they are defined or used. This allows the use
 * of a globally recognized PCON ID and still gives PIM implementers
 * flexibility in how they identify their own PCONs.
 */

#define PIM_ID_LEN 10
#define PCON_LOCAL_ID_LEN 21
#define HIGH_BIT 0x80000000

#define CREATE_PCONID(pim_id, pcon_local_id) \
		(HIGH_BIT | \
		(pim_id << PCON_LOCAL_ID_LEN) | \
		(pcon_local_id))

#define PCONID_PIMID(pcon_id) \
		((((uint32_t) pcon_id) << 1) >> (PCON_LOCAL_ID_LEN + 1))
#define PCONID_LOCALID(pcon_id) \
		((((uint32_t)pcon_id) << (PIM_ID_LEN + 1)) >> (PIM_ID_LEN + 1))
#define PCONID_VALID(pcon_id) (((uint32_t) pcon_id) & HIGH_BIT)

struct pimmgr_pcon_info;

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
	void (*destroy)(uint32_t local_pcon_id);
};

struct pim_info {
	char name[PIM_NAME_MAXLEN];
	uint32_t id;
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
	uint32_t local_id;

	struct kobject kobj;
	struct list_head pcon_list;
};

/* Called from inside a new PIM to register with pimmgr. */
int pimmgr_pim_register(char *name, struct pim_funcs *funcs);

/* Called from inside a new PIM to unregister from pimmgr. */
void pimmgr_pim_unregister(char *name);

/* Called from inside a PIM if a PCON becomes invalid */
/* (due to disconneWct, etc.) */
void pimmgr_pcon_invalidate(char *name, uint32_t pcon_local_id);

#endif
