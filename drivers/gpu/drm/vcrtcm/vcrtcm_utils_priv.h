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


#ifndef __VCRTCM_UTILSPRIV_H__
#define __VCRTCM_UTILSPRIV_H__

#include <vcrtcm/vcrtcm_utils.h>

struct vcrtcm_pim;
struct vcrtcm_pcon;

#define VCRTCM_DEBUG(fmt, args...) VCRTCM_DBG(1, vcrtcm_debug, fmt, ## args)

void *vcrtcm_kzalloc_pim(size_t size, gfp_t gfp_mask, struct vcrtcm_pim *pim);
void *vcrtcm_kzalloc_pcon(size_t size, gfp_t gfp_mask, struct vcrtcm_pcon *pcon);
void *vcrtcm_kmalloc_vcrtcm(size_t size, gfp_t gfp_mask);

#endif

