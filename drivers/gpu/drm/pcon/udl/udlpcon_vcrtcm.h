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

#ifndef UDLPCON_VCRTCM_H_
#define UDLPCON_VCRTCM_H_

/* VCRTCM interface function prototypes */

int udlpcon_attach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *udlpcon_info_, int flow);

void udlpcon_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info,
			void *udlpcon_info_, int flow);

int udlpcon_set_fb(struct vcrtcm_fb *vcrtcm_fb, void *udlpcon_info_,
			int flow);

int udlpcon_get_fb(struct vcrtcm_fb *vcrtcm_fb, void *udlpcon_info_,
			int flow);

int udlpcon_set_fps(int fps, void *udlpcon_info_, int flow);

int udlpcon_get_fps(int *fps, void *udlpcon_info_, int flow);

int udlpcon_page_flip(u32 ioaddr, void *udlpcon_info_, int flow);

int udlpcon_set_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *udlpcon_info_, int flow);

int udlpcon_get_cursor(struct vcrtcm_cursor *vcrtcm_cursor,
				void *udlpcon_info_, int flow);

void udlpcon_disable(void *udlpcon_info_, int flow);

/* VCRTCM functions that interact directly with HW */
int udlpcon_dirty_fb(struct drm_crtc *drm_crtc, void *udlpcon_info_, int flow);
int udlpcon_wait_fb(struct drm_crtc *drm_crtc, void *udlpcon_info_, int flow);
int udlpcon_get_fb_status(struct drm_crtc *drm_crtc,
		void *udlpcon_info_, int flow, u32 *status);
int udlpcon_set_dpms(int state, void *udlpcon_info_, int flow);
int udlpcon_get_dpms(int *state, void *udlpcon_info_, int flow);
int udlpcon_connected(void *udlpcon_info_, int flow, int *status);
int udlpcon_get_modes(void *udlpcon_info_, int flow, struct vcrtcm_mode **modes,
			int *count);
int udlpcon_check_mode(void *udlpcon_info_, int flow, struct vcrtcm_mode *mode,
			int *status);

/* Scheduled/delayed work functions */
void udlpcon_fake_vblank(struct work_struct *work);
void copy_cursor_work(struct work_struct *work);
int udlpcon_do_xmit_fb_pull(struct udlpcon_flow_info *uvhd);
int udlpcon_do_xmit_fb_push(struct udlpcon_flow_info *uvhd);

#endif
