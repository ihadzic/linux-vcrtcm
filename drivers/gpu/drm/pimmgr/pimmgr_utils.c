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

#include "pimmgr.h"
#include "pimmgr_utils.h"

static atomic_t pimmgr_kmalloc_count = ATOMIC_INIT(0);
static atomic_t pimmgr_vmalloc_count = ATOMIC_INIT(0);
static atomic_t pimmgr_page_count = ATOMIC_INIT(0);


int pimmgr_alloc_multiple_pages(gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages)
{
	struct page *current_page;
	int i;

	for (i = 0; i < num_pages; i++) {
		current_page = pimmgr_alloc_page(gfp_mask);
		if (current_page) {
			page_array[i] = current_page;
		} else {
			pimmgr_free_multiple_pages(page_array, i);
			return 1;
		}
	}
	return 0;
}
void pimmgr_free_multiple_pages(struct page **page_array,
				unsigned int num_pages)
{
	int i;

	for (i = 0; i < num_pages; i++)
		pimmgr_free_page(page_array[i]);

	return;
}

inline struct page *pimmgr_alloc_page(gfp_t gfp_mask)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		atomic_inc(&pimmgr_page_count);
	return page;
}

inline void pimmgr_free_page(struct page *page)
{
	__free_page(page);
	atomic_dec(&pimmgr_page_count);
}

inline void *pimmgr_kmalloc(size_t size, gfp_t gfp_mask)
{
	void *ptr = kmalloc(size, gfp_mask);
	if (ptr)
		atomic_inc(&pimmgr_kmalloc_count);
	return ptr;
}

inline void *pimmgr_kzalloc(size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kzalloc(size, gfp_mask);
	if (ptr)
		atomic_inc(&pimmgr_kmalloc_count);
	return ptr;
}

inline void pimmgr_kfree(void *ptr)
{
	kfree(ptr);
	atomic_dec(&pimmgr_kmalloc_count);
}

inline void *pimmgr_vmalloc(size_t size)
{
	void *ptr = vmalloc(size);
	if (ptr)
		atomic_inc(&pimmgr_vmalloc_count);
	return ptr;
}

inline void *pimmgr_vzalloc(size_t size)
{
	void *ptr = vzalloc(size);
	if (ptr)
		atomic_inc(&pimmgr_vmalloc_count);
	return ptr;
}

inline void pimmgr_vfree(void *ptr)
{
	vfree(ptr);
	atomic_dec(&pimmgr_vmalloc_count);
}

