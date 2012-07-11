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
	struct vcrtcm_pcon_info_private *pcon_info_private =
		container_of(pcon_info, struct vcrtcm_pcon_info_private,
			     pcon_info);
	struct drm_crtc *crtc = pcon_info_private->drm_crtc;
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
 * before this function is called.
 */
int vcrtcm_p_register_prime(struct vcrtcm_pcon_info *pcon_info,
			    struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct vcrtcm_pcon_info_private *pcon_info_private =
		container_of(pcon_info, struct vcrtcm_pcon_info_private,
			     pcon_info);
	struct drm_crtc *crtc = pcon_info_private->drm_crtc;
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
	VCRTCM_DEBUG("pcon %u push buffer GEM object name=%d, size=%d\n",
		     pcon_info_private->pcon_info.pconid,
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
 * of different size).
 */
void vcrtcm_p_unregister_prime(struct vcrtcm_pcon_info *pcon_info,
			       struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct vcrtcm_pcon_info_private *pcon_info_private =
		container_of(pcon_info, struct vcrtcm_pcon_info_private,
			     pcon_info);
	struct drm_crtc *crtc = pcon_info_private->drm_crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *obj = pbd->gpu_private;

	VCRTCM_DEBUG("pcon %u freeing GEM object name=%d, size=%d\n",
		     pcon_info_private->pcon_info.pconid,
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


/*
 * called by the pixel consumer (PCON) to
 * register itself implementation with vcrtcm; the PCON can also provide
 * pointers to back-end functions (functions that get called after
 * generic PCON function is executed)
 */
int vcrtcm_p_add(struct vcrtcm_pcon_funcs *pcon_funcs,
		  struct vcrtcm_pcon_props *pcon_props,
		  uint32_t pconid, void *pcon_cookie)
{
	struct vcrtcm_pcon_info_private *pcon_info_private;

	/* first check whether we are already registered */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry(pcon_info_private, &vcrtcm_pcon_list, list) {
		if (pcon_info_private->pcon_info.pconid == pconid) {
			/* if the PCON already exists, we just overwrite
			   the provided functions (assuming that the PCON
			   that called us knows what it's doing */
			mutex_lock(&pcon_info_private->pcon_info.mutex);
			VCRTCM_WARNING("found an existing pcon %u, "
				       "refreshing its implementation\n",
				       pcon_info_private->pcon_info.pconid);
			pcon_info_private->pcon_info.funcs = *pcon_funcs;
			pcon_info_private->pcon_info.props = *pcon_props;
			mutex_unlock(&pcon_info_private->pcon_info.mutex);
			mutex_unlock(&vcrtcm_pcon_list_mutex);
			return 0;
		}
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	/*
	 * If we got here, then we are dealing with a new implementation
	 * and we have to allocate and populate the PCON structure
	 */
	pcon_info_private =
		kmalloc(sizeof(struct vcrtcm_pcon_info_private), GFP_KERNEL);
	if (pcon_info_private == NULL)
		return -ENOMEM;

	/*
	 * populate the PCON structures (no need to hold the mutex
	 *  because no one else sees this structure yet)
	 */
	spin_lock_init(&pcon_info_private->lock);
	mutex_init(&pcon_info_private->pcon_info.mutex);
	pcon_info_private->pcon_info.funcs = *pcon_funcs;
	pcon_info_private->pcon_info.props = *pcon_props;

	/* populate the info structure and link it to the PCON structure */
	pcon_info_private->status = 0;
	pcon_info_private->pcon_info.pconid = pconid;
	pcon_info_private->pcon_info.pcon_cookie = pcon_cookie;
	pcon_info_private->vblank_time_valid = 0;
	pcon_info_private->vblank_time.tv_sec = 0;
	pcon_info_private->vblank_time.tv_usec = 0;
	pcon_info_private->drm_crtc = NULL;
	memset(&pcon_info_private->gpu_funcs, 0,
	       sizeof(struct vcrtcm_gpu_funcs));

	VCRTCM_INFO("adding new pcon %u\n",
		    pcon_info_private->pcon_info.pconid);

	/* make the new PCON available to the rest of the system */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_add(&pcon_info_private->list, &vcrtcm_pcon_list);
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_add);

/*
 * called by the pixel consumer (PCON)
 * (typically on exit) to unregister its implementation with PCON
 */
int vcrtcm_p_del(uint32_t pconid)
{
	struct vcrtcm_pcon_info_private *pcon_info_private;
	int r = 0;

	/* find the entry that should be removed */
	mutex_lock(&vcrtcm_pcon_list_mutex);
	list_for_each_entry(pcon_info_private, &vcrtcm_pcon_list, list) {
		if (pcon_info_private->pcon_info.pconid == pconid) {
			unsigned long flags;
			mutex_lock(&pcon_info_private->pcon_info.mutex);
			VCRTCM_INFO("found an existing pcon %u "
				    "removing\n",
				    pcon_info_private->pcon_info.pconid);
			spin_lock_irqsave(&pcon_info_private->lock, flags);
			if (pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
				pcon_info_private->status &= ~VCRTCM_STATUS_PCON_IN_USE;
				spin_unlock_irqrestore(&pcon_info_private->lock,
						       flags);
				VCRTCM_INFO("pcon in use by CRTC %p, "
					    "forcing detach\n",
					    pcon_info_private->drm_crtc);
				if (pcon_info_private->pcon_info.funcs.detach) {
					r = pcon_info_private->pcon_info.funcs.
						detach(&pcon_info_private->pcon_info);
					if (r) {
						VCRTCM_ERROR("could not force detach on CRTC %p\n",
							pcon_info_private->drm_crtc);
						spin_lock_irqsave(&pcon_info_private->lock, flags);
						pcon_info_private->status
							|= VCRTCM_STATUS_PCON_IN_USE;
						spin_unlock_irqrestore(&pcon_info_private->lock, flags);
						mutex_unlock(&pcon_info_private->pcon_info.mutex);
						mutex_unlock(&vcrtcm_pcon_list_mutex);
						return r;
					}
				}
				if (pcon_info_private->gpu_funcs.detach)
					pcon_info_private->gpu_funcs.
						detach(pcon_info_private->drm_crtc);
			} else
				spin_unlock_irqrestore(&pcon_info_private->lock,
						       flags);
			list_del(&pcon_info_private->list);
			mutex_unlock(&pcon_info_private->pcon_info.
				     mutex);
			kfree(pcon_info_private);
			mutex_unlock(&vcrtcm_pcon_list_mutex);
			return 0;
		}
	}
	mutex_unlock(&vcrtcm_pcon_list_mutex);

	/*
	 * if we got here, then the caller is attempting to remove something
	 * that does not exist
	 */
	VCRTCM_WARNING("requested pcon %u not found\n", pconid);
	return -EINVAL;
}
EXPORT_SYMBOL(vcrtcm_p_del);

/*
 * The PCON can use this function wait for the GPU to finish rendering
 * to the frame.  PCONs typically call this to prevent frame tearing.
 */
void vcrtcm_p_wait_fb(struct vcrtcm_pcon_info *pcon_info)
{
	struct vcrtcm_pcon_info_private *pcon_info_private =
	    container_of(pcon_info, struct vcrtcm_pcon_info_private,
			 pcon_info);
	unsigned long jiffies_snapshot, jiffies_snapshot_2;

	VCRTCM_INFO("waiting for GPU pcon %u\n",
		    pcon_info_private->pcon_info.pconid);
	jiffies_snapshot = jiffies;
	if (pcon_info_private->gpu_funcs.wait_fb)
		pcon_info_private->gpu_funcs.wait_fb(pcon_info_private->drm_crtc);
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
	struct vcrtcm_pcon_info_private *pcon_info_private =
	    container_of(pcon_info, struct vcrtcm_pcon_info_private,
			 pcon_info);
	unsigned long flags;

	spin_lock_irqsave(&pcon_info_private->lock, flags);
	if (!pcon_info_private->status & VCRTCM_STATUS_PCON_IN_USE) {
		/* someone pulled the rug under our feet, bail out */
		spin_unlock_irqrestore(&pcon_info_private->lock, flags);
		return;
	}
	do_gettimeofday(&pcon_info_private->vblank_time);
	pcon_info_private->vblank_time_valid = 1;
	spin_unlock_irqrestore(&pcon_info_private->lock, flags);
	if (pcon_info_private->gpu_funcs.vblank) {
		VCRTCM_DEBUG("emulating vblank event for pcon %u\n",
			     pcon_info_private->pcon_info.pconid);
		pcon_info_private->gpu_funcs.vblank(pcon_info_private->drm_crtc);
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
	struct vcrtcm_pcon_info_private *pcon_info_private =
		container_of(pcon_info, struct vcrtcm_pcon_info_private,
			     pcon_info);
	struct drm_crtc *crtc = pcon_info_private->drm_crtc;
	struct drm_gem_object *push_buffer_fb = fpbd->gpu_private;
	struct drm_gem_object *push_buffer_cursor = cpbd->gpu_private;

	if (pcon_info_private->gpu_funcs.push) {
		VCRTCM_DEBUG("push for pcon %u\n",
			     pcon_info_private->pcon_info.pconid);
		return pcon_info_private->gpu_funcs.push(crtc,
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
	struct vcrtcm_pcon_info_private *pcon_info_private =
		container_of(pcon_info, struct vcrtcm_pcon_info_private,
			     pcon_info);
	struct drm_crtc *crtc = pcon_info_private->drm_crtc;

	if (pcon_info_private->gpu_funcs.hotplug) {
		pcon_info_private->gpu_funcs.hotplug(crtc);
		VCRTCM_DEBUG("pcon %u hotplug\n",
			     pcon_info_private->pcon_info.pconid);

	}
}
EXPORT_SYMBOL(vcrtcm_p_hotplug);
