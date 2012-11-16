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

#ifndef UDLPIM_VCRTCM_H_
#define UDLPIM_VCRTCM_H_

/* VCRTCM interface function prototypes */

int udlpim_attach(int pconid, void *cookie);
int udlpim_detach(int pconid, void *cookie);
int udlpim_set_fb(int pconid, void *cookie, struct vcrtcm_fb *vcrtcm_fb);
int udlpim_get_fb(int pconid, void *cookie, struct vcrtcm_fb *vcrtcm_fb);
int udlpim_set_fps(int pconid, void *cookie, int fps);
int udlpim_get_fps(int pconid, void *cookie, int *fps);
int udlpim_page_flip(int pconid, void *cookie, u32 ioaddr);
int udlpim_set_cursor(int pconid, void *cookie, struct vcrtcm_cursor *vcrtcm_cursor);
int udlpim_get_cursor(int pconid, void *cookie, struct vcrtcm_cursor *vcrtcm_cursor);
void udlpim_disable(int pconid, void *cookie);

/* VCRTCM functions that interact directly with HW */
int udlpim_dirty_fb(int pconid, void *cookie, struct drm_crtc *drm_crtc);
int udlpim_wait_fb(int pconid, void *cookie, struct drm_crtc *drm_crtc);
int udlpim_get_fb_status(int pconid, void *cookie, struct drm_crtc *drm_crtc, u32 *status);
int udlpim_set_dpms(int pconid, void *cookie, int state);
int udlpim_get_dpms(int pconid, void *cookie, int *state);
int udlpim_connected(int pconid, void *cookie, int *status);
int udlpim_get_modes(int pconid, void *cookie, struct vcrtcm_mode **modes,
			int *count);
int udlpim_check_mode(int pconid, void *cookie, struct vcrtcm_mode *mode,
			int *status);

/* Scheduled/delayed work functions */
void udlpim_fake_vblank(struct work_struct *work);
void copy_cursor_work(struct work_struct *work);
int udlpim_do_xmit_fb_pull(struct udlpim_pcon *pcon);
int udlpim_do_xmit_fb_push(struct udlpim_pcon *pcon);

int udlpim_instantiate(int pconid, uint32_t hints,
	void **cookie, struct vcrtcm_pcon_funcs *funcs,
	enum vcrtcm_xfer_mode *xfer_mode, int *minor,
	int *vblank_slack, char *description);
void udlpim_destroy(int pconid, void *cookie);
void udlpim_destroy_pcon(struct udlpim_pcon *pcon);
void udlpim_detach_pcon(struct udlpim_pcon *pcon);

#endif
