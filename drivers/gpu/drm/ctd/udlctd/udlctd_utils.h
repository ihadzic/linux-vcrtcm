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

#ifndef __UDLCTD_UTILS_H__
#define __UDLCTD_UTILS_H__

#include <linux/console.h>
#include "udlctd.h"

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

inline struct page *udlctd_alloc_page(struct udlctd_info *udlctd_info,
				gfp_t gfp_mask);

inline void udlctd_free_page(struct udlctd_info *udlctd_info,
				struct page *page);

inline void *udlctd_kmalloc(struct udlctd_info *udlctd_info,
			size_t size,
			gfp_t gfp_mask);

inline void *udlctd_kzalloc(struct udlctd_info *udlctd_info,
			size_t size,
			gfp_t gfp_mask);

inline void udlctd_kfree(struct udlctd_info *udlctd_info,
			void *ptr);

inline void *udlctd_vmalloc(struct udlctd_info *udlctd_info,
			size_t size);

inline void *udlctd_vzalloc(struct udlctd_info *udlctd_info,
			size_t size);

inline void udlctd_vfree(struct udlctd_info *udlctd_info,
			void *ptr);
#endif
