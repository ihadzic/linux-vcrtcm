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

#ifndef __v4l2pim_UTILS_H__
#define __v4l2pim_UTILS_H__

extern int debug;

#define PR_INFO(fmt, args...) \
	do { \
		printk(KERN_INFO "[" KBUILD_MODNAME "]: " fmt, ## args); \
	} while (0)

#define PR_ERR(fmt, args...) \
	printk(KERN_ERR "[" KBUILD_MODNAME "]: " fmt, ## args)
#define PR_WARN(fmt, args...) \
	printk(KERN_WARNING "[" KBUILD_MODNAME "]: " fmt, ## args)
#define PR_DEBUG(fmt, args...) \
	do { \
		if (unlikely(debug > 0)) \
			printk(KERN_INFO "[" KBUILD_MODNAME "]: " \
					fmt, ## args); \
	} while (0)

int v4l2pim_alloc_multiple_pages(struct v4l2pim_info *v4l2pim_info,
					gfp_t gfp_mask,
					struct page **page_array,
					unsigned int num_pages);

void v4l2pim_free_multiple_pages(struct v4l2pim_info *v4l2pim_info,
					struct page **page_array,
					unsigned int num_pages);

inline struct page *v4l2pim_alloc_page(struct v4l2pim_info *v4l2pim_info,
				gfp_t gfp_mask);

inline void v4l2pim_free_page(struct v4l2pim_info *v4l2pim_info,
				struct page *page);

inline void *v4l2pim_kmalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size,
			gfp_t gfp_mask);

inline void *v4l2pim_kzalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size,
			gfp_t gfp_mask);

inline void v4l2pim_kfree(struct v4l2pim_info *v4l2pim_info,
			void *ptr);

inline void *v4l2pim_vmalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size);

inline void *v4l2pim_vzalloc(struct v4l2pim_info *v4l2pim_info,
			size_t size);

inline void v4l2pim_vfree(struct v4l2pim_info *v4l2pim_info,
			void *ptr);
#endif
