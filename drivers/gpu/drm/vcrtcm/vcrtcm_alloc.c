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

/*
 * Various utility functions for Virtual CRTC Manager and PIMs
 */

#include <linux/module.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pim_table.h"

static atomic_t vcrtcm_alloc_cnt = ATOMIC_INIT(0);

static void incdec(atomic_t *cnt, int incr)
{
	if (incr)
		atomic_inc(cnt);
	else
		atomic_dec(cnt);
}

static void adjcnt(uint32_t owner, int incr)
{
	if (owner & VCRTCM_OWNER_VCRTCM) {
		incdec(&vcrtcm_alloc_cnt, incr);
	} else if (owner & VCRTCM_OWNER_PIM) {
		int pimid = owner & ~VCRTCM_OWNER_PIM;
		struct vcrtcm_pim *pim = vcrtcm_get_pim(pimid);
		if (pim)
			incdec(&pim->alloc_cnt, incr);
	} else {
		int pconid = owner & ~VCRTCM_OWNER_PCON;
		struct vcrtcm_pcon *pcon = vcrtcm_get_pcon(pconid);
		if (pcon)
			incdec(&pcon->alloc_cnt, incr);
	}
}

struct page *vcrtcm_alloc_page(gfp_t gfp_mask, uint32_t owner)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		adjcnt(owner, 1);
	return page;
}
EXPORT_SYMBOL(vcrtcm_alloc_page);

void vcrtcm_free_page(struct page *page, uint32_t owner)
{
	if (page) {
		adjcnt(owner, 0);
		__free_page(page);
	}
}
EXPORT_SYMBOL(vcrtcm_free_page);

int vcrtcm_alloc_multiple_pages(gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages,
				uint32_t owner)
{
	struct page *current_page;
	int i;

	for (i = 0; i < num_pages; i++) {
		current_page = vcrtcm_alloc_page(gfp_mask, owner);
		if (current_page) {
			page_array[i] = current_page;
		} else {
			vcrtcm_free_multiple_pages(page_array, i, owner);
			return -ENOMEM;
		}
	}
	return 0;
}
EXPORT_SYMBOL(vcrtcm_alloc_multiple_pages);

void vcrtcm_free_multiple_pages(struct page **page_array,
				unsigned int num_pages, uint32_t owner)
{
	int i;

	for (i = 0; i < num_pages; i++)
		vcrtcm_free_page(page_array[i], owner);
}
EXPORT_SYMBOL(vcrtcm_free_multiple_pages);

void *vcrtcm_kmalloc(size_t size, gfp_t gfp_mask, uint32_t owner)
{
	void *ptr = kmalloc(size + sizeof(uint64_t), gfp_mask);
	if (!ptr)
		return NULL;
	adjcnt(owner, 1);
	*(uint32_t *)ptr = owner;
	return ptr + sizeof(uint64_t);
}
EXPORT_SYMBOL(vcrtcm_kmalloc);

void *vcrtcm_kzalloc(size_t size, gfp_t gfp_mask, uint32_t owner)
{
	void *ptr = kzalloc(size + sizeof(uint64_t), gfp_mask);
	if (!ptr)
		return NULL;
	adjcnt(owner, 1);
	*(uint32_t *)ptr = owner;
	return ptr + sizeof(uint64_t);
}
EXPORT_SYMBOL(vcrtcm_kzalloc);

void vcrtcm_kfree(void *ptr)
{
	if (ptr) {
		uint32_t owner;

		ptr -= sizeof(uint64_t);
		owner = *(uint32_t *)ptr;
		adjcnt(owner, 0);
		kfree(ptr);
	}
}
EXPORT_SYMBOL(vcrtcm_kfree);
