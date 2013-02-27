/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
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


/*
     This file defines those things that are common to the VCRTCM-PIM
     and VCRTCM-GPU APIs.
*/

#ifndef __VCRTCM_COMMON_H__
#define __VCRTCM_COMMON_H__

#define VCRTCM_FB_STATUS_IDLE  0x0
#define VCRTCM_FB_STATUS_XMIT  0x1
#define VCRTCM_FB_STATUS_PUSH  0x2

#define VCRTCM_DPMS_STATE_ON  0x1
#define VCRTCM_DPMS_STATE_OFF 0x0

#define VCRTCM_MODE_OK  0
#define VCRTCM_MODE_BAD 1

#define VCRTCM_PCON_DISCONNECTED 0
#define VCRTCM_PCON_CONNECTED    1

#define VCRTCM_CURSOR_FLAG_HIDE 0x1
#define VCRTCM_PFLIP_DEFERRED 1

enum vcrtcm_xfer_mode {
	VCRTCM_XFERMODE_UNSPECIFIED,
	VCRTCM_PEER_PULL,
	VCRTCM_PEER_PUSH,
	VCRTCM_PUSH_PULL
};

/* framebuffer/CRTC (emulated) registers */
struct vcrtcm_fb {
	u32 ioaddr;
	unsigned int bpp;
	unsigned int width;
	unsigned int pitch;
	unsigned int height;
	unsigned int viewport_x;
	unsigned int viewport_y;
	unsigned int hdisplay;
	unsigned int vdisplay;
};

struct vcrtcm_cursor {
	u32 ioaddr;
	unsigned int bpp;
	int location_x;
	int location_y;
	unsigned int height;
	unsigned int width;
	unsigned int flag;
};

struct vcrtcm_mode {
	int w;
	int h;
	int refresh;
};

const char *vcrtcm_xfer_mode_string(enum vcrtcm_xfer_mode mode);

#endif
