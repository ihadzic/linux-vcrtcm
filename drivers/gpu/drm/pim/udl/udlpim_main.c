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
#include <vcrtcm/vcrtcm_pim.h>

#include "udlpim.h"
#include "udlpim_vcrtcm.h"

/* Module option(s) */
int udlpim_true32bpp; /* Enable experimental (and buggy) true 32bpp color. */
int udlpim_debug; /* Enable the printing of debugging information */
int udlpim_enable_default_modes; /* Use standard VESA modes if we can't get EDID. */

struct list_head udlpim_minor_list;
int udlpim_major = -1;
int udlpim_num_minors = -1;
int udlpim_fake_vblank_slack = 1;

/* Use to generate minor numbers */
struct vcrtcm_id_generator udlpim_minor_id_generator;

static struct vcrtcm_pim_funcs udlpim_pim_funcs = {
	.instantiate = udlpim_instantiate,
	.destroy = udlpim_destroy
};

static int __init udlpim_init(void)
{
	int r;
	dev_t dev;

	VCRTCM_INFO("DisplayLink USB PCON, (C) Bell Labs, Alcatel-Lucent, Inc.\n");
	VCRTCM_INFO("Push mode enabled");
	INIT_LIST_HEAD(&udlpim_minor_list);
	vcrtcm_id_generator_init(&udlpim_minor_id_generator,
					UDLPIM_MAX_DEVICES);
	VCRTCM_INFO("Allocating/registering dynamic major number");
	r = alloc_chrdev_region(&dev, 0, UDLPIM_MAX_DEVICES, "udlpim");
	if (r) {
		VCRTCM_WARNING("cannot get major device number\n");
		return r;
	}
	udlpim_major = MAJOR(dev);
	VCRTCM_INFO("Using major device number %d\n", udlpim_major);
	udlpim_num_minors = 0;
	r = usb_register(&udlpim_driver);
	if (r) {
		VCRTCM_ERROR("usb_register failed, error %d", r);
		return r;
	}
	vcrtcm_pim_register("udl", &udlpim_pim_funcs);
	return 0;
}

static void __exit udlpim_exit(void)
{
	VCRTCM_INFO("shutting down udlpim\n");
	vcrtcm_pim_unregister("udl");
	unregister_chrdev_region(MKDEV(udlpim_major, 0), UDLPIM_MAX_DEVICES);
	usb_deregister(&udlpim_driver);
	vcrtcm_id_generator_destroy(&udlpim_minor_id_generator);
	VCRTCM_INFO("exiting udlpim\n");
}

module_init(udlpim_init);
module_exit(udlpim_exit);

module_param(udlpim_true32bpp, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(udlpim_true32bpp,
	"Enable support for true 32bpp color. *Experimental and buggy*");

module_param_named(debug, udlpim_debug, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(debug, "Enable debugging information.");

module_param_named(enable_default_modes, udlpim_enable_default_modes, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(enable_default_modes,
	"Support standard VESA modes if the monitor doesn't provide any.");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DisplayLink USB PCON");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
