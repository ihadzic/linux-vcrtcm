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
 * Author: Ilija Hadzic <ihadzic@research.bell-labs.com>
 */

#ifndef __RADEON_VCRTCM_KERNEL_H__
#define __RADEON_VCRTCM_KERNEL_H__

#include <vcrtcm/vcrtcm_gpu.h>
#include "radeon_vcrtcm.h"

int radeon_vcrtcm_detach(struct radeon_crtc *radeon_crtc);
int radeon_vcrtcm_wait(struct radeon_device *rdev);
void radeon_vcrtcm_xmit(struct radeon_device *rdev);
int radeon_vcrtcm_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int radeon_vcrtcm_set_fb(struct radeon_crtc *radeon_crtc,
			 int x, int y,
			 struct drm_framebuffer *fb,
			 uint64_t fb_location);
int radeon_vcrtcm_page_flip(struct radeon_crtc *radeon_crtc,
			    u64 base);
#endif
