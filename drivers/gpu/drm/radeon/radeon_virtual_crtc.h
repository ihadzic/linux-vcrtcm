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

#ifndef __RADEON_VIRTUAL_CRTC_H__
#define __RADEON_VIRTUAL_CRTC_H__

struct virtual_crtc {
	struct list_head list;
	struct radeon_crtc *radeon_crtc;
};

int radeon_virtual_crtc_do_set_base(struct drm_crtc *crtc,
				    struct drm_framebuffer *fb,
				    int x, int y, int atomic);
void radeon_virtual_crtc_handle_flip(struct radeon_device *rdev,
				     int crtc_id);
void radeon_add_virtual_enc_conn(struct drm_device *dev, int inst);
void radeon_virtual_crtc_init(struct drm_device *dev, int index);
void radeon_virtual_crtc_data_init(struct radeon_crtc *radeon_crtc);
int radeon_virtual_crtc_cursor_set(struct drm_crtc *crtc,
				   struct drm_file *file_priv,
				   uint32_t handle,
				   uint32_t width, uint32_t height);
int radeon_virtual_crtc_cursor_move(struct drm_crtc *crtc, int x, int y);
int radeon_hide_virtual_cursor(struct radeon_crtc *radeon_crtc);
int radeon_show_and_set_virtual_cursor(struct radeon_crtc *radeon_crtc,
				       struct drm_gem_object *obj,
				       uint64_t cursor_gpuaddr);
void radeon_emulate_vblank_core(struct radeon_device *rdev,
				struct radeon_crtc *radeon_crtc);
int radeon_virtual_crtc_get_vblank_timestamp_kms(struct drm_device *dev, int crtc,
					int *max_error,
					struct timeval *vblank_time,
					unsigned flags);
struct virtual_crtc *radeon_virtual_crtc_lookup(struct radeon_device *rdev,
	int crtc_id);
void radeon_virtual_crtc_set_emulated_vblank_time(struct radeon_crtc *radeon_crtc);

#endif
