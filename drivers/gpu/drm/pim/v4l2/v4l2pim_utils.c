/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Authors: Hans Christian Woithe <hans.woithe@alcatel-lucent.com>
		Bill Katsak <william.katsak@alcatel-lucent.com>

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

#include "v4l2pim.h"
#include "v4l2pim_utils.h"

int v4l2pim_alloc_multiple_pages(struct v4l2pim_info *v4l2pim_info,
				gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages)
{
	struct page *current_page;
	int i;

	for (i = 0; i < num_pages; i++) {
		current_page = v4l2pim_alloc_page(v4l2pim_info, gfp_mask);
		if (current_page) {
			page_array[i] = current_page;
		} else {
			v4l2pim_free_multiple_pages(v4l2pim_info,
							page_array, i);
			return 1;
		}
	}
	return 0;
}
void v4l2pim_free_multiple_pages(struct v4l2pim_info *v4l2pim_info,
				struct page **page_array,
				unsigned int num_pages)
{
	int i;

	for (i = 0; i < num_pages; i++)
		v4l2pim_free_page(v4l2pim_info, page_array[i]);

	return;
}

inline struct page *v4l2pim_alloc_page(struct v4l2pim_info *v4l2pim_info,
				gfp_t gfp_mask)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		v4l2pim_info->page_track++;
	return page;
}

inline void v4l2pim_free_page(struct v4l2pim_info *v4l2pim_info,
				struct page *page)
{
	__free_page(page);
	v4l2pim_info->page_track--;
}

inline void *v4l2pim_kmalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kmalloc(size, gfp_mask);
	if (ptr)
		v4l2pim_info->kmalloc_track++;
	return ptr;
}

inline void *v4l2pim_kzalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kzalloc(size, gfp_mask);
	if (ptr)
		v4l2pim_info->kmalloc_track++;
	return ptr;
}

inline void v4l2pim_kfree(struct v4l2pim_info *v4l2pim_info,
			void *ptr)
{
	kfree(ptr);
	v4l2pim_info->kmalloc_track--;
}

inline void *v4l2pim_vmalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size)
{
	void *ptr = vmalloc(size);
	if (ptr)
		v4l2pim_info->vmalloc_track++;
	return ptr;
}

inline void *v4l2pim_vzalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size)
{
	void *ptr = vzalloc(size);
	if (ptr)
		v4l2pim_info->vmalloc_track++;
	return ptr;
}

inline void v4l2pim_vfree(struct v4l2pim_info *v4l2pim_info,
			void *ptr)
{
	vfree(ptr);
	v4l2pim_info->vmalloc_track--;
}
