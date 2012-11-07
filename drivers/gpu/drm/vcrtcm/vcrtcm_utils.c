/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Author: Ilija Hadzic <ihadzic@research.bell-labs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Various utility functions for Virtual CRTC Manager and PIMs
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pim_table.h"

static atomic_t vcrtcm_alloc_cnt = ATOMIC_INIT(0);

int vcrtcm_id_generator_init(struct vcrtcm_id_generator *gen, int num_ids)
{
	int num_chunks = (num_ids / VCRTCM_ID_GEN_MASK_LEN_BITS) + 1;
	void *ptr;
	int i = 0;

	if (!gen)
		return -EINVAL;

	ptr = vcrtcm_kmalloc(sizeof(VCRTCM_ID_GEN_MASK_TYPE) * num_chunks, GFP_KERNEL, VCRTCM_OWNER_VCRTCM);
	if (!ptr)
		return -ENOMEM;

	gen->used_ids = ptr;
	for (i = 0; i < num_chunks; i++)
		gen->used_ids[i] = (VCRTCM_ID_GEN_MASK_TYPE) 0;
	gen->num_ids = num_ids;
	gen->used_count = 0;
	gen->increasing_pos = -1;
	mutex_init(&gen->mutex);

	return 0;
}
EXPORT_SYMBOL(vcrtcm_id_generator_init);

void vcrtcm_id_generator_destroy(struct vcrtcm_id_generator *gen)
{
	if (!gen)
		return;

	vcrtcm_kfree(gen->used_ids);

	gen->num_ids = 0;
	gen->used_count = 0;
	gen->increasing_pos = 0;

	return;
}
EXPORT_SYMBOL(vcrtcm_id_generator_destroy);

int vcrtcm_id_generator_get(struct vcrtcm_id_generator *gen, int behavior)
{
	int i, j;
	int num_chunks = 0;
	int start_id, start_chunk, start_offset;
	int new_id = -1;

	if (!gen)
		return -EINVAL;
	if (behavior != VCRTCM_ID_REUSE && behavior != VCRTCM_ID_INCREASING)
		return -EINVAL;

	mutex_lock(&gen->mutex);
	if (gen->used_count == gen->num_ids) {
		mutex_unlock(&gen->mutex);
		return -EBUSY;
	}

	if (behavior == VCRTCM_ID_INCREASING &&
			(gen->increasing_pos+1) < gen->num_ids) {
		start_id = gen->increasing_pos+1;
	} else if (behavior == VCRTCM_ID_INCREASING) {
		gen->increasing_pos = -1;
		start_id = 0;
	} else if (behavior == VCRTCM_ID_REUSE) {
		start_id = 0;
	}

	num_chunks = (gen->num_ids / VCRTCM_ID_GEN_MASK_LEN_BITS) + 1;
	start_chunk = start_id / VCRTCM_ID_GEN_MASK_LEN_BITS;
	start_offset = start_id % VCRTCM_ID_GEN_MASK_LEN_BITS;

	for (i = start_chunk; i < num_chunks; i++) {
		for (j = (i == start_chunk ? start_offset : 0);
				j < VCRTCM_ID_GEN_MASK_LEN_BITS;
				j++) {
			if (!(gen->used_ids[i] & (1 << j))) {
				gen->used_ids[i] |= (1 << j);
				gen->used_count++;
				new_id = (i*VCRTCM_ID_GEN_MASK_LEN_BITS) + j;
				if (gen->increasing_pos < new_id)
					gen->increasing_pos = new_id;
				mutex_unlock(&gen->mutex);
				return new_id;
			}
		}
	}
	mutex_unlock(&gen->mutex);
	return -EBUSY;
}
EXPORT_SYMBOL(vcrtcm_id_generator_get);

void vcrtcm_id_generator_put(struct vcrtcm_id_generator *gen, int id)
{
	int chunk = id / VCRTCM_ID_GEN_MASK_LEN_BITS;
	int offset = id % VCRTCM_ID_GEN_MASK_LEN_BITS;

	if (!gen)
		return;

	mutex_lock(&gen->mutex);
	gen->used_ids[chunk] &= ~(1 << offset);
	gen->used_count--;
	mutex_unlock(&gen->mutex);

	return;
}
EXPORT_SYMBOL(vcrtcm_id_generator_put);

int vcrtcm_alloc_multiple_pages(gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages,
				uint32_t owner)
{
	struct page *current_page;
	int i;

	for (i = 0; i < num_pages; i++) {
		current_page = vcrtcm_alloc_page(gfp_mask, owner);
		if (current_page) {
			page_array[i] = current_page;
		} else {
			vcrtcm_free_multiple_pages(page_array, i);
			return -ENOMEM;
		}
	}
	return 0;
}
EXPORT_SYMBOL(vcrtcm_alloc_multiple_pages);

void vcrtcm_free_multiple_pages(struct page **page_array,
				unsigned int num_pages)
{
	int i;

	for (i = 0; i < num_pages; i++)
		vcrtcm_free_page(page_array[i]);
}
EXPORT_SYMBOL(vcrtcm_free_multiple_pages);

static void incr_cnt(uint32_t owner)
{
	if (owner & VCRTCM_OWNER_VCRTCM)
		atomic_inc(&vcrtcm_alloc_cnt);
	else if (owner & VCRTCM_OWNER_PIM) {
		int pimid = owner & ~VCRTCM_OWNER_PIM;
		struct vcrtcm_pim *pim = vcrtcm_find_pim(pimid);
		if (pim)
			atomic_inc(&pim->alloc_cnt);
	} else {
		int pconid = owner & ~VCRTCM_OWNER_PCON;
		struct vcrtcm_pcon *pcon = vcrtcm_get_pcon(pconid);
		if (pcon)
			atomic_inc(&pcon->alloc_cnt);
	}
}

struct page *vcrtcm_alloc_page(gfp_t gfp_mask, uint32_t owner)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		incr_cnt(owner);
	return page;
}
EXPORT_SYMBOL(vcrtcm_alloc_page);

void vcrtcm_free_page(struct page *page)
{
	if (page) {
		__free_page(page);
	}
}
EXPORT_SYMBOL(vcrtcm_free_page);

void *vcrtcm_kmalloc(size_t size, gfp_t gfp_mask, uint32_t owner)
{
	void *ptr = kmalloc(size, gfp_mask);
	if (ptr)
		incr_cnt(owner);
	return ptr;
}
EXPORT_SYMBOL(vcrtcm_kmalloc);

void *vcrtcm_kzalloc(size_t size, gfp_t gfp_mask, uint32_t owner)
{
	void *ptr = kzalloc(size, gfp_mask);
	if (ptr)
		incr_cnt(owner);
	return ptr;
}
EXPORT_SYMBOL(vcrtcm_kzalloc);

void vcrtcm_kfree(void *ptr)
{
	if (ptr)
		kfree(ptr);
}
EXPORT_SYMBOL(vcrtcm_kfree);
