/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Author: Bill Katsak <william.katsak@alcatel-lucent.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "udlpim.h"
#include "udlpim_utils.h"

int udlpim_alloc_multiple_pages(struct udlpim_info *udlpim_info,
				gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages)
{
	struct page *current_page;
	int i;

	for (i = 0; i < num_pages; i++) {
		current_page = udlpim_alloc_page(udlpim_info, gfp_mask);
		if (current_page) {
			page_array[i] = current_page;
		} else {
			udlpim_free_multiple_pages(udlpim_info, page_array, i);
			return 1;
		}
	}
	return 0;
}
void udlpim_free_multiple_pages(struct udlpim_info *udlpim_info,
				struct page **page_array,
				unsigned int num_pages)
{
	int i;

	for (i = 0; i < num_pages; i++)
		udlpim_free_page(udlpim_info, page_array[i]);

	return;
}

inline struct page *udlpim_alloc_page(struct udlpim_info *udlpim_info,
				gfp_t gfp_mask)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		udlpim_info->page_track++;
	return page;
}

inline void udlpim_free_page(struct udlpim_info *udlpim_info,
				struct page *page)
{
	__free_page(page);
	udlpim_info->page_track--;
}

inline void *udlpim_kmalloc(struct udlpim_info *udlpim_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kmalloc(size, gfp_mask);
	if (ptr)
		udlpim_info->kmalloc_track++;
	return ptr;
}

inline void *udlpim_kzalloc(struct udlpim_info *udlpim_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kzalloc(size, gfp_mask);
	if (ptr)
		udlpim_info->kmalloc_track++;
	return ptr;
}

inline void udlpim_kfree(struct udlpim_info *udlpim_info,
			void *ptr)
{
	kfree(ptr);
	udlpim_info->kmalloc_track--;
}

inline void *udlpim_vmalloc(struct udlpim_info *udlpim_info,
			size_t size)
{
	void *ptr = vmalloc(size);
	if (ptr)
		udlpim_info->vmalloc_track++;
	return ptr;
}

inline void *udlpim_vzalloc(struct udlpim_info *udlpim_info,
			size_t size)
{
	void *ptr = vzalloc(size);
	if (ptr)
		udlpim_info->vmalloc_track++;
	return ptr;
}

inline void udlpim_vfree(struct udlpim_info *udlpim_info,
			void *ptr)
{
	vfree(ptr);
	udlpim_info->vmalloc_track--;
}
