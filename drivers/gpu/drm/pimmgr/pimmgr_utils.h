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

#ifndef __PIMMGR_UTILS_H__
#define __PIMMGR_UTILS_H__

#include <linux/slab.h>
#include <linux/console.h>
#include "pimmgr.h"

extern int pimmgr_debug;

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
		if (unlikely(pimmgr_debug > 0)) \
			printk(KERN_INFO "[" KBUILD_MODNAME "]: " \
					fmt, ## args); \
	} while (0)

int pimmgr_alloc_multiple_pages(gfp_t gfp_mask, \
				struct page **page_array, \
				unsigned int num_pages);

void pimmgr_free_multiple_pages(struct page **page_array,
				unsigned int num_pages);

inline struct page *pimmgr_alloc_page(gfp_t gfp_mask);

inline void pimmgr_free_page(struct page *page);

inline void *pimmgr_kmalloc(size_t size, gfp_t gfp_mask);

inline void *pimmgr_kzalloc(size_t size, gfp_t gfp_mask);

inline void pimmgr_kfree(void *ptr);

inline void *pimmgr_vmalloc(size_t size);

inline void *pimmgr_vzalloc(size_t size);

inline void pimmgr_vfree(void *ptr);

#endif
