/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Author: Ilija Hadzic <ihadzic@research.bell-labs.com>

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


/* Various utility macros, constants and functions
   for Virtual CRTC Manager */

#ifndef __VCRTCM_UTILS_H__
#define __VCRTCM_UTILS_H__

extern int vcrtcm_debug;

#define VCRTCM_INFO(fmt, args...) printk(KERN_INFO "[vcrtcm] " fmt, ## args)
#define VCRTCM_ERROR(fmt, args...) printk(KERN_ERR "[vcrtcm] " fmt, ## args)
#define VCRTCM_WARNING(fmt, args...) printk(KERN_WARNING "[vcrtcm] " fmt, ## args)
#define VCRTCM_DEBUG(fmt, args...)					\
	do {								\
		if (vcrtcm_debug)					\
			printk(KERN_DEBUG "[vcrtcm] " fmt, ## args);	\
	} while (0)

#endif
