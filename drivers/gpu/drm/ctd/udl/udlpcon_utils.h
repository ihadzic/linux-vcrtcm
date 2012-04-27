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

#ifndef __UDLPCON_UTILS_H__
#define __UDLPCON_UTILS_H__

#include <linux/console.h>
#include "udlpcon.h"

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

int udlpcon_alloc_multiple_pages(struct udlpcon_info *udlpcon_info,
				gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages);

void udlpcon_free_multiple_pages(struct udlpcon_info *udlpcon_info,
				struct page **page_array,
				unsigned int num_pages);

inline struct page *udlpcon_alloc_page(struct udlpcon_info *udlpcon_info,
				gfp_t gfp_mask);

inline void udlpcon_free_page(struct udlpcon_info *udlpcon_info,
				struct page *page);

inline void *udlpcon_kmalloc(struct udlpcon_info *udlpcon_info,
			size_t size,
			gfp_t gfp_mask);

inline void *udlpcon_kzalloc(struct udlpcon_info *udlpcon_info,
			size_t size,
			gfp_t gfp_mask);

inline void udlpcon_kfree(struct udlpcon_info *udlpcon_info,
			void *ptr);

inline void *udlpcon_vmalloc(struct udlpcon_info *udlpcon_info,
			size_t size);

inline void *udlpcon_vzalloc(struct udlpcon_info *udlpcon_info,
			size_t size);

inline void udlpcon_vfree(struct udlpcon_info *udlpcon_info,
			void *ptr);
#endif
