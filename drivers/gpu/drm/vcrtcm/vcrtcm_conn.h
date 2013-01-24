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

#ifndef __VCRTCM_CONN_H__
#define __VCRTCM_CONN_H__

struct drm_connector;
struct vcrtcm_drmdev;

struct vcrtcm_conn {
	struct drm_connector *drm_conn;
	struct vcrtcm_drmdev *vdev;
	struct kobject kobj;
	struct kobject pcons_kobj;
	int num_attached_pcons;
	struct list_head conn_list;
};

void vcrtcm_lock_conntbl(void);
void vcrtcm_unlock_conntbl(void);
struct vcrtcm_conn *vcrtcm_get_conn(struct drm_connector *drm_conn,
	struct vcrtcm_drmdev *vdev);
void vcrtcm_free_conn(struct vcrtcm_conn *conn);

#endif
