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

#include <linux/slab.h>
#include <vcrtcm/vcrtcm_utils.h>

int vcrtcm_id_generator_init(struct vcrtcm_id_generator *gen, int num_ids)
{
	int num_chunks = (num_ids / VCRTCM_ID_GEN_MASK_LEN_BITS) + 1;
	void *ptr;
	int i = 0;

	if (!gen)
		return -EINVAL;

	ptr = kmalloc(sizeof(VCRTCM_ID_GEN_MASK_TYPE) * num_chunks, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	gen->used_ids = ptr;
	for (i = 0; i < num_chunks; i++)
		gen->used_ids[i] = (VCRTCM_ID_GEN_MASK_TYPE) 0;
	gen->num_ids = num_ids;
	gen->used_count = 0;

	return 0;
}
EXPORT_SYMBOL(vcrtcm_id_generator_init);

void vcrtcm_id_generator_destroy(struct vcrtcm_id_generator *gen)
{
	if (!gen)
		return;

	kfree(gen->used_ids);

	gen->num_ids = 0;
	gen->used_count = 0;

	return;
}
EXPORT_SYMBOL(vcrtcm_id_generator_destroy);

int vcrtcm_id_generator_get(struct vcrtcm_id_generator *gen)
{
	int i, j;
	int num_chunks = (gen->num_ids / VCRTCM_ID_GEN_MASK_LEN_BITS) + 1;

	if (!gen)
		return -EINVAL;
	if (gen->used_count == gen->num_ids)
		return -EBUSY;

	for (i = 0; i < num_chunks; i++) {
		for (j = 0; j < VCRTCM_ID_GEN_MASK_LEN_BITS; j++) {
			if (!(gen->used_ids[i] & (1 << j))) {
				gen->used_ids[i] |= (1 << j);
				gen->used_count++;
				return (i*VCRTCM_ID_GEN_MASK_LEN_BITS) + j;
			}
		}
	}

	return -EBUSY;
}
EXPORT_SYMBOL(vcrtcm_id_generator_get);

void vcrtcm_id_generator_put(struct vcrtcm_id_generator *gen, int id)
{
	int chunk = id / VCRTCM_ID_GEN_MASK_LEN_BITS;
	int offset = id % VCRTCM_ID_GEN_MASK_LEN_BITS;

	if (!gen)
		return;

	gen->used_ids[chunk] &= ~(1 << offset);
	gen->used_count--;

	return;
}
EXPORT_SYMBOL(vcrtcm_id_generator_put);

int vcrtcm_alloc_multiple_pages(gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages,
				atomic_t *page_track)
{
	struct page *current_page;
	int i;

	for (i = 0; i < num_pages; i++) {
		current_page = vcrtcm_alloc_page(gfp_mask, page_track);
		if (current_page) {
			page_array[i] = current_page;
		} else {
			vcrtcm_free_multiple_pages(page_array, i, page_track);
			return -ENOMEM;
		}
	}
	return 0;
}
EXPORT_SYMBOL(vcrtcm_alloc_multiple_pages);

void vcrtcm_free_multiple_pages(struct page **page_array,
				unsigned int num_pages,
				atomic_t *page_track)
{
	int i;

	for (i = 0; i < num_pages; i++)
		vcrtcm_free_page(page_array[i], page_track);
}
EXPORT_SYMBOL(vcrtcm_free_multiple_pages);

struct page *vcrtcm_alloc_page(gfp_t gfp_mask, atomic_t *page_track)
{
	struct page *page = alloc_page(gfp_mask);
	if (page)
		atomic_inc(page_track);
	return page;
}
EXPORT_SYMBOL(vcrtcm_alloc_page);

void vcrtcm_free_page(struct page *page, atomic_t *page_track)
{
	__free_page(page);
	atomic_dec(page_track);
}
EXPORT_SYMBOL(vcrtcm_free_page);

void *vcrtcm_kmalloc(size_t size, gfp_t gfp_mask, atomic_t *kmalloc_track)
{
	void *ptr = kmalloc(size, gfp_mask);
	if (ptr)
		atomic_inc(kmalloc_track);
	return ptr;
}
EXPORT_SYMBOL(vcrtcm_kmalloc);

void *vcrtcm_kzalloc(size_t size, gfp_t gfp_mask, atomic_t *kmalloc_track)
{
	void *ptr = kzalloc(size, gfp_mask);
	if (ptr)
		atomic_inc(kmalloc_track);
	return ptr;
}
EXPORT_SYMBOL(vcrtcm_kzalloc);

void vcrtcm_kfree(void *ptr, atomic_t *kmalloc_track)
{
	kfree(ptr);
	atomic_dec(kmalloc_track);
}
EXPORT_SYMBOL(vcrtcm_kfree);

void *vcrtcm_vmalloc(size_t size, atomic_t *vmalloc_track)
{
	void *ptr = vmalloc(size);
	if (ptr)
		atomic_inc(vmalloc_track);
	return ptr;
}
EXPORT_SYMBOL(vcrtcm_vmalloc);

void *vcrtcm_vzalloc(size_t size, atomic_t *vmalloc_track)
{
	void *ptr = vzalloc(size);
	if (ptr)
		atomic_inc(vmalloc_track);
	return ptr;
}
EXPORT_SYMBOL(vcrtcm_vzalloc);

void vcrtcm_vfree(void *ptr, atomic_t *vmalloc_track)
{
	vfree(ptr);
	atomic_dec(vmalloc_track);
}
EXPORT_SYMBOL(vcrtcm_vfree);
