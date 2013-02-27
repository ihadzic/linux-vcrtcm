/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Authors:
 *     Ilija Hadzic <ihadzic@research.bell-labs.com>
 *     Martin Carroll <martin.carroll@research.bell-labs.com>
 *     William Katsak <wkatsak@cs.rutgers.edu>
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

#ifndef __VCRTCM_ALLOC_H__
#define __VCRTCM_ALLOC_H__

#include <linux/module.h>

#define VCRTCM_OWNER_VCRTCM 0x80000000
#define VCRTCM_OWNER_PIM 0x40000000
#define VCRTCM_OWNER_PCON 0x0
#define VCRTCM_OWNER_BITMASK 0xc0000000

void *vcrtcm_kmalloc(size_t size, gfp_t gfp_mask, uint32_t owner);
void *vcrtcm_kzalloc(size_t size, gfp_t gfp_mask, uint32_t owner);
void vcrtcm_kfree(void *ptr);
struct page *vcrtcm_alloc_page(gfp_t gfp_mask, uint32_t owner);
void vcrtcm_free_page(struct page *page, uint32_t owner);
int vcrtcm_alloc_multiple_pages(gfp_t gfp_mask,
	struct page **page_array,
	unsigned int num_pages,
	uint32_t owner);
void vcrtcm_free_multiple_pages(struct page **page_array,
	unsigned int num_pages, uint32_t owner);

#endif
