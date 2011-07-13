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

#include "udlctd.h"
#include "udlctd_utils.h"

inline struct page *udlctd_alloc_page(struct udlctd_info *udlctd_info,
				gfp_t gfp_mask)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		udlctd_info->page_track++;
	return page;
}

inline void udlctd_free_page(struct udlctd_info *udlctd_info,
				struct page *page)
{
	__free_page(page);
	udlctd_info->page_track--;
}

inline void *udlctd_kmalloc(struct udlctd_info *udlctd_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kmalloc(size, gfp_mask);
	if (ptr)
		udlctd_info->kmalloc_track++;
	return ptr;
}

inline void *udlctd_kzalloc(struct udlctd_info *udlctd_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kzalloc(size, gfp_mask);
	if (ptr)
		udlctd_info->kmalloc_track++;
	return ptr;
}

inline void udlctd_kfree(struct udlctd_info *udlctd_info,
			void *ptr)
{
	kfree(ptr);
	udlctd_info->kmalloc_track--;
}

inline void *udlctd_vmalloc(struct udlctd_info *udlctd_info,
			size_t size)
{
	void *ptr = vmalloc(size);
	if (ptr)
		udlctd_info->vmalloc_track++;
	return ptr;
}

inline void *udlctd_vzalloc(struct udlctd_info *udlctd_info,
			size_t size)
{
	void *ptr = vzalloc(size);
	if (ptr)
		udlctd_info->vmalloc_track++;
	return ptr;
}

inline void udlctd_vfree(struct udlctd_info *udlctd_info,
			void *ptr)
{
	vfree(ptr);
	udlctd_info->vmalloc_track--;
}
