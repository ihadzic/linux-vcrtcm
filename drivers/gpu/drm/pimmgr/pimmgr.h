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
#include "vcrtcm/vcrtcm_common.h"

#define PIM_ID_LEN 10
#define PCON_LOCAL_ID_LEN 22

#define CREATE_PCONID(pim_id, pcon_local_id) \
	((pim_id << PCON_LOCAL_ID_LEN) | \
	(pcon_local_id))

#define PCONID_PIMID(pcon_id) (pcon_id >> PCON_LOCAL_ID_LEN)
#define PCONID_LOCALID(pcon_id) ((pcon_id << PIM_ID_LEN) >> PIM_ID_LEN)

/* List of registered PIMs */
extern struct list_head pim_list;
extern struct mutex pim_list_mutex;

struct pcon_instance_info {
	struct vcrtcm_pcon_funcs funcs;
	struct vcrtcm_pcon_props props;
	void *pcon_cookie;
	unsigned pcon_local_id;

	struct list_head list;
};

/* Each PIM must implement these functions. */
struct pim_funcs {
	/* Should return a new pcon_instance_info structure */
	/* upon success. Should return NULL upon failure. */
	struct pcon_instance_info *(*instantiate)(void *pim_private,
							char *hint_string);

	/* Should deallocate the given PCON instance and free resources used. */
	void (*destroy)(void *pim_private, struct pcon_instance_info *instance);
};

struct pim_info {
	char name[33];
	unsigned pim_id;
	struct pim_funcs funcs;
	void *pim_private;

	struct list_head list;
};

/* Called from inside a new PIM to register with pimmgr. */
int pimmgr_pim_register(char *name, struct pim_funcs *funcs, void *pim_private);

/* Called from inside a new PIM to unregister from pimmgr. */
void pimmgr_pim_unregister(char *name);


/* These will be the approximate functions called from the */
/* userspace IOCTL handler */
void *pimmgr_ioctl_getinfo(void);
unsigned pimmgr_ioctl_instantiate_pcon(char *type);
void pimmgr_ioctl_destroy_pcon(unsigned pconid);

#endif
