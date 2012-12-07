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
#include <linux/dma-buf.h>
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_gpu.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include "vcrtcm_pcon_methods.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_alloc_priv.h"
#include "vcrtcm_pcon.h"

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
	struct vcrtcm_pcon *pcon;
	struct drm_crtc *crtc;
	struct drm_device *dev;
	struct sg_table *sg;
	int nents;

	pcon = vcrtcm_get_pcon(pbd->pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pbd->pconid);
		return ERR_PTR(-ENODEV);
	}
	crtc = pcon->drm_crtc;
	dev = crtc->dev;
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

	vcrtcm_kfree_freeonly(pbd);
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
int vcrtcm_p_register_prime(int pconid,
			    struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct vcrtcm_pcon *pcon;
	struct drm_crtc *crtc;
	struct drm_device *dev;
	struct dma_buf *dma_buf;
	int r = 0;
	int size = PAGE_SIZE * pbd->num_pages;
	struct drm_gem_object *obj;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		r = -ENODEV;
		goto out_err0;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	crtc = pcon->drm_crtc;
	if (!crtc) {
		VCRTCM_ERROR("no crtc for pcon %d\n", pconid);
		r = -ENODEV;
		goto out_err0;
	}
	dev = crtc->dev;
	if (!dev) {
		VCRTCM_ERROR("no dev for pcon %d\n", pconid);
		r = -ENODEV;
		goto out_err0;
	}
	if (!dev->driver) {
		VCRTCM_ERROR("no driver for pcon %d\n", pconid);
		r = -ENODEV;
		goto out_err0;
	}
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
		     pconid, obj->name, obj->size);
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
int vcrtcm_p_unregister_prime(int pconid,
			       struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct vcrtcm_pcon *pcon;
	struct drm_crtc *crtc;
	struct drm_device *dev;
	struct drm_gem_object *obj = pbd->gpu_private;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	/* this function is legal to call on a pcon that is being destroyed */
	if (!obj) {
		VCRTCM_ERROR("no obj for pcon %d\n", pconid);
		return -ENODEV;
	}
	VCRTCM_DEBUG("pcon %i freeing GEM object name=%d, size=%d\n",
		     pconid, obj->name, obj->size);
	crtc = pcon->drm_crtc;
	if (!crtc) {
		VCRTCM_ERROR("no crtc for pcon %d\n", pconid);
		return -ENODEV;
	}
	dev = crtc->dev;
	if (!dev) {
		VCRTCM_ERROR("no dev for pcon %d\n", pconid);
		return -ENODEV;
	}
	if (!dev->driver) {
		VCRTCM_ERROR("no driver for pcon %d\n", pconid);
		return -ENODEV;
	}
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
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_unregister_prime);

/*
 * The PCON can use this function wait for the GPU to finish rendering
 * to the frame.  Push-mode pims must call this function before freeing
 * the frame's buffers, which typically occurs as part of the pcon-
 * destruction procedure.
 */
int vcrtcm_p_wait_fb(int pconid)
{
	unsigned long jiffies_snapshot, jiffies_snapshot_2;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	/* this function is legal to call on a pcon that is being destroyed */
	VCRTCM_INFO("waiting for GPU pcon 0x%08x\n", pconid);
	jiffies_snapshot = jiffies;
	if (pcon->gpu_funcs.wait_fb)
		pcon->gpu_funcs.wait_fb(pcon->pconid, pcon->drm_crtc);
	jiffies_snapshot_2 = jiffies;

	VCRTCM_INFO("time spent waiting for GPU %d ms\n",
		    ((int)jiffies_snapshot_2 -
		     (int)(jiffies_snapshot)) * 1000 / HZ);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_wait_fb);

/*
 * called by the PCON to emulate vblank
 * this is the link between the vblank event that happened in
 * the PCON but is emulated by the virtual
 * CRTC implementation in the GPU-hardware-specific driver
 */
int vcrtcm_p_emulate_vblank(int pconid)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	if (!pcon->drm_crtc)
		return -EINVAL;
	do_gettimeofday(&pcon->vblank_time);
	pcon->vblank_time_valid = 1;
	if (pcon->gpu_funcs.vblank) {
		VCRTCM_DEBUG("emulating vblank event for pcon %i\n", pconid);
		pcon->gpu_funcs.vblank(pcon->pconid, pcon->drm_crtc);
	}
	pcon->last_vblank_jiffies = jiffies;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_emulate_vblank);

/*
 * called by the PCON to request GPU push of the
 * frame buffer pixels; pushes the frame buffer associated with
 * the ctrc that is attached to the specified hal into the push buffer
 * defined by pbd
 */
int vcrtcm_p_push(int pconid,
		struct vcrtcm_push_buffer_descriptor *fpbd,
		struct vcrtcm_push_buffer_descriptor *cpbd)
{
	struct vcrtcm_pcon *pcon;
	struct drm_crtc *crtc;
	struct drm_gem_object *push_buffer_fb = NULL;
	struct drm_gem_object *push_buffer_cursor = NULL;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	crtc = pcon->drm_crtc;
	if (cpbd) {
		push_buffer_cursor = cpbd->gpu_private;
		cpbd->virgin = 0;
	}
	if (fpbd) {
		push_buffer_fb = fpbd->gpu_private;
		fpbd->virgin = 0;
	}
	if (pcon->gpu_funcs.push) {
		VCRTCM_DEBUG("push for pcon %i\n", pconid);
		return pcon->gpu_funcs.push(pcon->pconid, crtc,
			push_buffer_fb, push_buffer_cursor);
	} else
		return -ENOTSUPP;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_push);

/*
 * called by the PCON to signal hotplug event on a CRTC
 * attached to the specified PCON
 */
int vcrtcm_p_hotplug(int pconid)
{
	struct vcrtcm_pcon *pcon;
	struct drm_crtc *crtc;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	crtc = pcon->drm_crtc;
	if (pcon->gpu_funcs.hotplug) {
		pcon->gpu_funcs.hotplug(pcon->pconid, crtc);
		VCRTCM_DEBUG("pcon %i hotplug\n", pconid);

	}
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_hotplug);

/*
 * Called by PCONs whose push-buffer backing store is in main
 * memory to allocate push buffers. This function does the opposite
 * of vcrtcm_p_alloc_pb
 */
int vcrtcm_p_free_pb(int pconid,
		      struct vcrtcm_push_buffer_descriptor *pbd)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	/* this function is legal to call on a pcon that is being destroyed */
	if (pbd) {
		int r;

		BUG_ON(!pbd->gpu_private);
		BUG_ON(!pbd->num_pages);
		r = vcrtcm_p_unregister_prime(pconid, pbd);
		if (r) {
			VCRTCM_ERROR("unregister_prime failed for pcon %d\n",
				pconid);
			return r;
		}
		vcrtcm_free_multiple_pages(pbd->pages, pbd->num_pages,
					   VCRTCM_OWNER_PCON | pconid);
		vcrtcm_kfree(pbd->pages);
		/*
		 * FIXME (ugly hack): The pbd buffer cannot be freed yet
		 * because it is still in use by the DMABUF subsystem.
		 * The vcrtcm_p_unregister_prime() call above will cause
		 * DMABUF to drop the reference to the file descriptor,
		 * after which vcrtcm_dma_buf_release() will be called.
		 * However, we can't call vcrtcm_kfree() in
		 * vcrtcm_dma_buf_release() because at that point the
		 * PCON that owns the buffer will have already been
		 * destroyed.  So the workaround is to tell the alloc
		 * routines to decrement the alloc counters now and do
		 * the actual free in vcrtcm_dma_buf_release().
		 */
		vcrtcm_kfree_decronly(pbd);
	}
	return 0;
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
vcrtcm_p_alloc_pb(int pconid, int npages,
		  gfp_t gfp_mask)
{
	int r = 0;
	struct vcrtcm_push_buffer_descriptor *pbd;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		r = -ENODEV;
		goto out_err0;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		r = -EINVAL;
		goto out_err0;
	}
	pbd = vcrtcm_kzalloc(sizeof(struct vcrtcm_push_buffer_descriptor),
			    GFP_KERNEL, VCRTCM_OWNER_PCON | pconid);
	pbd->pconid = pconid;
	pbd->virgin = 1;
	if (!pbd) {
		VCRTCM_ERROR("push buffer descriptor alloc failed\n");
		r = -ENOMEM;
		goto out_err0;
	}
	pbd->pages = vcrtcm_kzalloc(npages * sizeof(struct page *), GFP_KERNEL,
				   VCRTCM_OWNER_PCON | pconid);
	if (!pbd->pages) {
		VCRTCM_ERROR("pages pointer alloc failed\n");
		r = -ENOMEM;
		goto out_err1;
	}
	r = vcrtcm_alloc_multiple_pages(gfp_mask, pbd->pages, npages,
					VCRTCM_OWNER_PCON | pconid);
	if (r) {
		VCRTCM_ERROR("push buffer pages alloc failed\n");
		goto out_err2;
	}
	pbd->num_pages = npages;
	r = vcrtcm_p_register_prime(pconid, pbd);
	if (r) {
		VCRTCM_ERROR("export to VCRTCM failed\n");
		goto out_err3;
	}
	return pbd;
out_err3:
	vcrtcm_free_multiple_pages(pbd->pages, npages,
				   VCRTCM_OWNER_PCON | pconid);
out_err2:
	vcrtcm_kfree(pbd->pages);
out_err1:
	vcrtcm_kfree(pbd);
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
vcrtcm_p_realloc_pb(int pconid,
		    struct vcrtcm_push_buffer_descriptor *pbd, int npages,
		    gfp_t gfp_mask)
{
	struct vcrtcm_push_buffer_descriptor *npbd;
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return NULL;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return NULL;
	}
	if (npages == 0) {
		VCRTCM_DEBUG("zero size requested\n");
		vcrtcm_p_free_pb(pconid, pbd);
		npbd = NULL;
	} else if (!pbd) {
		/* no old buffer present */
		npbd = vcrtcm_p_alloc_pb(pconid, npages, gfp_mask);
	} else if (npages == pbd->num_pages) {
		/* can reuse existing pb */
		npbd = pbd;
	} else {
		VCRTCM_DEBUG("reallocating push buffer\n");
		vcrtcm_p_free_pb(pconid, pbd);
		npbd = vcrtcm_p_alloc_pb(pconid, npages, gfp_mask);
	}
	return npbd;
}
EXPORT_SYMBOL(vcrtcm_p_realloc_pb);

/* this function is actually not in the vcrtcm-pim api,
 * but it could be added to that api, if needed.
 * NB: if you change the implementation of this function, you might
 * also need to change the implementation of do_vcrtcm_ioctl_detach_pcon()
 */
static void do_vcrtcm_p_detach(struct vcrtcm_pcon *pcon, int explicit)
{
	cancel_delayed_work_sync(&pcon->vblank_work);
	pcon->vblank_period_jiffies = 0;
	if (pcon->drm_crtc) {
		if (explicit)
			VCRTCM_INFO("detaching pcon %i\n", pcon->pconid);
		else
			VCRTCM_INFO("doing implicit detach of pcon %i\n",
				pcon->pconid);
		if (pcon->gpu_funcs.detach)
			pcon->gpu_funcs.detach(pcon->pconid, pcon->drm_crtc);
	}
}

int vcrtcm_p_detach(int pconid)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x being destroyed\n", pconid);
		return -EINVAL;
	}
	do_vcrtcm_p_detach(pcon, 1);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_detach);

void do_vcrtcm_p_destroy(struct vcrtcm_pcon *pcon, int explicit)
{
	unsigned long flags;

	do_vcrtcm_p_detach(pcon, 0);
	if (explicit)
		VCRTCM_INFO("destroying pcon %i\n", pcon->pconid);
	else
		VCRTCM_INFO("doing implicit destroy of pcon %i\n",
			pcon->pconid);
	spin_lock_irqsave(&pcon->page_flip_spinlock, flags);
	pcon->being_destroyed = 1;
	spin_unlock_irqrestore(&pcon->page_flip_spinlock, flags);
	vcrtcm_destroy_pcon(pcon);
}

int vcrtcm_p_destroy(int pconid)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x already being destroyed\n", pconid);
		return -EINVAL;
	}
	do_vcrtcm_p_destroy(pcon, 1);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_destroy);

int vcrtcm_p_disable_callbacks(int pconid)
{
	struct vcrtcm_pcon *pcon;
	unsigned long flags;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	/* this function is legal to call on a pcon that is being destroyed */
	spin_lock_irqsave(&pcon->page_flip_spinlock, flags);
	pcon->pcon_callbacks_enabled = 0;
	spin_unlock_irqrestore(&pcon->page_flip_spinlock, flags);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_disable_callbacks);

int vcrtcm_p_log_alloc_cnts(int pconid, int on)
{
	struct vcrtcm_pcon *pcon;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	/* this function is legal to call on a pcon that is being destroyed */
	pcon->log_alloc_cnts = on;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_log_alloc_cnts);

int vcrtcm_p_lock_mutex(int pconid)
{
	return vcrtcm_lock_pconid(pconid);
}
EXPORT_SYMBOL(vcrtcm_p_lock_mutex);

int vcrtcm_p_unlock_mutex(int pconid)
{
	return vcrtcm_unlock_pconid(pconid);
}
EXPORT_SYMBOL(vcrtcm_p_unlock_mutex);
