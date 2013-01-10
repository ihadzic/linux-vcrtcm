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
#include <linux/slab.h>
#include "vcrtcm_drmdev_table.h"
#include "vcrtcm_utils_priv.h"

#define MAX_NUM_DEVICES 32

struct drmdev_table_entry {
	struct drm_device *dev;
	struct vcrtcm_g_drmdev_funcs funcs;
};

static struct drmdev_table_entry drmdev_table[MAX_NUM_DEVICES];
static DEFINE_SPINLOCK(drmdev_table_spinlock);

/*
 * assumes dev is not already in table
 */
int vcrtcm_set_drmdev_funcs(struct drm_device *dev,
	struct vcrtcm_g_drmdev_funcs *funcs)
{
	int k;
	unsigned long flags;

	spin_lock_irqsave(&drmdev_table_spinlock, flags);
	for (k = 0; k < MAX_NUM_DEVICES; ++k) {
		struct drmdev_table_entry *entry = &drmdev_table[k];
		if (!entry->dev) {
			entry->dev = dev;
			entry->funcs = *funcs;
			spin_unlock_irqrestore(&drmdev_table_spinlock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&drmdev_table_spinlock, flags);
	VCRTCM_ERROR("no free entry in device table\n");
	return -EINVAL;
}

int vcrtcm_get_drmdev_funcs(struct drm_device *dev,
	struct vcrtcm_g_drmdev_funcs *funcs)
{
	int k;
	unsigned long flags;

	spin_lock_irqsave(&drmdev_table_spinlock, flags);
	for (k = 0; k < MAX_NUM_DEVICES; ++k) {
		struct drmdev_table_entry *entry = &drmdev_table[k];
		if (entry->dev == dev) {
			*funcs = entry->funcs;
			spin_unlock_irqrestore(&drmdev_table_spinlock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&drmdev_table_spinlock, flags);
	VCRTCM_ERROR("dev %p not in device table\n", dev);
	return -EINVAL;
}

int vcrtcm_remove_drmdev_funcs(struct drm_device *dev)
{
	int k;
	unsigned long flags;

	spin_lock_irqsave(&drmdev_table_spinlock, flags);
	for (k = 0; k < MAX_NUM_DEVICES; ++k) {
		struct drmdev_table_entry *entry = &drmdev_table[k];
		if (entry->dev == dev) {
			entry->dev = NULL;
			spin_unlock_irqrestore(&drmdev_table_spinlock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&drmdev_table_spinlock, flags);
	VCRTCM_ERROR("dev %p not in device table\n", dev);
	return -EINVAL;
}
