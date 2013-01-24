/*
 * Copyright (C) 2012 Alcatel-Lucent, Inc.
 * Authors:
 *      Ilija Hadzic <ihadzic@research.bell-labs.com>
 *      Martin Carroll <martin.carroll@research.bell-labs.com>
 *      William Katsak <wkatsak@cs.rutgers.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/string.h>
#include <drm/drmP.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include "vcrtcm_conn.h"

static DEFINE_MUTEX(conn_list_mutex);
static LIST_HEAD(conn_list);

void vcrtcm_lock_conntbl(void)
{
	mutex_lock(&conn_list_mutex);
}

void vcrtcm_unlock_conntbl(void)
{
	mutex_unlock(&conn_list_mutex);
}

struct vcrtcm_conn *vcrtcm_get_conn(struct drm_connector *drm_conn,
	struct vcrtcm_drmdev *vdev)
{
	struct vcrtcm_conn *conn;

	list_for_each_entry(conn, &conn_list, conn_list) {
		if (conn->drm_conn == drm_conn)
			BUG_ON(conn->vdev != vdev);
			return conn;
	}
	conn = (struct vcrtcm_conn *)vcrtcm_kzalloc(
		sizeof(struct vcrtcm_conn), GFP_KERNEL, VCRTCM_OWNER_VCRTCM);
	if (!conn) {
		VCRTCM_ERROR("cannot allocate memory for conn\n");
		return NULL;
	}
	INIT_LIST_HEAD(&conn->conn_list);
	conn->drm_conn = drm_conn;
	conn->vdev = vdev;
	list_add_tail(&conn->conn_list, &conn_list);
	return conn;
}

void vcrtcm_free_conn(struct vcrtcm_conn *conn)
{
	list_del(&conn->conn_list);
	vcrtcm_kfree(conn);
}
