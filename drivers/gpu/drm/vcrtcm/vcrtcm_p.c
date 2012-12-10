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
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_sysfs.h>
#include "vcrtcm_pim_table.h"
#include "vcrtcm_sysfs_priv.h"
#include "vcrtcm_p.h"
#include "vcrtcm_pcon.h"
#include "vcrtcm_pcon_table.h"
#include "vcrtcm_utils_priv.h"
#include "vcrtcm_module.h"
#include "vcrtcm_alloc_priv.h"

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

int vcrtcm_p_register_prime_l(int pconid,
			    struct vcrtcm_push_buffer_descriptor *pbd)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_register_prime(pconid, pbd);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_register_prime_l);

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

int vcrtcm_p_unregister_prime_l(int pconid,
			    struct vcrtcm_push_buffer_descriptor *pbd)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_unregister_prime(pconid, pbd);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_unregister_prime_l);

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

int vcrtcm_p_wait_fb_l(int pconid)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_wait_fb(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_wait_fb_l);

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

int vcrtcm_p_emulate_vblank_l(int pconid)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_emulate_vblank(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_emulate_vblank_l);

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

int vcrtcm_p_push_l(int pconid,
		struct vcrtcm_push_buffer_descriptor *fpbd,
		struct vcrtcm_push_buffer_descriptor *cpbd)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_push(pconid, fpbd, cpbd);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_push_l);

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

int vcrtcm_p_hotplug_l(int pconid)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_hotplug(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_hotplug_l);

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

int vcrtcm_p_free_pb_l(int pconid,
		      struct vcrtcm_push_buffer_descriptor *pbd)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_free_pb(pconid, pbd);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_free_pb_l);

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

struct vcrtcm_push_buffer_descriptor *
vcrtcm_p_alloc_pb_l(int pconid, int npages,
		  gfp_t gfp_mask)
{
	struct vcrtcm_push_buffer_descriptor *r;

	if (vcrtcm_p_lock_pconid(pconid))
		return ERR_PTR(-EINVAL);
	r = vcrtcm_p_alloc_pb(pconid, npages, gfp_mask);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_alloc_pb_l);

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

struct vcrtcm_push_buffer_descriptor *
vcrtcm_p_realloc_pb_l(int pconid,
		    struct vcrtcm_push_buffer_descriptor *pbd, int npages,
		    gfp_t gfp_mask)
{
	struct vcrtcm_push_buffer_descriptor *r;

	if (vcrtcm_p_lock_pconid(pconid))
		return ERR_PTR(-EINVAL);
	r = vcrtcm_p_realloc_pb(pconid, pbd, npages, gfp_mask);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_realloc_pb_l);

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
	if (!pcon->drm_crtc) {
		VCRTCM_WARNING("pcon %d already detached\n", pcon->pconid);
		return 0;
	}
	vcrtcm_prepare_detach(pcon);
	if (pcon->gpu_funcs.detach)
		pcon->gpu_funcs.detach(pcon->pconid, pcon->drm_crtc);
	pcon->drm_crtc = NULL;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_detach);

int vcrtcm_p_detach_l(int pconid)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_detach(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_detach_l);

int vcrtcm_p_destroy(int pconid)
{
	spinlock_t *page_flip_spinlock;
	struct vcrtcm_pcon *pcon;
	unsigned long flags;

	vcrtcm_check_mutex(__func__, pconid);
	page_flip_spinlock = vcrtcm_get_pconid_spinlock(pconid);
	if (!page_flip_spinlock)
		return -EINVAL;
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	if (pcon->being_destroyed) {
		VCRTCM_ERROR("pcon 0x%08x already being destroyed\n", pconid);
		return -EINVAL;
	}
	if (pcon->drm_crtc) {
		vcrtcm_prepare_detach(pcon);
		if (pcon->gpu_funcs.detach)
			pcon->gpu_funcs.detach(pcon->pconid, pcon->drm_crtc);
		pcon->drm_crtc = NULL;
	}
	VCRTCM_INFO("destroying pcon %i\n", pcon->pconid);
	spin_lock_irqsave(page_flip_spinlock, flags);
	pcon->being_destroyed = 1;
	spin_unlock_irqrestore(page_flip_spinlock, flags);
	vcrtcm_destroy_pcon(pcon);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_destroy);

int vcrtcm_p_destroy_l(int pconid)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_destroy(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_destroy_l);

int vcrtcm_p_disable_callbacks(int pconid)
{
	struct vcrtcm_pcon *pcon;
	unsigned long flags;
	spinlock_t *pcon_spinlock;

	vcrtcm_check_mutex(__func__, pconid);
	pcon = vcrtcm_get_pcon(pconid);
	if (!pcon) {
		VCRTCM_ERROR("no pcon %d\n", pconid);
		return -ENODEV;
	}
	/* this function is legal to call on a pcon that is being destroyed */
	pcon_spinlock = vcrtcm_get_pconid_spinlock(pconid);
	BUG_ON(!pcon_spinlock);
	spin_lock_irqsave(pcon_spinlock, flags);
	pcon->pcon_callbacks_enabled = 0;
	spin_unlock_irqrestore(pcon_spinlock, flags);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_p_disable_callbacks);

int vcrtcm_p_disable_callbacks_l(int pconid)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_disable_callbacks(pconid);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_disable_callbacks_l);

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

int vcrtcm_p_log_alloc_cnts_l(int pconid, int on)
{
	int r;

	if (vcrtcm_p_lock_pconid(pconid))
		return -EINVAL;
	r = vcrtcm_p_log_alloc_cnts(pconid, on);
	vcrtcm_g_unlock_pconid(pconid);
	return r;
}
EXPORT_SYMBOL(vcrtcm_p_log_alloc_cnts_l);

int vcrtcm_p_lock_pconid(int pconid)
{
	return vcrtcm_lock_pconid(pconid);
}
EXPORT_SYMBOL(vcrtcm_p_lock_pconid);

int vcrtcm_p_unlock_pconid(int pconid)
{
	return vcrtcm_unlock_pconid(pconid);
}
EXPORT_SYMBOL(vcrtcm_p_unlock_pconid);

int vcrtcm_pim_register(char *pim_name,
	struct vcrtcm_pim_funcs *funcs, int *pimid)
{
	struct vcrtcm_pim *pim;

	VCRTCM_INFO("registering pim %s\n", pim_name);
	pim = vcrtcm_create_pim(pim_name, funcs);
	if (IS_ERR(pim))
		return PTR_ERR(pim);
	*pimid = pim->id;
	vcrtcm_sysfs_add_pim(pim);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_register);

int vcrtcm_pim_unregister(int pimid)
{
	struct vcrtcm_pim *pim;
	struct vcrtcm_pcon *pcon, *tmp;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	VCRTCM_INFO("unregistering %s\n", pim->name);
	list_for_each_entry_safe(pcon, tmp,
			&pim->pcons_in_pim_list, pcons_in_pim_list)
		vcrtcm_p_destroy_l(pcon->pconid);
	vcrtcm_sysfs_del_pim(pim);
	vcrtcm_destroy_pim(pim);
	VCRTCM_INFO("finished unregistering pim\n");
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_unregister);

int vcrtcm_pim_enable_callbacks(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	VCRTCM_INFO("enabling callbacks for pim %s\n", pim->name);
	pim->callbacks_enabled = 1;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_enable_callbacks);

int vcrtcm_pim_disable_callbacks(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	VCRTCM_INFO("disabling callbacks for pim %s\n", pim->name);
	pim->callbacks_enabled = 0;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_disable_callbacks);

int vcrtcm_pim_log_alloc_cnts(int pimid, int on)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim)
		return -EINVAL;
	pim->log_alloc_cnts = on;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_log_alloc_cnts);


/*
 * Helper function for vcrtcm_pim_add_major. It allocates and registers
 * a major device number. If desired major device numer is specified
 * it tries to use that one. Otherwise, it allocates dynamically
 */
static int vcrtcm_alloc_major(int desired_major, int num_minors,
			      const char *name, int *pim_major)
{
	dev_t dev;
	int r;

	if (desired_major >= 0) {
		dev = MKDEV(desired_major, 0);
		r = register_chrdev_region(dev, num_minors, name);
		if (r) {
			VCRTCM_ERROR("can't allocate static major %d for %s\n",
				     desired_major, name);
			return r;
		} else
			VCRTCM_INFO("allocated static major %d for %s\n",
				    MAJOR(dev), name);
	} else {
		r = alloc_chrdev_region(&dev, 0, num_minors, name);
		if (r) {
			VCRTCM_ERROR("can't allocate dynamic major for %s\n",
				     name);
			return r;
		} else
			VCRTCM_INFO("allocated dynamic major %d for %s\n",
				    MAJOR(dev), name);
	}
	*pim_major = MAJOR(dev);
	return r;
}

/*
 * Helper function for vcrtcm_pim_del_major. Returns the major
 * device number to the system by freeing the chrdev region
 */
static void vcrtcm_free_major(int major, int num_minors)
{
	if (major >= 0) {
		dev_t dev = MKDEV(major, 0);
		unregister_chrdev_region(dev, num_minors);
	}
}

/*
 * Helper function for vcrtcm_pim_add/del_minor. Looks up vcrtcm_minor
 * structure within a pim that has a specified minor number
 */
static struct vcrtcm_minor *vcrtcm_get_minor(struct vcrtcm_pim *pim, int minor)
{
	struct vcrtcm_minor *vcrtcm_minor;

	list_for_each_entry(vcrtcm_minor, &pim->minors_in_pim_list,
			    minors_in_pim_list) {
		if (vcrtcm_minor->minor == minor)
			return vcrtcm_minor;
	}
	return NULL;
}

/*
 * Records major device number for a PIM. Only PIMs that interact
 * outside the VCRTCM context have major device numbers. Calling
 * this function is a pre-requisite for using vcrtcm_pim_add_minor
 * and vcrtcm_pim_del_minor
 */
int vcrtcm_pim_add_major(int pimid, int desired_major, int max_minors)
{
	struct vcrtcm_pim *pim;
	int major, r;

	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (pim->has_major) {
		VCRTCM_ERROR("pim %d already has major %d\n",
			     pimid, pim->major);
		return -EBUSY;
	}
	r = vcrtcm_alloc_major(desired_major, max_minors, pim->name, &major);
	if (r)
		return r;
	INIT_LIST_HEAD(&pim->minors_in_pim_list);
	pim->major = major;
	pim->has_major = 1;
	pim->max_minors = max_minors;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_add_major);

/* Opposite of vcrtcm_pim_add_major */
int vcrtcm_pim_del_major(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (pim->has_major == 0) {
		VCRTCM_ERROR("pin %d has no major\n", pimid);
		return -ENOENT;
	}
	if (!list_empty(&pim->minors_in_pim_list)) {
		VCRTCM_ERROR("list of minors for pim %d not empty\n", pimid);
		return -EBUSY;
	}
	vcrtcm_free_major(pim->major, pim->max_minors);
	pim->major = 0;
	pim->has_major = 0;
	pim->max_minors = 0;
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_del_major);

/* Returns major for a given PCON or -ENOENT on error */
int vcrtcm_pim_get_major(int pimid)
{
	struct vcrtcm_pim *pim;

	pim = vcrtcm_get_pim(pimid);
	if (!pim || pim->has_major == 0)
		return -ENOENT;
	return pim->major;
}
EXPORT_SYMBOL(vcrtcm_pim_get_major);

/*
 * Creates a device structure for a specified pcon and minor,
 * and adds it to vcrtcm class. This function is used by PIMs
 * that interact with user space outside VCRTCM context, and
 * thus need their own major/minor numbers and device files.
 * Call into this function will generate udev event that will
 * in turn create a device file. PIM is responsible for
 * maintaining minor device numbers and relationship between
 * PCON IDs and minor device numbers can be arbitrary
 * (it's up to the PIM to establish a relationship that is
 * meaningful for it). Prior to calling this function, PIM
 * must register the major device number using vcrtcm_pim_add_major
 * function.
 */
int vcrtcm_pim_add_minor(int pimid, int minor)
{
	struct vcrtcm_pim *pim;
	struct device *device;
	dev_t dev;
	struct vcrtcm_minor *vcrtcm_minor;

	if (minor < 0) {
		VCRTCM_ERROR("invalid minor");
		return -EINVAL;
	}
	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (!pim->has_major) {
		VCRTCM_ERROR("pim %d has no major\n", pimid);
		return -ENOENT;
	}
	vcrtcm_minor = vcrtcm_get_minor(pim, minor);
	if (vcrtcm_minor) {
		VCRTCM_ERROR("pim %d, minor %d, already added\n",
			     pimid, minor);
		return -EBUSY;
	}
	vcrtcm_minor = vcrtcm_kzalloc(sizeof(struct vcrtcm_minor), GFP_KERNEL,
				      VCRTCM_OWNER_PIM | pim->id);
	if (!vcrtcm_minor)
		return -ENOMEM;
	dev = MKDEV(pim->major, minor);
	device = device_create(vcrtcm_class, NULL, dev, NULL,
			       "%s%d", pim->name, minor);
	if (!device) {
		vcrtcm_kfree(vcrtcm_minor);
		return -EFAULT;
	}
	vcrtcm_minor->minor = minor;
	vcrtcm_minor->device = device;
	list_add_tail(&vcrtcm_minor->minors_in_pim_list,
		      &pim->minors_in_pim_list);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_add_minor);

/* Opposite of vcrtcm_pim_add_minor.*/
int vcrtcm_pim_del_minor(int pimid, int minor)
{

	struct vcrtcm_pim *pim;
	struct vcrtcm_minor *vcrtcm_minor;
	dev_t dev;

	pim = vcrtcm_get_pim(pimid);
	if (!pim) {
		VCRTCM_ERROR("pim %d not found\n", pimid);
		return -ENOENT;
	}
	if (!pim->has_major) {
		VCRTCM_ERROR("pim %d has no major\n", pimid);
		return -ENOENT;
	}
	vcrtcm_minor = vcrtcm_get_minor(pim, minor);
	if (!vcrtcm_minor) {
		VCRTCM_ERROR("pim %d, minor %d, not found\n",
			     pimid, minor);
		return -EBUSY;
	}
	list_del(&vcrtcm_minor->minors_in_pim_list);
	dev = MKDEV(pim->major, minor);
	device_destroy(vcrtcm_class, dev);
	vcrtcm_kfree(vcrtcm_minor);
	return 0;
}
EXPORT_SYMBOL(vcrtcm_pim_del_minor);
