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
#include <linux/spinlock.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_utils.h>
#include "vcrtcm_alloc_priv.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_pim_table.h"

#define VCRTCM_ALLOC_LOG_MAXLEN 64

static int vcrtcm_alloc_cnt;
static int vcrtcm_page_alloc_cnt;
static uint64_t ncalls_kmalloc;
static uint64_t ncalls_kzalloc;
static uint64_t ncalls_kfree;
static uint64_t ncalls_allocpage;
static uint64_t ncalls_freepage;
static uint64_t ncalls_allocmultiplepage;
static uint64_t ncalls_freemultiplepage;
static int log_alloc_bugs;
static int log_all_vcrtcm_counts;
static int log_all_pim_counts;
static int log_all_pcon_counts;
static DEFINE_SPINLOCK(spinlock);

static void show_ncalls(void)
{
	VCRTCM_INFO("ncalls_kmalloc = %llu\n", ncalls_kmalloc);
	VCRTCM_INFO("ncalls_kzalloc = %llu\n", ncalls_kzalloc);
	VCRTCM_INFO("ncalls_kfree = %llu\n", ncalls_kfree);
	VCRTCM_INFO("ncalls_allocpage = %llu\n", ncalls_allocpage);
	VCRTCM_INFO("ncalls_freepage = %llu\n", ncalls_freepage);
	VCRTCM_INFO("ncalls_allocmultiplepage = %llu\n", ncalls_allocmultiplepage);
	VCRTCM_INFO("ncalls_freemultiplepage = %llu\n", ncalls_freemultiplepage);
}

static void doadjcnts(const char *fcn, int owner_type, int owner_id,
	const char *pim_name, int *cnt, int *page_cnt,
	int incr, int ispage, int log_this_owners_cnts, int *log_bugs)
{
	int gotbug = 0;
	int log_this = 0;
	char owner_log_buf[VCRTCM_ALLOC_LOG_MAXLEN];

	if (incr) {
		++*cnt;
		if (ispage)
			++*page_cnt;
	} else {
		--*cnt;
		if (ispage)
			--*page_cnt;
	}
	switch (owner_type) {
	case VCRTCM_OWNER_VCRTCM:
		log_this = log_all_vcrtcm_counts;
		snprintf(owner_log_buf, VCRTCM_ALLOC_LOG_MAXLEN, "vcrtcm");
		break;
	case VCRTCM_OWNER_PIM:
		log_this = log_all_pim_counts || log_this_owners_cnts;
		snprintf(owner_log_buf, VCRTCM_ALLOC_LOG_MAXLEN, "pim %s",
			pim_name);
		break;
	case VCRTCM_OWNER_PCON:
		log_this = log_all_pcon_counts || log_this_owners_cnts;
		snprintf(owner_log_buf, VCRTCM_ALLOC_LOG_MAXLEN, "pcon %d",
			owner_id);
		break;
	default:
		BUG();
	}
	if (log_this)
		VCRTCM_INFO("%s: alloc count for %s is %d/%d\n",
			    fcn, owner_log_buf, *cnt, *page_cnt);
	if (!incr && *cnt == 0)
		VCRTCM_INFO("%s freed its last allocation\n", owner_log_buf);
	if (*log_bugs) {
		if (*cnt < 0 || *page_cnt < 0) {
			VCRTCM_ERROR("%s alloc count negative (%d/%d)\n",
				     owner_log_buf, *cnt, *page_cnt);
			gotbug = 1;
		}
		if (*cnt < *page_cnt) {
			VCRTCM_ERROR("%s alloc count inconsistent (%d/%d)\n",
				     owner_log_buf, *cnt, *page_cnt);
			gotbug = 1;
		}
	}
	if (gotbug) {
		VCRTCM_ERROR("%s suppressing further alloc bug reports\n",
			     owner_log_buf);
		*log_bugs = 0;
		show_ncalls();
		dump_stack();
	}
}

static int adjcnts(const char *fcn, uint32_t owner, int incr, int ispage)
{
	/*
	 * NB: check the VCRTCM_OWNER_PIM bit before checking the
	 * VCRTCM_OWNER_VCRTCM bit, because a common pim bug is for
	 * the pim to specify an owner of (VCRTCM_OWNER_PIM | -1),
	 * which has both of those bits set, and we want to treat
	 * that value as VCRTCM_OWNER_PIM.
	 */
	if (owner & VCRTCM_OWNER_PIM) {
		int pimid = owner & ~VCRTCM_OWNER_PIM;
		struct vcrtcm_pim *pim = vcrtcm_get_pim(pimid);
		if (pim) {
			doadjcnts(fcn, VCRTCM_OWNER_PIM, pimid, pim->name,
				  &pim->alloc_cnt, &pim->page_alloc_cnt,
				  incr, ispage, pim->log_alloc_cnts,
				  &pim->log_alloc_bugs);
			return 0;
		}
		dump_stack();
		VCRTCM_ERROR("%s: pim %d does not exist, unable to %s\n",
			     fcn, pimid, incr ? "increment" : "decrement");
		return -ENODEV;
	} else if (owner & VCRTCM_OWNER_VCRTCM) {
		doadjcnts(fcn, VCRTCM_OWNER_VCRTCM, -1, NULL,
			  &vcrtcm_alloc_cnt, &vcrtcm_page_alloc_cnt,
			  incr, ispage, 0, &log_alloc_bugs);
		return 0;
	} else {
		int pconid = owner & ~VCRTCM_OWNER_PCON;
		struct vcrtcm_pcon *pcon = vcrtcm_get_pcon(pconid);
		if (pcon) {
			doadjcnts(fcn, VCRTCM_OWNER_PCON, pconid,
				  pcon->pim->name, &pcon->alloc_cnt,
				  &pcon->page_alloc_cnt, incr, ispage,
				  pcon->log_alloc_cnts, &pcon->log_alloc_bugs);
			return 0;
		}
		dump_stack();
		VCRTCM_ERROR("%s: pcon %d does not exist, unable to %s\n",
			     fcn, pconid, incr ? "increment" : "decrement");
		return -ENODEV;
	}
}

struct page *vcrtcm_alloc_page(gfp_t gfp_mask, uint32_t owner)
{
	struct page *page;
	unsigned long flags;
	int r;

	page = alloc_page(gfp_mask);
	if (!page)
		return NULL;
	spin_lock_irqsave(&spinlock, flags);
	++ncalls_allocpage;
	r = adjcnts(__func__, owner, 1, 1);
	spin_unlock_irqrestore(&spinlock, flags);
	if (r) {
		__free_page(page);
		return NULL;
	}
	return page;
}
EXPORT_SYMBOL(vcrtcm_alloc_page);

void vcrtcm_free_page(struct page *page, uint32_t owner)
{
	unsigned long flags;

	if (!page)
		return;
	spin_lock_irqsave(&spinlock, flags);
	++ncalls_freepage;
	adjcnts(__func__, owner, 0, 1);
	spin_unlock_irqrestore(&spinlock, flags);
	__free_page(page);
}
EXPORT_SYMBOL(vcrtcm_free_page);

int vcrtcm_alloc_multiple_pages(gfp_t gfp_mask,
				struct page **page_array,
				unsigned int num_pages,
				uint32_t owner)
{
	struct page *current_page;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&spinlock, flags);
	++ncalls_allocmultiplepage;
	spin_unlock_irqrestore(&spinlock, flags);
	for (i = 0; i < num_pages; i++) {
		current_page = vcrtcm_alloc_page(gfp_mask, owner);
		if (current_page) {
			page_array[i] = current_page;
		} else {
			vcrtcm_free_multiple_pages(page_array, i, owner);
			return -ENOMEM;
		}
	}
	return 0;
}
EXPORT_SYMBOL(vcrtcm_alloc_multiple_pages);

void vcrtcm_free_multiple_pages(struct page **page_array,
				unsigned int num_pages, uint32_t owner)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&spinlock, flags);
	++ncalls_freemultiplepage;
	spin_unlock_irqrestore(&spinlock, flags);
	for (i = 0; i < num_pages; i++)
		vcrtcm_free_page(page_array[i], owner);
}
EXPORT_SYMBOL(vcrtcm_free_multiple_pages);

static void *finish_alloc(const char *fcn, void *ptr, uint32_t owner)
{
	unsigned long flags;
	int r;

	if (!ptr)
		return NULL;
	spin_lock_irqsave(&spinlock, flags);
	++ncalls_kmalloc;
	r = adjcnts(fcn, owner, 1, 0);
	spin_unlock_irqrestore(&spinlock, flags);
	if (r) {
		kfree(ptr);
		return NULL;
	}
	*(uint32_t *)ptr = owner;
	return ptr + sizeof(uint64_t);
}

void *vcrtcm_kmalloc(size_t size, gfp_t gfp_mask, uint32_t owner)
{
	void *ptr;

	ptr = kmalloc(size + sizeof(uint64_t), gfp_mask);
	return finish_alloc(__func__, ptr, owner);
}
EXPORT_SYMBOL(vcrtcm_kmalloc);

void *vcrtcm_kzalloc(size_t size, gfp_t gfp_mask, uint32_t owner)
{
	void *ptr;

	ptr = kzalloc(size + sizeof(uint64_t), gfp_mask);
	return finish_alloc(__func__, ptr, owner);
}
EXPORT_SYMBOL(vcrtcm_kzalloc);

void vcrtcm_kfree_decronly(void *ptr)
{
	unsigned long flags;
	uint32_t owner;

	if (!ptr)
		return;
	ptr -= sizeof(uint64_t);
	owner = *(uint32_t *)ptr;
	spin_lock_irqsave(&spinlock, flags);
	++ncalls_kfree;
	adjcnts(__func__, owner, 0, 0);
	spin_unlock_irqrestore(&spinlock, flags);
}
EXPORT_SYMBOL(vcrtcm_kfree_decronly);

void vcrtcm_kfree_freeonly(void *ptr)
{
	if (!ptr)
		return;
	ptr -= sizeof(uint64_t);
	kfree(ptr);
}
EXPORT_SYMBOL(vcrtcm_kfree_freeonly);

void vcrtcm_kfree(void *ptr)
{
	vcrtcm_kfree_decronly(ptr);
	vcrtcm_kfree_freeonly(ptr);
}
EXPORT_SYMBOL(vcrtcm_kfree);
