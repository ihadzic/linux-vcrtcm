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

#include "v4l2ctd.h"
#include "v4l2ctd_utils.h"

inline struct page *v4l2ctd_alloc_page(struct v4l2ctd_info *v4l2ctd_info,
				gfp_t gfp_mask)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		v4l2ctd_info->page_track++;
	return page;
}

inline void v4l2ctd_free_page(struct v4l2ctd_info *v4l2ctd_info,
				struct page *page)
{
	__free_page(page);
	v4l2ctd_info->page_track--;
}

inline void *v4l2ctd_kmalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kmalloc(size, gfp_mask);
	if (ptr)
		v4l2ctd_info->kmalloc_track++;
	return ptr;
}

inline void *v4l2ctd_kzalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size,
			gfp_t gfp_mask)
{
	void *ptr = kzalloc(size, gfp_mask);
	if (ptr)
		v4l2ctd_info->kmalloc_track++;
	return ptr;
}

inline void v4l2ctd_kfree(struct v4l2ctd_info *v4l2ctd_info,
			void *ptr)
{
	kfree(ptr);
	v4l2ctd_info->kmalloc_track--;
}

inline void *v4l2ctd_vmalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size)
{
	void *ptr = vmalloc(size);
	if (ptr)
		v4l2ctd_info->vmalloc_track++;
	return ptr;
}

inline void *v4l2ctd_vzalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size)
{
	void *ptr = vzalloc(size);
	if (ptr)
		v4l2ctd_info->vmalloc_track++;
	return ptr;
}

inline void v4l2ctd_vfree(struct v4l2ctd_info *v4l2ctd_info,
			void *ptr)
{
	vfree(ptr);
	v4l2ctd_info->vmalloc_track--;
}
