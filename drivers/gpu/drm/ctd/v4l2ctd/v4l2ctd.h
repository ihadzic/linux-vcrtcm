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


#ifndef __v4l2ctd_H
#define __v4l2ctd_H

#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/videobuf-vmalloc.h>
#include <linux/videodev2.h>

#include "vcrtcm/vcrtcm_ctd.h"

#define V4L2CTD_NUM_MINORS 1
#define V4L2CTD_BPP 32
#define V4L2CTD_FPS_HARD_LIMIT 60
#define V4L2CTD_DEFAULT_PIXEL_DEPTH 32
#define V4L2CTD_XFER_MAX_TRY 20
#define V4L2CTD_XFER_TIMEOUT (300*HZ/1000)
#define V4L2CTD_XMIT_HARD_DEADLINE HZ
#define V4L2CTD_FB_PULL 0
#define V4L2CTD_FB_PUSH 1
#define V4L2CTD_FB_XFER_MODE V4L2CTD_FB_PUSH

/*#define DEBUG_NO_VCRTCM_KERNEL */

extern struct list_head v4l2ctd_info_list;
extern int v4l2ctd_major;
extern int v4l2ctd_num_minors;
extern int v4l2ctd_fake_vblank_slack;
extern struct vcrtcm_funcs v4l2ctd_vcrtcm_funcs;



struct v4l2ctd_fmt {
	char  *name;
	uint32_t  fourcc;
	int   depth;
};

struct v4l2ctd_info {
	/* vcrtcm stuff */
	struct list_head list;
	int minor;
	struct v4l2ctd_vcrtcm_hal_descriptor *v4l2ctd_vcrtcm_hal_descriptor;
	struct mutex xmit_mutex;
	int xfer_in_progress;
	int enabled_queue;
	int status;
	wait_queue_head_t xmit_sync_queue;

	struct workqueue_struct *workqueue;

	struct delayed_work fake_vblank_work;

	char *main_buffer;
	char *cursor;
	int fb_len;
	int cursor_len;

	/* v4l2ctd */
	uint8_t *shadowbuf;
	uint32_t shadowbufsize;
	unsigned long jshadowbuf;
	struct video_device *vfd;
	struct v4l2_device v4l2_dev;
	struct mutex mlock;
	spinlock_t slock;
	struct videobuf_queue vb_vidq;
	struct list_head active;
	unsigned long generating;
	struct task_struct *kthread;
	struct v4l2ctd_fmt *fmt;


	/* debug stuff */
	int page_track;
	int kmalloc_track;
	int vmalloc_track;
};

struct v4l2ctd_vcrtcm_hal_descriptor {
	struct list_head list;
	int fb_xmit_counter;
	int fb_force_xmit;
	u32 pending_pflip_ioaddr;
	unsigned long fb_xmit_period_jiffies;
	unsigned long next_fb_xmit_jiffies;
	unsigned long last_xmit_jiffies;
	unsigned long next_vblank_jiffies;
	struct vcrtcm_dev_hal *vcrtcm_dev_hal;
	struct vcrtcm_fb vcrtcm_fb;
	struct vcrtcm_cursor vcrtcm_cursor;
	struct vcrtcm_push_buffer_descriptor pbd_fb[2];
	struct vcrtcm_push_buffer_descriptor pbd_cursor[2];
	void *pb_fb[2];
	void *pb_cursor[2];
	int pb_needs_xmit[2];
	int push_buffer_index;

	int dpms_state;

	void *hw_fb_ptr;
	void *hw_fb_prev_ptr;
	u32 ioaddr_prev;

	struct v4l2ctd_info *v4l2ctd_info;
};

#endif
