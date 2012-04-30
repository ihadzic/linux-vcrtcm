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

#ifndef __UDLPCON_H
#define __UDLPCON_H

#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <vcrtcm/vcrtcm_pcon.h>

#define UDLPCON_MAX_DEVICES 64 /* This is currently a hard limitation. */
#define UDLPCON_FPS_HARD_LIMIT 100
#define UDLPCON_DEFAULT_PIXEL_DEPTH 32
#define UDLPCON_XFER_MAX_TRY 20
#define UDLPCON_XFER_TIMEOUT (300*HZ/1000) /*5*HZ/1000*/
#define UDLPCON_XMIT_HARD_DEADLINE (HZ/10)

#define UDLPCON_IN_DO_XMIT 0x1

#define UDLPCON_ALLOC_PB_FLAG_FB 0x0
#define UDLPCON_ALLOC_PB_FLAG_CURSOR 0x1
#define UDLPCON_ALLOC_PB_STRING(x) ((x) ? "cursor" : "framebuffer")

#define UDLPCON_BLANK_COLOR 0x0080c8
#define UDLPCON_ERROR_COLOR 0xFF0000

#define UDLPCON_EDID_QUERY_TIME HZ
#define UDLPCON_EDID_QUERY_TRIES 3

/* Module options */
extern int true32bpp;
extern int debug;
extern int enable_default_modes;

extern struct usb_driver udlpcon_driver;
extern struct list_head udlpcon_info_list;
extern int udlpcon_major;
extern int udlpcon_num_minors;
extern int udlpcon_max_minor;
extern int udlpcon_fake_vblank_slack;
extern struct vcrtcm_pcon_funcs udlpcon_vcrtcm_pcon_funcs;
extern struct vcrtcm_hw_props udlpcon_vcrtcm_hw_props;

struct urb_node {
	struct list_head entry;
	struct udlpcon_info *dev;
	struct delayed_work release_urb_work;
	struct urb *urb;
};

struct urb_list {
	struct list_head list;
	spinlock_t lock;
	struct semaphore limit_sem;
	int available;
	int count;
	size_t size;
};

struct udlpcon_video_mode {
	struct list_head list;
	u32 xres;
	u32 yres;
	u32 pixclock;
	u32 left_margin;
	u32 right_margin;
	u32 upper_margin;
	u32 lower_margin;
	u32 hsync_len;
	u32 vsync_len;
	u32 refresh;
};

struct udlpcon_scratch_memory_descriptor {
	struct page **backing_buffer_pages;
	struct page **hline_16_pages;
	struct page **hline_8_pages;
	unsigned int backing_buffer_num_pages;
	unsigned int hline_16_num_pages;
	unsigned int hline_8_num_pages;
};

struct udlpcon_info {
	/* vcrtcm stuff */
	struct list_head list;
	int minor;
	struct udlpcon_vcrtcm_hal_descriptor *udlpcon_vcrtcm_hal_descriptor;
	struct mutex buffer_mutex;
	spinlock_t udlpcon_lock;
	int enabled_queue;
	unsigned long status;
	wait_queue_head_t xmit_sync_queue;

	struct workqueue_struct *workqueue;

	struct delayed_work fake_vblank_work;
	struct delayed_work query_edid_work;

	/* displaylink specific stuff */
	char *edid;
	size_t edid_size;
	int sku_pixel_limit;
	int base16;
	int base8;
	char *main_buffer;
	char *backing_buffer;
	char *hline_16;
	char *hline_8;
	char *cursor;
	int bpp;

	struct udlpcon_scratch_memory_descriptor *scratch_memory;

	/* supported fb modes */
	struct udlpcon_video_mode default_video_mode;
	struct udlpcon_video_mode current_video_mode;
	struct vcrtcm_mode *last_vcrtcm_mode_list;
	int monitor_connected;

	/* usb stuff */
	struct usb_device *udev;
	struct device *gdev;
	struct urb_list urbs;
	struct kref kref;
	bool virtualized; /* true when physical usb device not present */
	atomic_t usb_active; /* 0 = update virtual buffer but no usb traffic */
	atomic_t lost_pixels; /* 1 = a render op failed, Need screen refresh */

	atomic_t bytes_rendered; /* raw pixel-bytes driver asked to render */
	atomic_t bytes_identical; /* saved effort with backbuffer comparison */
	atomic_t bytes_sent; /* to usb, after compression including overhead */
	atomic_t cpu_kcycles_used; /* transpired during pixel processing */

	/* debug stuff */
	int page_track;
	int kmalloc_track;
	int vmalloc_track;
};

struct udlpcon_vcrtcm_hal_descriptor {
	struct list_head list;
	int fb_xmit_counter;
	int fb_force_xmit;
	int fb_xmit_allowed;
	unsigned long fb_xmit_period_jiffies;
	unsigned long last_xmit_jiffies;
	unsigned long next_vblank_jiffies;
	struct vcrtcm_pcon_info *vcrtcm_pcon_info;
	struct vcrtcm_fb vcrtcm_fb;
	struct vcrtcm_cursor vcrtcm_cursor;
	struct vcrtcm_push_buffer_descriptor pbd_fb[2];
	struct vcrtcm_push_buffer_descriptor pbd_cursor[2];
	void *pb_fb[2];
	void *pb_cursor[2];
	int pb_needs_xmit[2];
	int push_buffer_index;

	int dpms_state;

	struct udlpcon_info *udlpcon_info;
};


/* USB/HW functions that VCRTCM functions need access to */
int udlpcon_setup_screen(struct udlpcon_info *udlpcon_info,
	struct udlpcon_video_mode *mode, struct vcrtcm_fb *vcrtcm_fb);
int udlpcon_error_screen(struct udlpcon_info *udlpcon_info);
int udlpcon_dpms_sleep(struct udlpcon_info *udlpcon_info);
int udlpcon_dpms_wakeup(struct udlpcon_info *udlpcon_info);
int udlpcon_transmit_framebuffer(struct udlpcon_info *udlpcon_info);
int udlpcon_build_modelist(struct udlpcon_info *udlpcon_info,
			struct udlpcon_video_mode **modes, int *mode_count);
int udlpcon_free_modelist(struct udlpcon_info *udlpcon_info,
			struct udlpcon_video_mode *modes);
void udlpcon_query_edid_core(struct udlpcon_info *udlpcon_info);

#endif
