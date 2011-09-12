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

#ifndef __v4l2ctd_UTILS_H__
#define __v4l2ctd_UTILS_H__

inline struct page *v4l2ctd_alloc_page(struct v4l2ctd_info *v4l2ctd_info,
				gfp_t gfp_mask);

inline void v4l2ctd_free_page(struct v4l2ctd_info *v4l2ctd_info,
				struct page *page);

inline void *v4l2ctd_kmalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size,
			gfp_t gfp_mask);

inline void *v4l2ctd_kzalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size,
			gfp_t gfp_mask);

inline void v4l2ctd_kfree(struct v4l2ctd_info *v4l2ctd_info,
			void *ptr);

inline void *v4l2ctd_vmalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size);

inline void *v4l2ctd_vzalloc(struct v4l2ctd_info *v4l2ctd_info,
			size_t size);

inline void v4l2ctd_vfree(struct v4l2ctd_info *v4l2ctd_info,
			void *ptr);

#endif
