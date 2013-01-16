/**
 * \file drm_vcrtcm.c
 * DRM support functions for VCRTCM
 *
 * \author Martin Carroll <martin.carroll@alcatel-lucent.com>
 */

/*
 * Created: Fri Jan  11 09:04:00 2013 by martin.carroll@alcatel-lucent.com
 *
 * Copyright (C) 2013 Alcatel-Lucent, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include <drm/drm_vcrtcm.h>
#include <linux/slab.h>
#include <linux/module.h>

int drm_vcrtcm_connector_is_in_group(int connid, struct drm_mode_group *group)
{
	int i;
	int s;
	int e;

	s = group->num_crtcs + group->num_encoders;
	e = s + group->num_connectors;
	for (i = s; i < e; ++i) {
		if (connid == group->id_list[i])
			return 1;
	}
	return 0;
}

static int crtc_drmid2index(struct drm_device *dev, int drmid)
{
	struct drm_crtc *crtc;
	int index = 0;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (crtc->base.id == drmid)
			return index;
		++index;
	}
	return -1;
}

static int search_encoder(struct drm_device *dev, struct drm_connector *conn,
	struct drm_encoder *enc, struct drm_mode_group *group, int *crtc_drmid,
	int *crtc_index)
{
	struct drm_crtc *crtc;
	int mask = 0x1;
	int index = 0;

	DRM_INFO("possible crtcs: 0x%08x\n", enc->possible_crtcs);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (enc->possible_crtcs & mask) {
			int drmid = crtc->base.id;
			DRM_INFO("search crtc %d:%d\n", drmid, index);
			if (drm_obj_is_in_group(drmid, group)) {
				DRM_INFO("crtc %d:%d is in group\n", drmid, index);
				if (*crtc_drmid >= 0) {
					DRM_ERROR("conn %d has multiple crtcs\n",
						conn->base.id);
					return -EINVAL;
				}
				*crtc_drmid = drmid;
				*crtc_index = index;
			}
		}
		mask <<= 1;
		++index;
	}
	/* note that not finding a crtc for a given encoder is *not*
	 * an error
	 */
	return 0;
}

static int drm_vcrtcm_select_crtc_for_attach(struct drm_device *dev,
	struct drm_connector *conn, struct drm_mode_group *group,
	int *crtc_drmid, int *crtc_index)
{
	int num_items;
	int k;

	DRM_INFO("selecting crtc from: %d crtc, %d enc, %d conn, %d plane\n",
		group->num_crtcs, group->num_encoders, group->num_connectors,
		group->num_planes);
	num_items = group->num_crtcs + group->num_encoders +
		group->num_connectors;
	for (k = 0; k < num_items; ++k)
		DRM_INFO("id %d\n", group->id_list[k]);

	/* if connector has an in-use crtc that is also in the group,
	 * choose it.
	 */
	if (conn->encoder && conn->encoder->crtc) {
		int drmid = conn->encoder->crtc->base.id;
		if (drm_obj_is_in_group(drmid, group)) {
			*crtc_drmid = drmid;
			*crtc_index = crtc_drmid2index(dev, drmid);
			BUG_ON(*crtc_index < 0);
			DRM_INFO("selected connected crtc %d:%d\n", *crtc_drmid,
				*crtc_index);
			return 0;
		}
	}
	/* otherwise, if the connector has exactly one crtc that is
	 * also in the group, choose it.
	 */
	*crtc_drmid = -1;
	*crtc_index = -1;
	for (k = 0; k < DRM_CONNECTOR_MAX_ENCODER; ++k) {
		int enc_id = conn->encoder_ids[k];
		DRM_INFO("search enc %d\n", enc_id);
		if (drm_obj_is_in_group(enc_id, group)) {
			struct drm_mode_object *obj;
			struct drm_encoder *enc;
			int r;

			DRM_INFO("enc %d is in group\n", enc_id);
			obj = drm_mode_object_find(dev, enc_id,
				DRM_MODE_OBJECT_ENCODER);
			BUG_ON(!obj);
			enc = obj_to_encoder(obj);
			r = search_encoder(dev, conn, enc, group, crtc_drmid,
				crtc_index);
			if (r)
				return r;
		}
	}
	if (*crtc_drmid < 0) {
		DRM_ERROR("conn %d has no crtcs\n", conn->base.id);
		return -ENODEV;
	}
	DRM_INFO("selected unique crtc %d:%d\n", *crtc_drmid, *crtc_index);
	return 0;
}

/*
 * maps the given dev_t to the corresponding drm_device.  also
 * determines whether given connector is in the minor, and if
 * so, selects the crtc within that minor to attach to.
 */
struct drm_connector *drm_vcrtcm_get_crtc_for_attach(dev_t dev,
	int connid, int *crtc_drmid, int *crtc_index)
{
	struct drm_minor *minor;
	struct drm_device *ddev;
	struct drm_mode_object *obj;
	struct drm_connector *conn;
	int r;

	if (MAJOR(dev) != DRM_MAJOR) {
		DRM_ERROR("wrong major %d\n", MAJOR(dev));
		return ERR_PTR(-EINVAL);
	}
	mutex_lock(&drm_global_mutex);
	minor = idr_find(&drm_minors_idr, MINOR(dev));
	if (!minor) {
		mutex_unlock(&drm_global_mutex);
		DRM_ERROR("unknown minor %d\n", MINOR(dev));
		return ERR_PTR(-ENODEV);
	}
	if (minor->type == DRM_MINOR_UNASSIGNED ||
		minor->type == DRM_MINOR_CONTROL) {
		mutex_unlock(&drm_global_mutex);
		DRM_ERROR("minor %d has inappropriate type %d\n", MINOR(dev),
			minor->type);
		return ERR_PTR(-EINVAL);
	}
	ddev = minor->dev;
	BUG_ON(!ddev);
	mutex_lock(&ddev->mode_config.mutex);
	if (!drm_vcrtcm_connector_is_in_group(connid, &minor->mode_group)) {
		mutex_unlock(&ddev->mode_config.mutex);
		mutex_unlock(&drm_global_mutex);
		DRM_ERROR("conn %d not in minor %d\n", connid, MINOR(dev));
		return ERR_PTR(-ENODEV);
	}
	obj = drm_mode_object_find(ddev, connid, DRM_MODE_OBJECT_CONNECTOR);
	BUG_ON(!obj);
	conn = obj_to_connector(obj);
	r = drm_vcrtcm_select_crtc_for_attach(ddev, conn,
		&minor->mode_group, crtc_drmid, crtc_index);
	if (r) {
		mutex_unlock(&ddev->mode_config.mutex);
		mutex_unlock(&drm_global_mutex);
		return ERR_PTR(r);
	}
	mutex_unlock(&ddev->mode_config.mutex);
	mutex_unlock(&drm_global_mutex);
	return conn;
}
EXPORT_SYMBOL(drm_vcrtcm_get_crtc_for_attach);
