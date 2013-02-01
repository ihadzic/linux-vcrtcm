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

#ifndef __VCRTCM_DRMDEV_H__
#define __VCRTCM_DRMDEV_H__

#include <vcrtcm/vcrtcm_gpu.h>

struct vcrtcm_drmdev {
	struct drm_device *dev;
	struct vcrtcm_g_drmdev_funcs funcs;
	struct kobject kobj;
	struct kobject conns_kobj;
};

struct vcrtcm_drmdev *vcrtcm_add_drmdev(struct drm_device *dev,
	struct vcrtcm_g_drmdev_funcs *funcs);
struct vcrtcm_drmdev *vcrtcm_get_drmdev(struct drm_device *dev);
int vcrtcm_remove_drmdev(struct drm_device *dev);
int vcrtcm_drmdev_minor(struct drm_device *dev);

#endif
