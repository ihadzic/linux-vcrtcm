/*
 * Copyright (C) 2012 Alcatel-Lucent, Inc.
 * Author: Bill Katsak <william.katsak@alcatel-lucent.com>
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

#ifndef __VCRTCM_IOCTL_H__
#define __VCRTCM_IOCTL_H__

/* IOCTLs */
#define VCRTCM_MAGIC 'K'

/* args: pimid hints */
#define VCRTCM_IOC_INSTANTIATE _IOW(VCRTCM_MAGIC, 1, int)

/* args: pconid */
#define VCRTCM_IOC_DESTROY _IOW(VCRTCM_MAGIC, 2, int)

/* args: pimid testarg */
#define VCRTCM_IOC_PIMTEST _IOW(VCRTCM_MAGIC, 3, int)

/* args: drm-connector-id */
#define VCRTCM_IOC_CONNECT _IOW(VCRTCM_MAGIC, 4, int)

/* args: drm-connector-id */
#define VCRTCM_IOC_DISCONNECT _IOW(VCRTCM_MAGIC, 5, int)

/* args: pconid drm-connector-id */
#define VCRTCM_IOC_ATTACH _IOW(VCRTCM_MAGIC, 6, int)

/* args: pconid */
#define VCRTCM_IOC_DETACH _IOW(VCRTCM_MAGIC, 7, int)

/* args: pconid fps */
#define VCRTCM_IOC_FPS _IOW(VCRTCM_MAGIC, 8, int)

/* args: pconid */
#define VCRTCM_IOC_XMIT _IOW(VCRTCM_MAGIC, 9, int)

struct vcrtcm_ioctl_args {
	union {
		int pimid;
		int pconid;
		int connid;
	} arg1;
	union {
		uint32_t hints;
		uint32_t testarg;
		int connid;
	} arg2;
	union {
		int pconid;
	} result1;
};

#endif
