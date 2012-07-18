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

#define PIM_NAME_LEN 33
#define PCON_DESC_LEN 128

#define PIM_ID_LEN 10
#define PCON_LOCAL_ID_LEN 21
#define HIGH_BIT (1 << (sizeof(uint32_t)*8 - 1))

#define CREATE_PCONID(pim_id, pcon_local_id) \
		(HIGH_BIT | \
		(pim_id << PCON_LOCAL_ID_LEN) | \
		(pcon_local_id))

#define PCONID_PIMID(pcon_id) \
		((((uint32_t) pcon_id) << 1) >> (PCON_LOCAL_ID_LEN + 1))
#define PCONID_LOCALID(pcon_id) \
		((((uint32_t)pcon_id) << (PIM_ID_LEN + 1)) >> (PIM_ID_LEN + 1))
#define PCONID_VALID(pcon_id) (((uint32_t) pcon_id) & HIGH_BIT)

/* List of registered PIMs */
extern struct list_head pim_list;
extern struct mutex pim_list_mutex;
extern struct list_head pcon_instance_list;

struct pcon_instance_info;

/* Each PIM must implement these functions. */
struct pim_funcs {
	/* Return a new pcon_instance_info structure */
	/* upon success. Return NULL upon failure. */
	int (*instantiate)(struct pcon_instance_info *instance_info,
				void *data, uint32_t hints);

	/* Deallocate the given PCON instance and free resources used. */
	void (*destroy)(uint32_t local_pcon_id, void *data);
};

struct pim_info {
	char name[PIM_NAME_LEN];
	uint32_t id;
	struct pim_funcs funcs;
	void *data;
	struct kobject kobj;

	struct list_head pim_list;
};

struct pcon_instance_info {
	struct pim_info *pim;
	struct vcrtcm_pcon_funcs *funcs;
	struct vcrtcm_pcon_props *props;
	void *cookie;
	uint32_t local_id;

	struct kobject kobj;
	struct list_head instance_list;
};

/* Called from inside a new PIM to register with pimmgr. */
int pimmgr_pim_register(char *name, struct pim_funcs *funcs, void *data);

/* Called from inside a new PIM to unregister from pimmgr. */
void pimmgr_pim_unregister(char *name);

/* Called from inside a PIM if a PCON becomes invalid */
/* (due to disconneWct, etc.) */
void pimmgr_pcon_invalidate(char *name, uint32_t pcon_local_id);

/* These will be the approximate functions called from the */
/* userspace IOCTL handler */
long pimmgr_ioctl_core(struct file *filp, unsigned int cmd, unsigned long arg);
uint32_t pimmgr_ioctl_instantiate_pcon(char *name, uint32_t hints,
							uint32_t *pconid);
int pimmgr_ioctl_destroy_pcon(uint32_t pconid);

struct pim_info *create_pim_info(char *name, struct pim_funcs *funcs,
					void *data);
void update_pim_info(char *name, struct pim_funcs *funcs, void *data);
struct pim_info *find_pim_info_by_name(char *name);
struct pim_info *find_pim_info_by_id(uint32_t pim_id);
void destroy_pim_info(struct pim_info *info);
void add_pim_info(struct pim_info *info);
void remove_pim_info(struct pim_info *info);
struct pcon_instance_info *find_pcon_instance_info(struct pim_info *pim,
							uint32_t local_id);
int pimmgr_pim_register(char *name, struct pim_funcs *funcs, void *data);
void pimmgr_pim_unregister(char *name);


#endif
