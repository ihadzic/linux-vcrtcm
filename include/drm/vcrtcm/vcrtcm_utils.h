/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Author: Ilija Hadzic <ihadzic@research.bell-labs.com>

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


/*
 * Various utility macros, constants and functions for Virtual CRTC
 * Manager and PIMs
 */

#ifndef __VCRTCM_UTILS_H__
#define __VCRTCM_UTILS_H__

#include <linux/module.h>

#ifdef MODULE
#define VCRTCM_NAME (THIS_MODULE->name)
#else
#define VCRTCM_NAME "????"
#endif

#define VCRTCM_INFO(fmt, args...)					\
	pr_info("[%s] " fmt, VCRTCM_NAME, ## args)

#define VCRTCM_ERROR(fmt, args...)					\
	pr_err("[%s:%s] " fmt, VCRTCM_NAME, __func__, ## args)

#define VCRTCM_WARNING(fmt, args...)					\
	pr_warn("[%s:%s] " fmt, VCRTCM_NAME, __func__, ## args)

#define VCRTCM_DBG(msg_level, current_level, fmt, args...)		\
	do {								\
		if (unlikely(current_level >= msg_level)) {		\
			printk(KERN_DEBUG "[%s:%s] " fmt,		\
			       VCRTCM_NAME, __func__, ## args);		\
		}							\
	} while (0)

#define VCRTCM_ID_GEN_MASK_TYPE uint32_t
#define VCRTCM_ID_GEN_MASK_LEN_BITS (sizeof(VCRTCM_ID_GEN_MASK_TYPE) * 8)

struct vcrtcm_id_generator {
	int num_ids;
	int used_count;
	uint64_t *used_ids;
	struct mutex mutex;
};

int vcrtcm_id_generator_init(struct vcrtcm_id_generator *gen, int num_ids);
void vcrtcm_id_generator_destroy(struct vcrtcm_id_generator *gen);
int vcrtcm_id_generator_get(struct vcrtcm_id_generator *gen);
void vcrtcm_id_generator_put(struct vcrtcm_id_generator *gen, int id);
int vcrtcm_alloc_multiple_pages(gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages,
				atomic_t *page_track);
void vcrtcm_free_multiple_pages(struct page **page_array,
				unsigned int num_pages,
				atomic_t *page_track);
struct page *vcrtcm_alloc_page(gfp_t gfp_mask, atomic_t *page_track);
void vcrtcm_free_page(struct page *page, atomic_t *page_track);
void *vcrtcm_kmalloc(size_t size, gfp_t gfp_mask, atomic_t *kmalloc_track);
void *vcrtcm_kzalloc(size_t size, gfp_t gfp_mask, atomic_t *kmalloc_track);
void vcrtcm_kfree(void *ptr, atomic_t *kmalloc_track);

#endif
