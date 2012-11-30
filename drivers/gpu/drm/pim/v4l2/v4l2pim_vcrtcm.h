/*
   Copyright (C) 2011 Alcatel-Lucent, Inc.
   Authors: Hans Christian Woithe <hans.woithe@alcatel-lucent.com>
		Bill Katsak <william.katsak@alcatel-lucent.com>

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

#ifndef V4L2PIM_VCRTCM_H
#define V4L2PIM_VCRTCM_H

/* VCRTCM interface function prototypes */

int v4l2pim_attach(int pconid, void *cookie);
int v4l2pim_detach(int pconid, void *cookie);
int v4l2pim_set_fb(int pconid, void *cookie, struct vcrtcm_fb *vcrtcm_fb);
int v4l2pim_get_fb(int pconid, void *cookie, struct vcrtcm_fb *vcrtcm_fb);
int v4l2pim_set_fps(int pconid, void *cookie, int fps);
int v4l2pim_get_fps(int pconid, void *cookie, int *fps);
int v4l2pim_set_cursor(int pconid, void *cookie, struct vcrtcm_cursor *vcrtcm_cursor);
int v4l2pim_get_cursor(int pconid, void *cookie, struct vcrtcm_cursor *vcrtcm_cursor);
void v4l2pim_disable(int pconid, void *cookie);
int v4l2pim_instantiate(int pconid, uint32_t hints,
	void **cookie, struct vcrtcm_pcon_funcs *funcs,
	enum vcrtcm_xfer_mode *xfer_mode, int *minor,
	int *vblank_slack, char *description);
void v4l2pim_destroy(int pconid, void *cookie);

/* VCRTCM functions that interact directly with HW */
int v4l2pim_dirty_fb(int pconid, void *cookie);
int v4l2pim_wait_fb(int pconid, void *cookie);
int v4l2pim_get_fb_status(int pconid, void *cookie, u32 *status);
int v4l2pim_set_dpms(int pconid, void *cookie, int state);
int v4l2pim_get_dpms(int pconid, void *cookie, int *state);

void copy_cursor_work(struct work_struct *work);
void v4l2pim_destroy_pcon(struct v4l2pim_pcon *pcon);
int v4l2pim_detach_pcon(struct v4l2pim_pcon *pcon);

#endif
