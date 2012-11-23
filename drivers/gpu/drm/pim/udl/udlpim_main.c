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
int udlpim_log_pim_alloc_counts;
int udlpim_log_pcon_alloc_counts;

struct list_head udlpim_minor_list;
int udlpim_major = -1;
int udlpim_num_minors = -1;
int udlpim_fake_vblank_slack = 1;
int udlpim_pimid = -1;

/* Use to generate minor numbers */
struct vcrtcm_id_generator udlpim_minor_id_generator;

static struct vcrtcm_pim_funcs udlpim_pim_funcs = {
	.instantiate = udlpim_instantiate,
	.destroy = udlpim_destroy
};

static int __init udlpim_init(void)
{
	int r;

	VCRTCM_INFO("DisplayLink USB PCON, (C) Bell Labs, Alcatel-Lucent, Inc.\n");
	VCRTCM_INFO("Push mode enabled");
	r = vcrtcm_alloc_major(&udlpim_major, UDLPIM_MAX_MINORS, UDLPIM_PIM_NAME);
	if (r)
		return r;
	vcrtcm_pim_register(UDLPIM_PIM_NAME, &udlpim_pim_funcs, &udlpim_pimid);
	vcrtcm_pim_log_alloc_cnts(udlpim_pimid, udlpim_log_pim_alloc_counts);
	INIT_LIST_HEAD(&udlpim_minor_list);
	vcrtcm_id_generator_init(&udlpim_minor_id_generator,
					UDLPIM_MAX_MINORS);
	udlpim_num_minors = 0;
	r = usb_register(&udlpim_driver);
	if (r) {
		unregister_chrdev_region(MKDEV(udlpim_major, 0), UDLPIM_MAX_MINORS);
		vcrtcm_pim_unregister(udlpim_pimid);
		VCRTCM_ERROR("usb_register failed, error %d", r);
		return r;
	}
	vcrtcm_pim_enable_callbacks(udlpim_pimid);
	return 0;
}

static void __exit udlpim_exit(void)
{
	struct udlpim_minor *minor;
	struct udlpim_minor *tmp;

	VCRTCM_INFO("shutting down udlpim\n");
	vcrtcm_pim_disable_callbacks(udlpim_pimid);
	unregister_chrdev_region(MKDEV(udlpim_major, 0), UDLPIM_MAX_MINORS);
	list_for_each_entry_safe(minor, tmp, &udlpim_minor_list, list) {
		if (minor->pcon) {
			udlpim_detach_pcon(minor->pcon);
			udlpim_destroy_pcon(minor->pcon);
		}
	}
	/* must deregister usb before unregistering pim, because
	* deregistering usb causes the minors to be destroyed,
	* and when the minors are destroyed they return their
	* allocations to vcrtcm
	*/
	usb_deregister(&udlpim_driver);
	vcrtcm_pim_unregister(udlpim_pimid);
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

module_param_named(major, udlpim_major, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(major, "Major device number (default=dynamic)");

module_param_named(enable_default_modes, udlpim_enable_default_modes, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(enable_default_modes,
	"Support standard VESA modes if the monitor doesn't provide any.");
module_param_named(log_pim_alloc_cnts, udlpim_log_pim_alloc_counts,
		   int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(log_pim_alloc_cnts,
		 "When set to 1, log all per-PIM alloc counts (default = 0)");
module_param_named(log_pcon_alloc_cnts, udlpim_log_pcon_alloc_counts,
		   int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(log_pcon_alloc_cnts,
		 "When set to 1, log all per-PCON alloc counts (default = 0)");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DisplayLink USB PCON");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
