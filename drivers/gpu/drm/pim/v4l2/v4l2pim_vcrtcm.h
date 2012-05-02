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

#ifndef V4L2PCON_VCRTCM_H
#define V4L2PCON_VCRTCM_H

/* VCRTCM interface function prototypes */

int v4l2pim_attach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *v4l2pim_info_, int flow);

void v4l2pim_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *v4l2pim_info_, int flow);

int v4l2pim_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *v4l2pim_info_, int flow);

int v4l2pim_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *v4l2pim_info_, int flow);

int v4l2pim_set_fps(int fps, void *v4l2pim_info_, int flow);

int v4l2pim_get_fps(int *fps, void *v4l2pim_info_, int flow);

int v4l2pim_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *v4l2pim_info_, int flow);

int v4l2pim_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *v4l2pim_info_, int flow);

void v4l2pim_disable(void *v4l2pim_info_, int flow);

/* VCRTCM functions that interact directly with HW */
int v4l2pim_dirty_fb(struct drm_crtc *drm_crtc, void *v4l2pim_info_, int flow);
int v4l2pim_wait_fb(struct drm_crtc *drm_crtc, void *v4l2pim_info_, int flow);
int v4l2pim_get_fb_status(struct drm_crtc *drm_crtc,
		void *v4l2pim_info_, int flow, u32 *status);
int v4l2pim_set_dpms(int state, void *v4l2pim_info_, int flow);
int v4l2pim_get_dpms(int *state, void *v4l2pim_info_, int flow);

/* Scheduled/delayed work functions */
void v4l2pim_fake_vblank(struct work_struct *work);
void copy_cursor_work(struct work_struct *work);
int v4l2pim_do_xmit_fb_pull(struct v4l2pim_flow_info *flow_info);
int v4l2pim_do_xmit_fb_push(struct v4l2pim_flow_info *flow_info);

#endif
