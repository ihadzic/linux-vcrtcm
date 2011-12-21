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

#define V4L2CTD_MAX_MINOR 255

#define V4L2CTD_FPS_HARD_LIMIT 100
#define V4L2CTD_XMIT_HARD_DEADLINE (HZ/10)

#define V4L2CTD_IN_DO_XMIT 0x1

#define V4L2CTD_ALLOC_PB_FLAG_FB 0x0
#define V4L2CTD_ALLOC_PB_FLAG_CURSOR 0x1
#define V4L2CTD_ALLOC_PB_STRING(x) ((x) ? "cursor" : "framebuffer")

extern int debug;

extern struct list_head v4l2ctd_info_list;
extern int v4l2ctd_major;
extern int v4l2ctd_num_minors;
extern int v4l2ctd_fake_vblank_slack;

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
	struct mutex buffer_mutex;
	spinlock_t v4l2ctd_lock;
	int enabled_queue;
	unsigned long status;
	wait_queue_head_t xmit_sync_queue;

	struct workqueue_struct *workqueue;

	struct delayed_work fake_vblank_work;

	char *main_buffer;
	char *cursor;

	/* v4l2ctd */
	uint8_t *shadowbuf;
	uint32_t shadowbufsize;
	struct mutex sb_lock;
	struct page **shadowbuf_pages;
	unsigned int shadowbuf_num_pages;
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
	unsigned long fb_xmit_period_jiffies;
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

	struct v4l2ctd_info *v4l2ctd_info;
};

int v4l2ctd_alloc_shadowbuf(struct v4l2ctd_info *v4l2ctd_info,
				unsigned long size);
void v4l2ctd_free_shadowbuf(struct v4l2ctd_info *v4l2ctd_info);

#endif
