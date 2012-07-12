/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Author: Bill Katsak <william.katsak@alcatel-lucent.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/delay.h>
#include <linux/prefetch.h>
#include <vcrtcm/vcrtcm_pcon.h>
#include <vcrtcm/pimmgr.h>

#include "udlpim.h"
#include "udlpim_vcrtcm.h"

/* Module option(s) */
int true32bpp; /* Enable experimental (and buggy) true 32bpp color. */
int debug; /* Enable the printing of debugging information */
int enable_default_modes; /* Use standard VESA modes if we can't get EDID. */

struct list_head udlpim_info_list;
int udlpim_major = -1;
int udlpim_num_minors = -1;
int udlpim_max_minor = -1;
int udlpim_fake_vblank_slack = 1;

struct vcrtcm_pcon_funcs udlpim_vcrtcm_pcon_funcs = {
	.attach = udlpim_attach,
	.detach = udlpim_detach,
	.set_fb = udlpim_set_fb,
	.get_fb = udlpim_get_fb,
	.dirty_fb = udlpim_dirty_fb,
	.wait_fb = udlpim_wait_fb,
	.get_fb_status = udlpim_get_fb_status,
	.set_fps = udlpim_set_fps,
	.get_fps = udlpim_get_fps,
	.set_cursor = udlpim_set_cursor,
	.get_cursor = udlpim_get_cursor,
	.set_dpms = udlpim_set_dpms,
	.get_dpms = udlpim_get_dpms,
	.connected = udlpim_connected,
	.get_modes = udlpim_get_modes,
	.check_mode = udlpim_check_mode,
	.disable = udlpim_disable
};


struct vcrtcm_pcon_props udlpim_vcrtcm_pcon_props = {
	.xfer_mode = VCRTCM_PUSH_PULL
};

static int udlpim_instantiate(struct pcon_instance_info *instance_info,
				void *data, uint32_t hints);
static void udlpim_destroy(uint32_t local_pcon_id, void *data);

static struct pim_funcs udlpim_pim_funcs = {
	.instantiate = udlpim_instantiate,
	.destroy = udlpim_destroy
};

static int __init udlpim_init(void)
{
	int ret;
	dev_t dev;

	VCRTCM_INFO("DisplayLink USB PCON, "
	"(C) Bell Labs, Alcatel-Lucent, Inc.\n");
	VCRTCM_INFO("Push mode enabled");

	INIT_LIST_HEAD(&udlpim_info_list);

	VCRTCM_INFO("Allocating/registering dynamic major number");
	ret = alloc_chrdev_region(&dev, 0, UDLPIM_MAX_DEVICES, "udlpim");
	udlpim_major = MAJOR(dev);

	if (ret) {
		VCRTCM_WARNING("Can't get major device number, driver unusable\n");
		udlpim_major = -1;
		udlpim_num_minors = 0;
	} else {
		VCRTCM_INFO("Using major device number %d\n", udlpim_major);
	}

	udlpim_num_minors = 0;
	ret = usb_register(&udlpim_driver);

	if (ret) {
		VCRTCM_ERROR("usb_register failed. Error number %d", ret);
		return ret;
	}

	pimmgr_pim_register("udl", &udlpim_pim_funcs, NULL);

	return 0;
}

static void __exit udlpim_exit(void)
{
	VCRTCM_INFO("Cleaning up udlpim\n");
	usb_deregister(&udlpim_driver);

	if (udlpim_major >= -1) {
		VCRTCM_INFO
		("Deallocating major device number %d, count %d\n",
			udlpim_major, UDLPIM_MAX_DEVICES);
		unregister_chrdev_region(MKDEV(udlpim_major, 0), UDLPIM_MAX_DEVICES);
	}

	pimmgr_pim_unregister("udl");

	return;
}

static int udlpim_instantiate(struct pcon_instance_info *instance_info,
				void *data, uint32_t hints)
{
	struct udlpim_info *info;

	list_for_each_entry(info, &udlpim_info_list, list) {
		if (!info->used) {
			instance_info->funcs = &udlpim_vcrtcm_pcon_funcs;
			instance_info->props = &udlpim_vcrtcm_pcon_props;
			instance_info->cookie = info;
			instance_info->local_id = (uint32_t) info->minor;
			info->used = 1;
			return 1;
		}
	}

	return 0;
}

static void udlpim_destroy(uint32_t local_pcon_id, void *data)
{
	struct udlpim_info *info;

	list_for_each_entry(info, &udlpim_info_list, list) {
		if (((uint32_t) info->minor) == local_pcon_id) {
			info->used = 0;
			return;
		}
	}
}

module_init(udlpim_init);
module_exit(udlpim_exit);

module_param(true32bpp, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(true32bpp,
	"Enable support for true 32bpp color. *Experimental and buggy*");

module_param(debug, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(debug, "Enable debugging information.");

module_param(enable_default_modes, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(enable_default_modes,
	"Support standard VESA modes if the monitor doesn't provide any.");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DisplayLink USB PCON");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
