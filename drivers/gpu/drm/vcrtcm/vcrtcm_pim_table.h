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


#ifndef __VCRTCM_PIMTABLE_H__
#define __VCRTCM_PIMTABLE_H__

#include <vcrtcm/vcrtcm_pim.h>

struct vcrtcm_pim {
	char name[PIM_NAME_MAXLEN];
	int id;
	struct vcrtcm_pim_funcs funcs;
	struct list_head pcons_in_pim_list;
	struct kobject kobj;
	struct list_head pim_list;
	int callbacks_enabled;
	atomic_t alloc_cnt;
};

struct vcrtcm_pim *vcrtcm_get_pim(int pimid);
struct vcrtcm_pim *vcrtcm_create_pim(char *pim_name,
	struct vcrtcm_pim_funcs *funcs);
void vcrtcm_destroy_pim(struct vcrtcm_pim *pim);
void vcrtcm_init_pim_table(void);
void vcrtcm_free_pims(void);

#endif
