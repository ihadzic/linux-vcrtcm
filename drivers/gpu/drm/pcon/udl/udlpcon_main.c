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

#include "udlpcon.h"
#include "udlpcon_vcrtcm.h"
#include "udlpcon_utils.h"


/* Module option(s) */
int true32bpp; /* Enable experimental (and buggy) true 32bpp color. */
int debug; /* Enable the printing of debugging information */
int enable_default_modes; /* Use standard VESA modes if we can't get EDID. */

struct list_head udlpcon_info_list;
int udlpcon_major = -1;
int udlpcon_num_minors = -1;
int udlpcon_max_minor = -1;
int udlpcon_fake_vblank_slack = 1;

struct vcrtcm_pcon_funcs udlpcon_vcrtcm_pcon_funcs = {
	.attach = udlpcon_attach,
	.detach = udlpcon_detach,
	.set_fb = udlpcon_set_fb,
	.get_fb = udlpcon_get_fb,
	.dirty_fb = udlpcon_dirty_fb,
	.wait_fb = udlpcon_wait_fb,
	.get_fb_status = udlpcon_get_fb_status,
	.set_fps = udlpcon_set_fps,
	.get_fps = udlpcon_get_fps,
	.set_cursor = udlpcon_set_cursor,
	.get_cursor = udlpcon_get_cursor,
	.set_dpms = udlpcon_set_dpms,
	.get_dpms = udlpcon_get_dpms,
	.connected = udlpcon_connected,
	.get_modes = udlpcon_get_modes,
	.check_mode = udlpcon_check_mode,
	.disable = udlpcon_disable
};


struct vcrtcm_pcon_props udlpcon_vcrtcm_pcon_props = {
	.xfer_mode = VCRTCM_PUSH_PULL
};

static int __init udlpcon_init(void)
{
	int ret;
	dev_t dev;

	PR_INFO("DisplayLink USB PCON, "
	"(C) Bell Labs, Alcatel-Lucent, Inc.\n");
	PR_INFO("Push mode enabled");

	INIT_LIST_HEAD(&udlpcon_info_list);

	PR_INFO("Allocating/registering dynamic major number");
	ret = alloc_chrdev_region(&dev, 0, UDLPCON_MAX_DEVICES, "udlpcon");
	udlpcon_major = MAJOR(dev);

	if (ret) {
		PR_WARN("Can't get major device number, driver unusable\n");
		udlpcon_major = -1;
		udlpcon_num_minors = 0;
	} else {
		PR_INFO("Using major device number %d\n", udlpcon_major);
	}

	udlpcon_num_minors = 0;
	ret = usb_register(&udlpcon_driver);

	if (ret) {
		PR_ERR("usb_register failed. Error number %d", ret);
		return ret;
	}

	return 0;
}

static void __exit udlpcon_exit(void)
{
	PR_INFO("Cleaning up udlpcon\n");
	usb_deregister(&udlpcon_driver);

	if (udlpcon_major >= -1) {
		PR_INFO
		("Deallocating major device number %d, count %d\n",
			udlpcon_major, UDLPCON_MAX_DEVICES);
		unregister_chrdev_region(MKDEV(udlpcon_major, 0), UDLPCON_MAX_DEVICES);
	}

	return;
}

module_init(udlpcon_init);
module_exit(udlpcon_exit);

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
