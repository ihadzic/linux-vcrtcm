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


#ifndef __VCRTCM_PCONTABLE_H__
#define __VCRTCM_PCONTABLE_H__

#define VCRTCM_STATUS_PCON_IN_USE 0x01
#define VCRTCM_DMA_BUF_PERMS 0600
#define MAX_NUM_PCONIDS 1024

struct vcrtcm_pcon;
struct vcrtcm_pim;

struct vcrtcm_pcon *vcrtcm_alloc_pcon(struct vcrtcm_pim *pim);
void vcrtcm_dealloc_pcon(int pconid);
struct vcrtcm_pcon *vcrtcm_get_pcon(int pconid);
int vcrtcm_init_pcon_table(void);

#endif
