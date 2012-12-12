/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Authors:
 *      Ilija Hadzic <ihadzic@research.bell-labs.com>
 *      Martin Carroll <martin.carroll@research.bell-labs.com>
 *      William Katsak <wkatsak@cs.rutgers.edu>
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
#include <linux/fs.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include "vcrtcm_utils_priv.h"

int vcrtcm_id_generator_init(struct vcrtcm_id_generator *gen, int num_ids)
{
	int num_chunks = (num_ids / VCRTCM_ID_GEN_MASK_LEN_BITS) + 1;
	void *ptr;
	int i = 0;

	if (!gen)
		return -EINVAL;

	ptr = vcrtcm_kmalloc(sizeof(VCRTCM_ID_GEN_MASK_TYPE) * num_chunks,
			     GFP_KERNEL, VCRTCM_OWNER_VCRTCM);
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


