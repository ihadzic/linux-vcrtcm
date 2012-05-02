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

int udlpim_attach(struct vcrtcm_pcon_info *vcrtcm_pcon_info);
void udlpim_detach(struct vcrtcm_pcon_info *vcrtcm_pcon_info);
int udlpim_set_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct vcrtcm_fb *vcrtcm_fb);
int udlpim_get_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct vcrtcm_fb *vcrtcm_fb);
int udlpim_set_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int fps);
int udlpim_get_fps(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *fps);
int udlpim_page_flip(struct vcrtcm_pcon_info *vcrtcm_pcon_info, u32 ioaddr);
int udlpim_set_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct vcrtcm_cursor *vcrtcm_cursor);
int udlpim_get_cursor(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct vcrtcm_cursor *vcrtcm_cursor);
void udlpim_disable(struct vcrtcm_pcon_info *vcrtcm_pcon_info);

/* VCRTCM functions that interact directly with HW */
int udlpim_dirty_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct drm_crtc *drm_crtc);
int udlpim_wait_fb(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct drm_crtc *drm_crtc);
int udlpim_get_fb_status(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct drm_crtc *drm_crtc, u32 *status);
int udlpim_set_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int state);
int udlpim_get_dpms(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *state);
int udlpim_connected(struct vcrtcm_pcon_info *vcrtcm_pcon_info, int *status);
int udlpim_get_modes(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct vcrtcm_mode **modes,
			int *count);
int udlpim_check_mode(struct vcrtcm_pcon_info *vcrtcm_pcon_info, struct vcrtcm_mode *mode,
			int *status);

/* Scheduled/delayed work functions */
void udlpim_fake_vblank(struct work_struct *work);
void copy_cursor_work(struct work_struct *work);
int udlpim_do_xmit_fb_pull(struct udlpim_flow_info *flow_info);
int udlpim_do_xmit_fb_push(struct udlpim_flow_info *flow_info);

#endif
