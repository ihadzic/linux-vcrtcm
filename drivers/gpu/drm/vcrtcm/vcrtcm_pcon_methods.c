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


#include <linux/module.h>
#include "vcrtcm_private.h"

/*
 * Callback from DMABUF when dma_buf object is attached and mapped
 * (typically as the result of registering the push buffer with PRIME).
 * Because PCON owns the buffer, it is responsible for mapping it
 * into GPU driver's space, which is taken careo of by this (common)
 * VCRTCM function.
 */
static struct sg_table
*vcrtcm_dma_buf_map(struct dma_buf_attachment *attachment,
		    enum dma_data_direction dir)
{
	struct vcrtcm_push_buffer_descriptor *pbd = attachment->dmabuf->priv;
	struct vcrtcm_pcon_info *pcon_info = pbd->owner_pcon;
	struct drm_crtc *crtc = pcon_info->drm_crtc;
	struct drm_device *dev = crtc->dev;
	struct sg_table *sg;
	int nents;

	mutex_lock(&dev->struct_mutex);
	sg = drm_prime_pages_to_sg(pbd->pages, pbd->num_pages);
	/* REVISIT: do something if nents is not equal to requested nents */
	nents = dma_map_sg(attachment->dev, sg->sgl, sg->nents, dir);
	mutex_unlock(&dev->struct_mutex);
	return sg;
}

/*
 * Callback from DMABUF when dma_buf object is detached and unmapped
 * (typically as the result of unregistering the push buffer with PRIME)
 * Because PCON owns the buffer, it is responsible for unmapping it
 * from GPU driver's space, which is taken care of by this (common)
 * VCRTCM function.
 */
static void vcrtcm_dma_buf_unmap(struct dma_buf_attachment *attachment,
				 struct sg_table *sg,
				 enum dma_data_direction dir)
{
	dma_unmap_sg(attachment->dev, sg->sgl, sg->nents, dir);
	sg_free_table(sg);
	kfree(sg);
}

/*
 * Callback from DMABUF when dma_buf object goes away.
 */
static void vcrtcm_dma_buf_release(struct dma_buf *dma_buf)
{
	struct vcrtcm_push_buffer_descriptor *pbd = dma_buf->priv;
	struct drm_gem_object *obj = pbd->gpu_private;

	kfree(pbd);
	if (obj->export_dma_buf == dma_buf) {
		VCRTCM_DEBUG("unreference obj %p\n", obj);
		obj->export_dma_buf = NULL;
		drm_gem_object_unreference_unlocked(obj);
	}
}

static void *vcrtcm_dma_buf_kmap(struct dma_buf *dma_buf,
				 unsigned long page_num)
{
	return NULL;
}

static void *vcrtcm_dma_buf_kmap_atomic(struct dma_buf *dma_buf,
					unsigned long page_num)
{
	return NULL;
}

static void vcrtcm_dma_buf_kunmap(struct dma_buf *dma_buf,
				  unsigned long page_num, void *addr)
{
}

static void vcrtcm_dma_buf_kunmap_atomic(struct dma_buf *dma_buf,
					 unsigned long page_num, void *addr)
{
}

static int vcrtcm_dma_buf_mmap(struct dma_buf *dma_buf,
			       struct vm_area_struct *vma)
{
	return -EINVAL;
}

static void *vcrtcm_dma_buf_vmap(struct dma_buf *dma_buf)
{
	return NULL;
}

static void vcrtcm_dma_buf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
}

static const struct dma_buf_ops vcrtcm_dma_buf_ops = {
	.map_dma_buf = vcrtcm_dma_buf_map,
	.unmap_dma_buf = vcrtcm_dma_buf_unmap,
	.release = vcrtcm_dma_buf_release,
	.kmap = vcrtcm_dma_buf_kmap,
	.kmap_atomic = vcrtcm_dma_buf_kmap_atomic,
	.kunmap = vcrtcm_dma_buf_kunmap,
	.kunmap_atomic = vcrtcm_dma_buf_kunmap_atomic,
	.mmap = vcrtcm_dma_buf_mmap,
	.vmap = vcrtcm_dma_buf_vmap,
	.vunmap = vcrtcm_dma_buf_vunmap
};

/*
 * called by PCON to register the push buffer with PRIME infrastructure.
 * PCON must allocate the backing store for the push buffer and VCRTCM
 * takes care of turning that into a GEM object that GPU can use
 * as copy destination. Populates the dma_buf and gpu_private fields
 * of the push buffer descriptor (pbd) that are not known by PCON
 * before this function is called. Also called by vcrtcm_p_alloc_pb,
 * which is used by PCONs whose backing store is in main memory.
 */
int vcrtcm_p_register_prime(struct vcrtcm_pcon_info *pcon_info,
			    struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct drm_crtc *crtc = pcon_info->drm_crtc;
	struct drm_device *dev = crtc->dev;
	struct dma_buf *dma_buf;
	int r = 0;
	int size = PAGE_SIZE * pbd->num_pages;
	struct drm_gem_object *obj;

	dma_buf = dma_buf_export(pbd, &vcrtcm_dma_buf_ops,
				 size, VCRTCM_DMA_BUF_PERMS);
	if (IS_ERR(dma_buf)) {
		r = PTR_ERR(dma_buf);
		goto out_err0;
	}
	pbd->dma_buf = dma_buf;
	/* this will cause a callback into vcrtcm_dma_buf_map */
	obj = dev->driver->gem_prime_import(dev, dma_buf);
	if (IS_ERR(obj)) {
		r = PTR_ERR(obj);
		goto out_err1;
	}
	pbd->gpu_private = obj;
	VCRTCM_DEBUG("pcon %i push buffer GEM object name=%d, size=%d\n",
		     pcon_info->pconid,
		     obj->name, obj->size);
	return r;

out_err1:
	dma_buf_put(dma_buf);
out_err0:
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_register_prime);

/*
 * Called by PCON to unregister the push buffer with PRIME infrastructure.
 * Typically PCON calls this function when freeing the push buffer or
 * replacing a pre-existing push buffer with a different one (i.e.,
 * of different size). Also called by vcrtcm_p_free_pb, which is called
 * by PCONs whose backing store is in main memory.
 */
void vcrtcm_p_unregister_prime(struct vcrtcm_pcon_info *pcon_info,
			       struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct drm_crtc *crtc = pcon_info->drm_crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *obj = pbd->gpu_private;

	VCRTCM_DEBUG("pcon %i freeing GEM object name=%d, size=%d\n",
		     pcon_info->pconid,
		     obj->name, obj->size);
	/*
	 * This call is magic: It will free the GEM object, which will
	 * result in a call to drm_prime_gem_destroy (assuming that there
	 * is PRIME import associated with the object), which in turn
	 * will call vcrtcm_dma_buf_unmap to cleanup the mapping. It will
	 * also destroy the dma_buf object if the last attachment is
	 * dropped (which is de-facto always, because in VCRTCM there
	 * is always only one attachment.
	 */
	dev->driver->gem_free_object(obj);
}
EXPORT_SYMBOL(vcrtcm_p_unregister_prime);

int vcrtcm_del_pcon(int pconid)
{
	struct vcrtcm_pcon_info *pcon_info;
	int r = 0;

	/* find the entry that should be removed */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry(pcon_info, &vcrtcm_pcon_list, list) {
		if (pcon_info->pconid == pconid) {
			unsigned long flags;
			mutex_lock(&pcon_info->mutex);
			VCRTCM_INFO("found an existing pcon %i "
				    "removing\n",
				    pcon_info->pconid);
			spin_lock_irqsave(&pcon_info->lock, flags);
			if (pcon_info->status & VCRTCM_STATUS_PCON_IN_USE) {
				pcon_info->status &= ~VCRTCM_STATUS_PCON_IN_USE;
				spin_unlock_irqrestore(&pcon_info->lock,
						       flags);
				VCRTCM_INFO("pcon in use by CRTC %p, "
					    "forcing detach\n",
					    pcon_info->drm_crtc);
				if (pcon_info->funcs.detach) {
					r = pcon_info->funcs.detach(pcon_info);
					if (r) {
						VCRTCM_ERROR("could not force detach on CRTC %p\n",
							pcon_info->drm_crtc);
						spin_lock_irqsave(&pcon_info->lock, flags);
						pcon_info->status
							|= VCRTCM_STATUS_PCON_IN_USE;
						spin_unlock_irqrestore(&pcon_info->lock, flags);
						mutex_unlock(&pcon_info->mutex);
						mutex_unlock(&vcrtcm_pcon_list_mutex);
						return r;
					}
				}
				if (pcon_info->gpu_funcs.detach)
					pcon_info->gpu_funcs.detach(pcon_info->drm_crtc);
			} else
				spin_unlock_irqrestore(&pcon_info->lock, flags);
			list_del(&pcon_info->list);
			mutex_unlock(&pcon_info->mutex);
			vcrtcm_dealloc_pcon_info(pcon_info->pconid);
			mutex_unlock(&vcrtcm_pcon_list_mutex);
			return 0;
		}
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	/*
	 * if we got here, then the caller is attempting to remove something
	 * that does not exist
	 */
	VCRTCM_WARNING("requested pcon %i not found\n", pconid);
	return -EINVAL;
}

/*
 * The PCON can use this function wait for the GPU to finish rendering
 * to the frame.  PCONs typically call this to prevent frame tearing.
 */
void vcrtcm_p_wait_fb(struct vcrtcm_pcon_info *pcon_info)
{
	unsigned long jiffies_snapshot, jiffies_snapshot_2;

	VCRTCM_INFO("waiting for GPU pcon %i\n",
		    pcon_info->pconid);
	jiffies_snapshot = jiffies;
	if (pcon_info->gpu_funcs.wait_fb)
		pcon_info->gpu_funcs.wait_fb(pcon_info->drm_crtc);
	jiffies_snapshot_2 = jiffies;

	VCRTCM_INFO("time spent waiting for GPU %d ms\n",
		    ((int)jiffies_snapshot_2 -
		     (int)(jiffies_snapshot)) * 1000 / HZ);

}
EXPORT_SYMBOL(vcrtcm_p_wait_fb);

/*
 * called by the PCON to emulate vblank
 * this is the link between the vblank event that happened in
 * the PCON but is emulated by the virtual
 * CRTC implementation in the GPU-hardware-specific driver
 */
void vcrtcm_p_emulate_vblank(struct vcrtcm_pcon_info *pcon_info)
{
	unsigned long flags;

	spin_lock_irqsave(&pcon_info->lock, flags);
	if (!pcon_info->status & VCRTCM_STATUS_PCON_IN_USE) {
		/* someone pulled the rug under our feet, bail out */
		spin_unlock_irqrestore(&pcon_info->lock, flags);
		return;
	}
	do_gettimeofday(&pcon_info->vblank_time);
	pcon_info->vblank_time_valid = 1;
	spin_unlock_irqrestore(&pcon_info->lock, flags);
	if (pcon_info->gpu_funcs.vblank) {
		VCRTCM_DEBUG("emulating vblank event for pcon %i\n", pcon_info->pconid);
		pcon_info->gpu_funcs.vblank(pcon_info->drm_crtc);
	}
}
EXPORT_SYMBOL(vcrtcm_p_emulate_vblank);

/*
 * called by the PCON to request GPU push of the
 * frame buffer pixels; pushes the frame buffer associated with
 * the ctrc that is attached to the specified hal into the push buffer
 * defined by pbd
 */
int vcrtcm_p_push(struct vcrtcm_pcon_info *pcon_info,
		struct vcrtcm_push_buffer_descriptor *fpbd,
		struct vcrtcm_push_buffer_descriptor *cpbd)
{
	struct drm_crtc *crtc = pcon_info->drm_crtc;
	struct drm_gem_object *push_buffer_fb = NULL;
	struct drm_gem_object *push_buffer_cursor = NULL;

	if (cpbd) {
		push_buffer_cursor = cpbd->gpu_private;
		cpbd->virgin = 0;
	}
	if (fpbd) {
		push_buffer_fb = fpbd->gpu_private;
		fpbd->virgin = 0;
	}
	if (pcon_info->gpu_funcs.push) {
		VCRTCM_DEBUG("push for pcon %i\n",
			     pcon_info->pconid);
		return pcon_info->gpu_funcs.push(crtc,
			push_buffer_fb, push_buffer_cursor);
	} else
		return -ENOTSUPP;
}
EXPORT_SYMBOL(vcrtcm_p_push);

/*
 * called by the PCON to signal hotplug event on a CRTC
 * attached to the specified PCON
 */
void vcrtcm_p_hotplug(struct vcrtcm_pcon_info *pcon_info)
{
	struct drm_crtc *crtc = pcon_info->drm_crtc;

	if (pcon_info->gpu_funcs.hotplug) {
		pcon_info->gpu_funcs.hotplug(crtc);
		VCRTCM_DEBUG("pcon %i hotplug\n", pcon_info->pconid);

	}
}
EXPORT_SYMBOL(vcrtcm_p_hotplug);

/*
 * Called by PCONs whose push-buffer backing store is in main
 * memory to allocate push buffers. This function does the opposite
 * of vcrtcm_p_alloc_pb
 */
void vcrtcm_p_free_pb(struct vcrtcm_pcon_info *pcon_info,
		      struct vcrtcm_push_buffer_descriptor *pbd,
		      atomic_t *kmalloc_track, atomic_t *page_track)
{
	if (pbd) {
		BUG_ON(!pbd->gpu_private);
		BUG_ON(!pbd->num_pages);
		vcrtcm_p_unregister_prime(pcon_info, pbd);
		vcrtcm_free_multiple_pages(pbd->pages, pbd->num_pages,
					   page_track);
		vcrtcm_kfree(pbd->pages, kmalloc_track);
		/*
		 * FIXME (ugly hack): kfree(pbd) happens in
		 * vcrtcm_dma_buf_release() function as a consequence
		 * of vcrtcm_p_unregister_prime() call above (asynchronous
		 * callback on thread level that gets called when DMABUF
		 * subsystem drops the reference to the file descriptor).
		 * However, we can't call vcrtcm_kfree() there because
		 * at that point we no longer know from which PCON
		 * the buffer belonged to. So we decrement the track
		 * counter here and do the actual free in the callback.
		 * It's ugly, but pragmatic. Revisit this later and figure out
		 * a more elegant way to handle this.
		 */
		atomic_dec(kmalloc_track);
	}
}
EXPORT_SYMBOL(vcrtcm_p_free_pb);

/*
 * Called by PCONs whose push-buffer backing store is in main
 * memory to allocate push buffers. This function creates the descriptor
 * allocates pages and calls vcrtcm_p_register_prime. Vast majority
 * of PCONs can safely use this function, but some (typically those
 * who want the backing store at the place other than main memory)
 * may have to cut their own allocation function.
 */
struct vcrtcm_push_buffer_descriptor *
vcrtcm_p_alloc_pb(struct vcrtcm_pcon_info *pcon_info, int npages,
		  gfp_t gfp_mask, atomic_t *kmalloc_track,
		  atomic_t *page_track)
{
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd;

	pbd = vcrtcm_kzalloc(sizeof(struct vcrtcm_push_buffer_descriptor),
			    GFP_KERNEL, kmalloc_track);
	pbd->owner_pcon = pcon_info;
	pbd->virgin = 1;
	if (!pbd) {
		VCRTCM_ERROR("push buffer descriptor alloc failed\n");
		r = -ENOMEM;
		goto out_err0;
	}
	pbd->pages = vcrtcm_kzalloc(npages * sizeof(struct page *), GFP_KERNEL,
				   kmalloc_track);
	if (!pbd->pages) {
		VCRTCM_ERROR("pages pointer alloc failed\n");
		r = -ENOMEM;
		goto out_err1;
	}
	r = vcrtcm_alloc_multiple_pages(gfp_mask, pbd->pages, npages,
					page_track);
	if (r) {
		VCRTCM_ERROR("push buffer pages alloc failed\n");
		goto out_err2;
	}
	pbd->num_pages = npages;
	r = vcrtcm_p_register_prime(pcon_info, pbd);
	if (r) {
		VCRTCM_ERROR("export to VCRTCM failed\n");
		goto out_err3;
	}
	return pbd;
out_err3:
	vcrtcm_free_multiple_pages(pbd->pages, npages, page_track);
out_err2:
	vcrtcm_kfree(pbd->pages, kmalloc_track);
out_err1:
	vcrtcm_kfree(pbd, kmalloc_track);
out_err0:
	return ERR_PTR(r);
}
EXPORT_SYMBOL(vcrtcm_p_alloc_pb);

/*
 * Called by PCONs when buffer resize occurs. This function only
 * gets us the new push buffer in main memory. If PCON needs to do
 * some PCON-specific mapping of the pages, it must make its own
 * arrangements to do that. It can either call this function and then
 * figure out if it needs to remap pages, or it can cut its own
 * realloc function using lower-level calls, such as
 * vcrtcm_p_alloc_pb and vcrtcm_p_free_pb
 */
struct vcrtcm_push_buffer_descriptor *
vcrtcm_p_realloc_pb(struct vcrtcm_pcon_info *pcon_info,
		    struct vcrtcm_push_buffer_descriptor *pbd, int npages,
		    gfp_t gfp_mask,
		    atomic_t *kmalloc_track, atomic_t *page_track)
{
	struct vcrtcm_push_buffer_descriptor *npbd;

	if (npages == 0) {
		VCRTCM_DEBUG("zero size requested\n");
		vcrtcm_p_free_pb(pcon_info, pbd, kmalloc_track, page_track);
		npbd = NULL;
	} else if (!pbd) {
		/* no old buffer present */
		npbd = vcrtcm_p_alloc_pb(pcon_info, npages, gfp_mask,
					 kmalloc_track, page_track);
	} else if (npages == pbd->num_pages) {
		/* can reuse existing pb */
		npbd = pbd;
	} else {
		VCRTCM_DEBUG("reallocating push buffer\n");
		vcrtcm_p_free_pb(pcon_info, pbd, kmalloc_track, page_track);
		npbd = vcrtcm_p_alloc_pb(pcon_info, npages, gfp_mask,
					 kmalloc_track, page_track);
	}
	return npbd;
}
EXPORT_SYMBOL(vcrtcm_p_realloc_pb);
