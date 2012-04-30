/*
 * Copyright 2010 Alcatel-Lucent Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ilija Hadzic <ihadzic@research.bell-labs.com>
 *          Larry Liu <ldl@research.bell-labs.com>
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/radeon_drm.h>
#include <drm/drm_fixed.h>
#include "radeon.h"
#include "radeon_virtual_crtc.h"
#include "atom.h"

#include "radeon_vcrtcm_kernel.h"

#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

struct drm_encoder *radeon_best_single_encoder(struct drm_connector *connector);
void radeon_connector_destroy(struct drm_connector *connector);

/* utility function for finding a virtual CRTC given a virtual connector
 * mapping between virtual crtcs and connectors must be 1:1:1, which
 * is always true for virtual CRTCs
 */
static struct radeon_crtc
*virtual_connector_to_crtc(struct drm_connector *connector)
{
	int encoder_id, crtc;
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct virtual_crtc *virtual_crtc;
	uint32_t pc;

	/* we can do this because encoder-connector mapping is 1-to-1 */
	encoder_id = connector->encoder_ids[0];
	if (!encoder_id) {
		WARN_ON(1);
		return NULL;
	}
	obj = drm_mode_object_find(dev, encoder_id, DRM_MODE_OBJECT_ENCODER);
	if (!obj) {
		WARN_ON(1);
		return NULL;
	}
	encoder = obj_to_encoder(obj);
	pc = 0x1;
	crtc = 0;
	/* first bit in possible_crtcs mask is ours becaus of 1:1 mapping */
	for (crtc = 0; crtc < 32 && !(pc & encoder->possible_crtcs); crtc++)
		pc <<= 1;
	if (crtc < rdev->num_crtc ||
	    crtc >= rdev->num_crtc+rdev->num_virtual_crtc) {
		WARN_ON(1);
		return NULL;
	}
	list_for_each_entry(virtual_crtc,
			    &rdev->mode_info.virtual_crtcs, list) {
		if (virtual_crtc->radeon_crtc->crtc_id == crtc)
			return virtual_crtc->radeon_crtc;
	}
	return NULL;
}

static int radeon_virtual_crtc_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	struct radeon_crtc *radeon_crtc;
	struct vcrtcm_mode *probed_modes;
	int i, probed_modes_count, r;

	/* if we are always connected, make up some common modes */
	if (radeon_conn_virt_crtc) {
		struct vcrtcm_mode common_modes[17] = {
			{640, 480, 60},
			{720, 480, 60},
			{800, 600, 60},
			{848, 480, 60},
			{1024, 768, 60},
			{1152, 768, 60},
			{1280, 720, 60},
			{1280, 800, 60},
			{1280, 854, 60},
			{1280, 960, 60},
			{1280, 1024, 60},
			{1440, 900, 60},
			{1400, 1050, 60},
			{1680, 1050, 60},
			{1600, 1200, 60},
			{1920, 1080, 60},
			{1920, 1200, 60}
		};

		for (i = 0; i < 17; i++) {
			mode = drm_cvt_mode(dev,
					    common_modes[i].w,
					    common_modes[i].h,
					    common_modes[i].refresh,
					    false, false, false);
			drm_mode_probed_add(connector, mode);
		}
		return 1;
	}

	/* find the CRTC associated with this connector */
	radeon_crtc = virtual_connector_to_crtc(connector);
	if (!radeon_crtc)
		return 0;

	/* if we have no PCON attached, then we have no modes to add */
	if (!radeon_crtc->vcrtcm_pcon_info)
		return 0;

	/* found an attached PCON, ask it */
	r = vcrtcm_get_modes(radeon_crtc->vcrtcm_pcon_info,
			     &probed_modes, &probed_modes_count);
	if (r)
		return 0;
	for (i = 0; i < probed_modes_count; i++) {
		mode = drm_cvt_mode(dev,
				    probed_modes[i].w,
				    probed_modes[i].h,
				    probed_modes[i].refresh,
				    false, false, false);
		drm_mode_probed_add(connector, mode);
	}
	return 1;
}

static int radeon_virtual_crtc_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode)
{
	int mode_status, r;
	struct vcrtcm_mode vcrtcm_mode;
	struct radeon_crtc *radeon_crtc;

	/* if we are always connected, then any mode is fine */
	if (radeon_conn_virt_crtc)
		return MODE_OK;

	radeon_crtc = virtual_connector_to_crtc(connector);

	/* if nothing is attached or there is no crtc, then no mode is good */
	if (!radeon_crtc)
		return MODE_ERROR;
	if (!radeon_crtc->vcrtcm_pcon_info)
		return MODE_ERROR;

	/* otherwise, we ask VCRTCM */
	vcrtcm_mode.w = drm_mode_width(mode);
	vcrtcm_mode.h = drm_mode_height(mode);
	vcrtcm_mode.refresh = drm_mode_vrefresh(mode);
	r = vcrtcm_check_mode(radeon_crtc->vcrtcm_pcon_info,
			      &vcrtcm_mode,
			      &mode_status);
	if (r)
		return MODE_ERROR;
	if (mode_status == VCRTCM_MODE_OK)
		return MODE_OK;
	else
		return MODE_NOMODE;
}

/* virtual connector is always connected */
static enum drm_connector_status
radeon_virtual_connector_detect(struct drm_connector *connector, bool force)
{
	struct radeon_crtc *radeon_crtc;
	int r, connected_status;

	/* told by the parameter that we are always connected */
	if (radeon_conn_virt_crtc)
		return connector_status_connected;

	radeon_crtc = virtual_connector_to_crtc(connector);
	if (!radeon_crtc)
		return connector_status_unknown;

	/* we are not connected if nothing is attached or there is no crtc */
	if (!radeon_crtc->vcrtcm_pcon_info)
		return connector_status_disconnected;

	/* otherwise ask VCRTCM */
	r = vcrtcm_pcon_connected(radeon_crtc->vcrtcm_pcon_info,
				 &connected_status);
	if (r)
		return connector_status_unknown;
	if (connected_status == VCRTCM_PCON_CONNECTED)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static int radeon_virtual_connector_set_property(struct drm_connector
						 *connector,
						 struct drm_property *property,
						 uint64_t val)
{
	/* REVISIT: does nothing for now */
	DRM_DEBUG("\n");
	return 0;
}

static void radeon_virtual_connector_force(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector =
	    to_radeon_connector(connector);
	/* REVISIT: for now we just repeat what dvi_force does, */
	/* it probably has no effect for virtual connector since */
	/* none of its helper funcs will look at ->use_digital field */
	/* but it doesn't hurt either */
	DRM_DEBUG("\n");
	if (connector->force == DRM_FORCE_ON)
		radeon_connector->use_digital = false;
	if (connector->force == DRM_FORCE_ON_DIGITAL)
		radeon_connector->use_digital = true;
}

struct drm_connector_helper_funcs radeon_virtual_connector_helper_funcs = {
	.get_modes = radeon_virtual_crtc_get_modes,
	.mode_valid = radeon_virtual_crtc_mode_valid,
	.best_encoder = radeon_best_single_encoder
};

struct drm_connector_funcs radeon_virtual_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = radeon_virtual_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = radeon_connector_destroy,
	.set_property = radeon_virtual_connector_set_property,
	.force = radeon_virtual_connector_force,
};

static void radeon_virtual_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	/* REVISIT: all DPMS stuff is handled by CRTC dpms (for now) */
	/* this might change in the future */
	DRM_DEBUG("mode %d\n", mode);
}

static bool radeon_virtual_mode_fixup(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG("\n");

	/* set the active encoder to connector routing */
	radeon_encoder_set_active_device(encoder);
	drm_mode_set_crtcinfo(adjusted_mode, 0);

	return true;
}

static void radeon_virtual_encoder_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG("\n");
	radeon_virtual_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void radeon_virtual_encoder_commit(struct drm_encoder *encoder)
{
	DRM_DEBUG("\n");
	radeon_virtual_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static void
radeon_virtual_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG("\n");
}

static enum drm_connector_status
radeon_virtual_encoder_detect(struct drm_encoder *encoder,
			      struct drm_connector *connector)
{
	DRM_DEBUG("\n");
	/* REVISIT: virtual connector/encoder are always connected (for now) */
	/* consider asking VCRTCM module for real status and if the DCT device */
	/* is really always connected hard-code the status there */
	return connector_status_connected;
}

static void radeon_virtual_encoder_disable(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_encoder_helper_funcs *encoder_funcs;

	encoder_funcs = encoder->helper_private;
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
	radeon_encoder->active_device = 0;
}

static const struct drm_encoder_helper_funcs
radeon_virtual_encoder_helper_funcs = {
	.dpms = radeon_virtual_encoder_dpms,
	.mode_fixup = radeon_virtual_mode_fixup,
	.prepare = radeon_virtual_encoder_prepare,
	.mode_set = radeon_virtual_encoder_mode_set,
	.commit = radeon_virtual_encoder_commit,
	.detect = radeon_virtual_encoder_detect,
	.disable = radeon_virtual_encoder_disable,
};

static const struct drm_encoder_funcs radeon_virtual_enc_funcs = {
	.destroy = radeon_enc_destroy,
};

void radeon_add_virtual_enc_conn(struct drm_device *dev, int inst)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct radeon_connector *radeon_connector;
	struct radeon_encoder *radeon_encoder;
	uint32_t supported_device;
	uint32_t connector_id, encoder_id;
	uint16_t connector_object_id;
	int connector_type;
	uint32_t subpixel_order = SubPixelHorizontalRGB;
	struct radeon_hpd hpd;

	DRM_DEBUG("instance %d\n", inst);

	/* use special supported device number for virtual display */
	supported_device = ATOM_DEVICE_VIRTUAL_SUPPORT;
	connector_id = (uint32_t) inst;
	connector_object_id = CONNECTOR_OBJECT_ID_VIRTUAL;
	connector_type = DRM_MODE_CONNECTOR_DisplayPort;	/* let's see if we can get away with this type */
	/* to avoid adding a new one to DRM level */
	/* see if we already added it */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		if (radeon_connector->connector_id == connector_id) {
			radeon_connector->devices |= supported_device;
			DRM_ERROR("connector_id %d already added\n",
				 connector_id);
			return;
		}
	}

	DRM_INFO("adding new virtual connector %d\n", connector_id);

	radeon_connector = kzalloc(sizeof(struct radeon_connector), GFP_KERNEL);
	if (!radeon_connector) {
		DRM_ERROR("can't get memory\n");
		return;
	}

	connector = &radeon_connector->base;
	radeon_connector->connector_id = connector_id;
	radeon_connector->devices = supported_device;
	radeon_connector->connector_object_id = connector_object_id;

	hpd.hpd = RADEON_HPD_NONE;
	radeon_connector->hpd = hpd;	/* hopefully this sufficiently "dumbs" down the HPD */
	radeon_connector->ddc_bus = NULL;	/* let's see if we can get with no DDC bus (we should) */
	radeon_connector->con_priv = NULL;

	/* now add the virtual connector to DRM structures */
	drm_connector_init(dev, &radeon_connector->base,
			   &radeon_virtual_connector_funcs, connector_type);
	drm_connector_helper_add(&radeon_connector->base,
				 &radeon_virtual_connector_helper_funcs);
	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
		DRM_CONNECTOR_POLL_DISCONNECT;
	connector->display_info.subpixel_order = subpixel_order;

	drm_connector_attach_property(&radeon_connector->base,
				      rdev->mode_info.load_detect_property,
				      false);

	drm_sysfs_connector_add(connector);

	DRM_INFO("connector added\n");
	DRM_INFO("connector_type %d, connector_type_id %d base.id=%d\n",
		 connector->connector_type,
		 connector->connector_type_id, connector->base.id);
	DRM_INFO("radeon_connector_id %d, radeon_connector_object_id %d\n",
		 radeon_connector->connector_id,
		 radeon_connector->connector_object_id);
	/* encoder does not have different fields for id and object_id like */
	/* connector does, so we use this packed format, for the lack of better solution */
	encoder_id = (inst << 16) | ENCODER_OBJECT_ID_VIRTUAL;

	/* see if we already added it */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		radeon_encoder = to_radeon_encoder(encoder);
		if (radeon_encoder->encoder_id == encoder_id) {
			radeon_encoder->devices |= supported_device;
			DRM_ERROR("encoder_id %d already added\n",
				 encoder_id);
			return;
		}
	}

	DRM_INFO("adding a new virtual encoder\n");

	/* add a new one */
	radeon_encoder = kzalloc(sizeof(struct radeon_encoder), GFP_KERNEL);
	if (!radeon_encoder) {
		DRM_ERROR("can't get memory\n");
		return;
	}

	encoder = &radeon_encoder->base;
	/* virtual-CRTC/virtual-connector mapping is 1-to-1, one and only one */
	/* CRTC is possible for this connector and it is calculated from the instance */
	/* number and the number of real (physical) CRTCs */
	encoder->possible_crtcs = 0x1 << (rdev->num_crtc + inst);
	radeon_encoder->enc_priv = NULL;
	radeon_encoder->encoder_id = encoder_id;
	radeon_encoder->devices = supported_device;
	radeon_encoder->rmx_type = RMX_OFF;

	drm_encoder_init(dev, encoder, &radeon_virtual_enc_funcs,
			 DRM_MODE_ENCODER_NONE);
	drm_encoder_helper_add(encoder, &radeon_virtual_encoder_helper_funcs);
	/* REVISIT: let's leave enc_priv at NULL and see what happens, */
	/* normally, it should be something like this */
	/* radeon_encoder->enc_priv = radeon_atombios_get_primary_dac_info(radeon_encoder); */

	DRM_INFO("encoder added\n");
	DRM_INFO("encoder_type %d, encoder_id %d\n",
		 encoder->encoder_type, encoder->base.id);
	DRM_INFO("radeon_encoder_id %d\n", radeon_encoder->encoder_id);

	drm_mode_connector_attach_encoder(connector, encoder);

}

void radeon_virtual_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	/*struct radeon_device *rdev = dev->dev_private; */
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	DRM_DEBUG("crtc_id=%d, mode=%d\n", radeon_crtc->crtc_id, mode);

	/* REVISIT: consider sending this info over the network to the client */
	/*          and adding support to the client to act upon the command */
	/* REVISIT2: client should also report the status back to us */
	/*           (e.g. auto power savings screen turnoff etc.) */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
		DRM_DEBUG("enabling crtc\n");
		radeon_crtc->enabled = true;
		/*radeon_pm_compute_clocks(rdev); */
		if (radeon_crtc->vcrtcm_pcon_info)
			vcrtcm_set_dpms(radeon_crtc->vcrtcm_pcon_info,
					VCRTCM_DPMS_STATE_ON);
		drm_vblank_post_modeset(dev, radeon_crtc->crtc_id);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		DRM_DEBUG("disabling crtc\n");
		drm_vblank_pre_modeset(dev, radeon_crtc->crtc_id);
		if (radeon_crtc->vcrtcm_pcon_info)
			vcrtcm_set_dpms(radeon_crtc->vcrtcm_pcon_info,
					VCRTCM_DPMS_STATE_OFF);
		radeon_crtc->enabled = false;
		/*radeon_pm_compute_clocks(rdev); */
		break;
	}
}

static bool radeon_virtual_crtc_mode_fixup(struct drm_crtc *crtc,
					   struct drm_display_mode *mode,
					   struct drm_display_mode
					   *adjusted_mode)
{
	DRM_DEBUG("\n");
	return true;
}

static void radeon_virtual_crtc_pre_page_flip(struct radeon_device *rdev, int crtc)
{
	if (crtc < 0 || crtc >= rdev->num_crtc+rdev->num_virtual_crtc) {
		DRM_ERROR("Invalid crtc %d\n", crtc);
		return;
	}

	if (crtc >= rdev->num_crtc) {
		struct virtual_crtc *virtual_crtc;
		list_for_each_entry(virtual_crtc,
				    &rdev->mode_info.virtual_crtcs, list) {
			if (virtual_crtc->radeon_crtc->crtc_id == crtc) {
				DRM_DEBUG("pflip emulation enabled for virtual crtc_id %d\n", crtc);
				virtual_crtc->radeon_crtc->pflip_emulation_enabled = true;
				break;
			}
		}
	} else {
		DRM_ERROR("virtual pflip requested on physical crtc\n");
	}
}

static void radeon_virtual_crtc_post_page_flip(struct radeon_device *rdev, int crtc)
{
	if (crtc < 0 || crtc >= rdev->num_crtc+rdev->num_virtual_crtc) {
		DRM_ERROR("Invalid crtc %d\n", crtc);
		return;
	}

	if (crtc >= rdev->num_crtc) {
		struct virtual_crtc *virtual_crtc;
		list_for_each_entry(virtual_crtc,
				    &rdev->mode_info.virtual_crtcs, list) {
			if (virtual_crtc->radeon_crtc->crtc_id == crtc) {
				DRM_DEBUG("pflip emulation disabled for virtual crtc_id %d\n", crtc);
				virtual_crtc->radeon_crtc->pflip_emulation_enabled = false;
				break;
			}
		}
	} else {
		DRM_ERROR("radeon_virtual_crtc_post_page_flip called on physical crtc\n");
	}
}


static int radeon_virtual_crtc_page_flip(struct drm_crtc *crtc,
					 struct drm_framebuffer *fb,
					 struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_framebuffer *old_radeon_fb;
	struct radeon_framebuffer *new_radeon_fb;
	struct drm_gem_object *obj;
	struct radeon_bo *rbo;
	struct radeon_unpin_work *work;
	unsigned long flags;
	u64 base;
	int r;

	if (!radeon_crtc->vcrtcm_pcon_info) {
		struct timeval now;
		int crtc_id = radeon_crtc->crtc_id;
		DRM_DEBUG("no hal on crtc %d, pflip faked out\n", crtc_id);
		crtc->fb = fb;
		event->event.sequence =
			drm_vblank_count_and_time(dev, crtc_id, &now);
		event->event.tv_sec = now.tv_sec;
		event->event.tv_usec = now.tv_usec;
		list_add_tail(&event->base.link,
			      &event->base.file_priv->event_list);
		wake_up_interruptible(&event->base.file_priv->event_wait);
		return 0;
	}

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (work == NULL)
		return -ENOMEM;

	work->event = event;
	work->rdev = rdev;
	work->crtc_id = radeon_crtc->crtc_id;
	old_radeon_fb = to_radeon_framebuffer(crtc->fb);
	new_radeon_fb = to_radeon_framebuffer(fb);
	/* schedule unpin of the old buffer */
	obj = old_radeon_fb->obj;
	/* take a reference to the old object */
	drm_gem_object_reference(obj);
	rbo = gem_to_radeon_bo(obj);
	work->old_rbo = rbo;
	obj = new_radeon_fb->obj;
	rbo = gem_to_radeon_bo(obj);
	if (rbo->tbo.sync_obj)
		work->fence = radeon_fence_ref(rbo->tbo.sync_obj);
	INIT_WORK(&work->work, radeon_unpin_work_func);

	/* We borrow the event spin lock for protecting unpin_work */
	spin_lock_irqsave(&dev->event_lock, flags);
	if (radeon_crtc->unpin_work) {
		drm_gem_object_unreference_unlocked(old_radeon_fb->obj);
		spin_unlock_irqrestore(&dev->event_lock, flags);
		kfree(work);
		radeon_fence_unref(&work->fence);

		DRM_DEBUG_DRIVER("flip queue: crtc already busy\n");
		return -EBUSY;
	}
	radeon_crtc->unpin_work = work;
	radeon_crtc->deferred_flip_completion = 0;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* pin the new buffer */
	DRM_DEBUG_DRIVER("flip-ioctl() cur_fbo = %p, cur_bbo = %p\n",
			 work->old_rbo, rbo);

	r = radeon_bo_reserve(rbo, false);
	if (unlikely(r != 0)) {
		DRM_ERROR("failed to reserve new rbo buffer before flip\n");
		goto pflip_cleanup;
	}
	r = radeon_bo_pin(rbo, RADEON_GEM_DOMAIN_VRAM, &base);
	if (unlikely(r != 0)) {
		radeon_bo_unreserve(rbo);
		r = -EINVAL;
		DRM_ERROR("failed to pin new rbo buffer before flip\n");
		goto pflip_cleanup;
	}
	radeon_bo_unreserve(rbo);

	spin_lock_irqsave(&dev->event_lock, flags);
	work->new_crtc_base = base;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* update crtc fb */
	if (crtc->fb == radeon_crtc->vcrtcm_push_fb)
		radeon_crtc->vcrtcm_push_fb = fb;
	crtc->fb = fb;

	r = drm_vblank_get(dev, radeon_crtc->crtc_id);
	if (r) {
		DRM_ERROR("failed to get vblank before flip\n");
		goto pflip_cleanup1;
	}

	/* set the proper interrupt */
	radeon_virtual_crtc_pre_page_flip(rdev, radeon_crtc->crtc_id);

	return 0;

pflip_cleanup1:
	if (unlikely(radeon_bo_reserve(rbo, false) != 0)) {
		DRM_ERROR("failed to reserve new rbo in error path\n");
		goto pflip_cleanup;
	}
	if (unlikely(radeon_bo_unpin(rbo) != 0))
		DRM_ERROR("failed to unpin new rbo in error path\n");
	radeon_bo_unreserve(rbo);

pflip_cleanup:
	spin_lock_irqsave(&dev->event_lock, flags);
	radeon_crtc->unpin_work = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);
	radeon_fence_unref(&work->fence);
	kfree(work);

	return r;
}

/* page flip handler for virtual CRTCs, follows the same logic as
   the one for physical CRTC, except that backend calls are different
   this one doesn't poke any registers but instead calls vcrtcm backend
*/
void radeon_virtual_crtc_handle_flip(struct radeon_device *rdev, int crtc)
{
	struct virtual_crtc *virtual_crtc;
	struct radeon_crtc *radeon_crtc = NULL;
	struct radeon_unpin_work *work;
	struct drm_pending_vblank_event *e;
	struct timeval now;
	unsigned long flags;
	u32 update_pending;

	if (crtc < rdev->num_crtc) {
		DRM_ERROR("not supposed to be here for (physical) crtc %d\n", crtc);
		return;
	}
	list_for_each_entry(virtual_crtc,
			    &rdev->mode_info.virtual_crtcs, list) {
		if (virtual_crtc->radeon_crtc->crtc_id == crtc) {
			radeon_crtc = virtual_crtc->radeon_crtc;
			break;
		}
	}
	if (!radeon_crtc) {
		DRM_ERROR("crtc not found\n");
		return;
	}
	DRM_DEBUG("performing page flip on virtual crtc %d\n", crtc);
	spin_lock_irqsave(&rdev->ddev->event_lock, flags);
	work = radeon_crtc->unpin_work;
	if (work == NULL ||
	    (work->fence && !radeon_fence_signaled(work->fence))) {
		spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);
		return;
	}

	/* New pageflip, or just completion of a previous one? */
	if (!radeon_crtc->deferred_flip_completion) {
		/* do the flip (call into vcrtcm backend) */
		update_pending =
			radeon_vcrtcm_page_flip(radeon_crtc, work->new_crtc_base);
		/* if flip has failed, we warn and continue */
		/* screen will be corrupted, but there is nothing we can do about it */
		if (update_pending < 0) {
			DRM_ERROR("page flip failed err %d\n", update_pending);
			update_pending = 0;
		}
	} else {
		/* This is just a completion of a flip queued in crtc
		 * at last invocation. Make sure we go directly to
		 * completion routine.
		 */
		update_pending = 0;
		radeon_crtc->deferred_flip_completion = 0;
	}

	if (update_pending) {
		/* unlike physical crtc, we have either completed or not */
		/* there is no need to examine vpos to tell if we "might" */
		/* still complete. if we didn't complete, then we the flip */
		/* will be queued up and completed at next vcrtcm transmission */
		radeon_crtc->deferred_flip_completion = 1;
		spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);
		return;
	}

	/* if we got here, then page flip has completed, so clean up */
	radeon_crtc->unpin_work = NULL;

	/* wakeup userspace */
	if (work->event) {
		e = work->event;
		e->event.sequence = drm_vblank_count_and_time(rdev->ddev, crtc, &now);
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;
		list_add_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}
	spin_unlock_irqrestore(&rdev->ddev->event_lock, flags);

	drm_vblank_put(rdev->ddev, radeon_crtc->crtc_id);
	radeon_fence_unref(&work->fence);
	radeon_virtual_crtc_post_page_flip(rdev, crtc);
	schedule_work(&work->work);
}

int radeon_virtual_crtc_do_set_base(struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    int x, int y, int atomic)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_framebuffer *radeon_fb;
	struct drm_framebuffer *target_fb;
	struct radeon_bo *rbo;
	int r = 0;
	u64 fb_gpuaddr;

	DRM_DEBUG("crtc=%d, x=%d, y=%d, atomic=%d\n",
		  radeon_crtc->crtc_id, x, y, atomic);

	/* no fb bound */
	if (!atomic && !crtc->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (atomic) {
		radeon_fb = to_radeon_framebuffer(fb);
		target_fb = fb;
	} else {
		radeon_fb = to_radeon_framebuffer(crtc->fb);
		target_fb = crtc->fb;
	}

	DRM_INFO("set base (vcrtc): new framebuffer info\n");
	DRM_INFO("frame buffer pitch %d width %d height %d bpp %d\n",
		 (unsigned int)(target_fb->pitches[0]),
		 (unsigned int)(target_fb->width),
		 (unsigned int)(target_fb->height),
		 (unsigned int)(target_fb->bits_per_pixel));
	DRM_INFO("crtc x %d y %d hdisplay %d vdisplay %d\n",
		 x, y, radeon_crtc->base.mode.hdisplay,
		 radeon_crtc->base.mode.vdisplay);

	rbo = gem_to_radeon_bo(radeon_fb->obj);
	r = radeon_bo_reserve(rbo, false);
	if (unlikely(r))
		return r;
	/* follow the same logic as in evergreen/atombios set_base */
	if (atomic)
		fb_gpuaddr = radeon_bo_gpu_offset(rbo);
	else {
		r = radeon_bo_pin(rbo, RADEON_GEM_DOMAIN_VRAM, &fb_gpuaddr);
		if (unlikely(r != 0)) {
			radeon_bo_unreserve(rbo);
			return r;
		}
	}
	radeon_bo_unreserve(rbo);

	r = radeon_vcrtcm_set_fb(radeon_crtc, x, y, target_fb, fb_gpuaddr);
	if (!atomic && fb && fb != crtc->fb) {
		DRM_DEBUG("found old framebuffer, unpinning\n");
		radeon_fb = to_radeon_framebuffer(fb);
		rbo = gem_to_radeon_bo(radeon_fb->obj);
		r = radeon_bo_reserve(rbo, false);
		if (unlikely(r))
			return r;
		/* it's a good idea to wait on VCRTCM before returning, that */
		/* would guarantee not to release a buffer whose transmission */
		/* may still be in progress */
		if (radeon_crtc->vcrtcm_pcon_info)
			r = vcrtcm_wait_fb(radeon_crtc->vcrtcm_pcon_info);
		/* if wait fails, unpin anyway (we have a bigger problem in that case) */
		radeon_bo_unpin(rbo);
		radeon_bo_unreserve(rbo);
	} else
		DRM_DEBUG("no old framebuffer\n");

	return r;
}

static int radeon_virtual_crtc_set_base(struct drm_crtc *crtc, int x, int y,
					struct drm_framebuffer *old_fb)
{
	return radeon_virtual_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

static int radeon_virtual_crtc_set_base_atomic(struct drm_crtc *crtc,
					       struct drm_framebuffer *fb,
					       int x, int y,
					       enum mode_set_atomic state)
{
	return radeon_virtual_crtc_do_set_base(crtc, fb, x, y, 1);
}

static int radeon_virtual_crtc_mode_set(struct drm_crtc *crtc,
					struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode,
					int x, int y,
					struct drm_framebuffer *old_fb)
{
	DRM_DEBUG("\n");
	return radeon_virtual_crtc_set_base(crtc, x, y, old_fb);
}

static void radeon_virtual_crtc_disable(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	DRM_DEBUG("\n");
	if (radeon_crtc->vcrtcm_pcon_info)
		vcrtcm_disable(radeon_crtc->vcrtcm_pcon_info);
	radeon_virtual_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void radeon_virtual_crtc_prepare(struct drm_crtc *crtc)
{

	DRM_DEBUG("\n");
	radeon_virtual_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void radeon_virtual_crtc_commit(struct drm_crtc *crtc)
{
	DRM_DEBUG("\n");
	radeon_virtual_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

/* REVISIT: eventually we will have to send */
/* LUT over the network to the client and the client will */
/* have to do the translation when displaying the color buffer */
/* for now, we don't support LUT-based color maps, so we */
/* should not need this function anyway */
static void radeon_virtual_crtc_load_lut(struct drm_crtc *crtc)
{
	DRM_DEBUG("\n");
}

static const struct drm_crtc_helper_funcs virtual_helper_funcs = {
	.dpms = radeon_virtual_crtc_dpms,
	.mode_fixup = radeon_virtual_crtc_mode_fixup,
	.mode_set = radeon_virtual_crtc_mode_set,
	.mode_set_base = radeon_virtual_crtc_set_base,
	.mode_set_base_atomic = radeon_virtual_crtc_set_base_atomic,
	.prepare = radeon_virtual_crtc_prepare,
	.commit = radeon_virtual_crtc_commit,
	.load_lut = radeon_virtual_crtc_load_lut,
	.disable = radeon_virtual_crtc_disable
};

int radeon_virtual_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int r = 0;

	DRM_DEBUG("x %d y %d crtc %d\n", x, y, radeon_crtc->crtc_id);

	if (radeon_crtc->vcrtcm_pcon_info) {
		struct vcrtcm_cursor vcrtcm_cursor;

		r = vcrtcm_get_cursor(radeon_crtc->vcrtcm_pcon_info,
				      &vcrtcm_cursor);
		if (r)
			return r;
		vcrtcm_cursor.location_x = x;
		vcrtcm_cursor.location_y = y;
		r = vcrtcm_set_cursor(radeon_crtc->vcrtcm_pcon_info,
				      &vcrtcm_cursor);
		if (radeon_crtc->enabled)
			vcrtcm_xmit_fb(radeon_crtc->vcrtcm_pcon_info);
	}

	return r;
}

int radeon_hide_virtual_cursor(struct radeon_crtc *radeon_crtc)
{
	struct vcrtcm_cursor vcrtcm_cursor;
	int r = 0;
	int fps;

	DRM_DEBUG("\n");
	if (radeon_crtc->vcrtcm_pcon_info) {
		/* turn off cursor */
		r = vcrtcm_get_cursor(radeon_crtc->vcrtcm_pcon_info,
				      &vcrtcm_cursor);
		if (r)
			return r;
		vcrtcm_cursor.flag |= VCRTCM_CURSOR_FLAG_HIDE;
		r = vcrtcm_set_cursor(radeon_crtc->vcrtcm_pcon_info,
				      &vcrtcm_cursor);
		if (r)
			return r;
		r = vcrtcm_get_fps(radeon_crtc->vcrtcm_pcon_info, &fps);
		if (r)
			return r;
		/* force xmit only if xmit is enabled, */
		/* otherwise, just silently return */
		if (fps > 0)
			return vcrtcm_xmit_fb(radeon_crtc->vcrtcm_pcon_info);
		else
			return 0;
	}
	return r;
}

int radeon_show_and_set_virtual_cursor(struct radeon_crtc *radeon_crtc,
				       struct drm_gem_object *obj,
				       uint64_t cursor_gpuaddr)
{
	struct radeon_device *rdev =
	    (struct radeon_device *)radeon_crtc->base.dev->dev_private;
	struct vcrtcm_cursor vcrtcm_cursor;
	uint64_t cursor_ioaddr;
	int r = 0;
	int fps;

	DRM_DEBUG("\n");
	if (radeon_crtc->vcrtcm_pcon_info) {
		cursor_ioaddr =
		    rdev->mc.aper_base + (cursor_gpuaddr - rdev->mc.vram_start);
		DRM_DEBUG("cursor sprite ioaddr %llx gpuaddr %llx size %d\n",
			  cursor_ioaddr, cursor_gpuaddr, obj->size);

		r = vcrtcm_get_cursor(radeon_crtc->vcrtcm_pcon_info,
				      &vcrtcm_cursor);
		if (r)
			return r;
		vcrtcm_cursor.flag &= ~VCRTCM_CURSOR_FLAG_HIDE;
		vcrtcm_cursor.width = radeon_crtc->cursor_width;
		vcrtcm_cursor.height = radeon_crtc->cursor_height;
		vcrtcm_cursor.bpp = radeon_crtc->base.fb->bits_per_pixel;
		vcrtcm_cursor.ioaddr = cursor_ioaddr;
		r = vcrtcm_set_cursor(radeon_crtc->vcrtcm_pcon_info,
				      &vcrtcm_cursor);
		if (r)
			return r;
		r = vcrtcm_get_fps(radeon_crtc->vcrtcm_pcon_info, &fps);
		if (r)
			return r;
		if (fps > 0)
			return vcrtcm_xmit_fb(radeon_crtc->vcrtcm_pcon_info);
		else
			return 0;
	}
	return r;
}

int radeon_virtual_crtc_cursor_set(struct drm_crtc *crtc,
				   struct drm_file *file_priv,
				   uint32_t handle,
				   uint32_t width, uint32_t height)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_gem_object *obj;
	struct radeon_bo *robj;
	uint64_t cursor_gpuaddr;
	int r = 0;

	DRM_DEBUG("w %d h %d hdl %d crtc_id %d\n",
		  width, height, handle, radeon_crtc->crtc_id);

	if (!handle) {
		obj = NULL;
		r = radeon_hide_virtual_cursor(radeon_crtc);
		goto unpin;
	}

	if ((width > CURSOR_WIDTH) || (height > CURSOR_HEIGHT)) {
		DRM_ERROR("bad cursor width or height %d x %d\n",
			  width, height);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("cannot find cursor object %x for crtc %d\n",
			  handle, radeon_crtc->crtc_id);
		return -EINVAL;
	}

	robj = gem_to_radeon_bo(obj);
	r = radeon_bo_reserve(robj, false);
	if (unlikely(r != 0))
		goto fail;
	r = radeon_bo_pin(robj, RADEON_GEM_DOMAIN_VRAM, &cursor_gpuaddr);
	radeon_bo_unreserve(robj);
	if (r)
		goto fail;

	radeon_crtc->cursor_width = width;
	radeon_crtc->cursor_height = height;

	radeon_show_and_set_virtual_cursor(radeon_crtc, obj, cursor_gpuaddr);

unpin:
	if (radeon_crtc->cursor_bo) {
		robj = gem_to_radeon_bo(radeon_crtc->cursor_bo);
		r = radeon_bo_reserve(robj, false);
		if (likely(r == 0)) {
			radeon_bo_unpin(robj);
			radeon_bo_unreserve(robj);
		}
		drm_gem_object_unreference_unlocked(radeon_crtc->cursor_bo);
	}

	radeon_crtc->cursor_bo = obj;
	if (radeon_crtc->vcrtcm_pcon_info && radeon_crtc->enabled)
		vcrtcm_xmit_fb(radeon_crtc->vcrtcm_pcon_info);
	return 0;
fail:
	drm_gem_object_unreference_unlocked(obj);
	return r;
}

static void radeon_virtual_crtc_destroy(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_device *rdev = crtc->dev->dev_private;
	struct virtual_crtc *virtual_crtc, *tmp;

	DRM_DEBUG("%d\n", radeon_crtc->crtc_id);

	/* if this CRTC has a vcrtcm HAL attached to it, */
	/* detach it at this point; ignore the error code */
	/* since there is nothing we can do about it anyway */
	radeon_vcrtcm_detach(radeon_crtc);

	list_for_each_entry_safe(virtual_crtc, tmp,
				 &rdev->mode_info.virtual_crtcs, list) {
		if (virtual_crtc->radeon_crtc == radeon_crtc) {
			DRM_DEBUG("found virtual crtc that should be removed\n");
			list_del(&virtual_crtc->list);
			kfree(virtual_crtc);
			if (rdev->num_virtual_crtc != 0)
				rdev->num_virtual_crtc--;
			else
				DRM_ERROR("num_virtual_crtc corrupted, "
					  "but can't do anything about it\n");
		}
	}

	drm_crtc_cleanup(crtc);
	kfree(radeon_crtc);
}

static void radeon_virtual_crtc_gamma_set(struct drm_crtc *crtc, u16 *red,
					  u16 *green, u16 *blue,
					  uint32_t start, uint32_t size)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int end = (start + size > 256) ? 256 : start + size, i;

	/* userspace palettes are always correct as is */
	for (i = start; i < end; i++) {
		radeon_crtc->lut_r[i] = red[i] >> 6;
		radeon_crtc->lut_g[i] = green[i] >> 6;
		radeon_crtc->lut_b[i] = blue[i] >> 6;
	}

	/* REVISIT: lut probably needs to go to the vcrtcm which */
	/* should pass it to the PCON to decide what to do */
	/* (e.g. send a message to the remote display device) */
	/* implement the call here */
}

static const struct drm_crtc_funcs radeon_virtual_crtc_funcs = {
	.cursor_set = radeon_virtual_crtc_cursor_set,
	.cursor_move = radeon_virtual_crtc_cursor_move,
	.gamma_set = radeon_virtual_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = radeon_virtual_crtc_destroy,
	.page_flip = radeon_virtual_crtc_page_flip
};

void radeon_virtual_crtc_data_init(struct radeon_crtc *radeon_crtc)
{
	/* virtual crtc specific initialization */
	radeon_crtc->emulated_vblank_counter = 0;
	radeon_crtc->vblank_emulation_enabled = false;
	radeon_crtc->emulated_pflip_counter = 0;
	radeon_crtc->pflip_emulation_enabled = false;
	radeon_crtc->vcrtcm_pcon_info = NULL;
	radeon_crtc->vcrtcm_push_fb = NULL;
}

void radeon_virtual_crtc_init(struct drm_device *dev, int index)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc;
	struct virtual_crtc *virtual_crtc;
	int i;

	DRM_DEBUG("index=%d\n", index);

	/* REVISIT: why do we need to extend the size by some "magic" number */
	/* of connector structure sizes (check who uses it for now, we just */
	/* play dumb and do whatever the real CRTC init function allocates) */
	radeon_crtc =
	    kzalloc(sizeof(struct radeon_crtc) +
		    (RADEONFB_CONN_LIMIT * sizeof(struct drm_connector *)),
		    GFP_KERNEL);

	if (radeon_crtc == NULL)
		return;

	virtual_crtc = kmalloc(sizeof(struct virtual_crtc), GFP_KERNEL);

	if (virtual_crtc == NULL) {
		kfree(radeon_crtc);
		return;
	}

	drm_crtc_init(dev, &radeon_crtc->base, &radeon_virtual_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&radeon_crtc->base, 256);
	radeon_crtc->crtc_id = index;
	radeon_virtual_crtc_data_init(radeon_crtc);
	virtual_crtc->radeon_crtc = radeon_crtc;
	list_add(&virtual_crtc->list, &rdev->mode_info.virtual_crtcs);

	for (i = 0; i < 256; i++) {
		radeon_crtc->lut_r[i] = i << 2;
		radeon_crtc->lut_g[i] = i << 2;
		radeon_crtc->lut_b[i] = i << 2;
	}

	/* if the rest of the implementation of virtual CRTC has been done */
	/* right, we should be able to "live" crtc_offset being anything; */
	/* this field is used when poking CRTC-related registers in the */
	/* GPU and we better not do that for virtual CRTC; still, as every */
	/* good programmer does, we initialize it to something (0 or 42 should */
	/* be right numbers) */
	radeon_crtc->crtc_offset = 0;

	/* atombios initialization does this so we are just being stupind */
	/* don't expect to be referenced, but won't hurt to set it */
	radeon_crtc->pll_id = -1;

	/* this links helper function (which will be virutal CRTC */
	/* specific) to DRM; functions are (un)implemented in this file */
	drm_crtc_helper_add(&radeon_crtc->base, &virtual_helper_funcs);

	DRM_INFO("created virtual crtc radeon_crtc_id %d, drm_crtc_id %d\n",
		 radeon_crtc->crtc_id, radeon_crtc->base.base.id);
}

void radeon_vbl_emu_cleanup_work(struct work_struct *work)
{
	struct radeon_vblank_emu_driver *vbl_emu_drv =
		container_of(work, struct radeon_vblank_emu_driver,
			     cleanup_work);
	unsigned long flags;
	struct push_vblank_pending *push_vblank_pending, *tmp;

	spin_lock_irqsave(&vbl_emu_drv->pending_queue_lock, flags);
	list_for_each_entry_safe(push_vblank_pending, tmp,
				 &vbl_emu_drv->pending_queue, list) {
		if (push_vblank_pending->vblank_sent) {
			DRM_DEBUG("push complete crtc_id=%d, time=%lu ms\n",
				  push_vblank_pending->radeon_crtc->crtc_id,
				  (push_vblank_pending->end_jiffies-
				  push_vblank_pending->start_jiffies)*1000/HZ);
			list_del(&push_vblank_pending->list);
			WARN_ON(!radeon_fence_signaled(push_vblank_pending->radeon_fence));
			radeon_fence_unref(&push_vblank_pending->radeon_fence);
			kfree(push_vblank_pending);
		}
	}
	spin_unlock_irqrestore(&vbl_emu_drv->pending_queue_lock, flags);
}
