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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/delay.h>
#include <linux/prefetch.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <linux/videodev2.h>
#include <media/videobuf-vmalloc.h>

#include "v4l2pcon.h"
#include "v4l2pcon_vcrtcm.h"
#include "v4l2pcon_utils.h"
#include "vcrtcm/vcrtcm_pcon.h"


#define V4L2PCON_MAJOR_VERSION 0
#define V4L2PCON_MINOR_VERSION 2
#define V4L2PCON_RELEASE 0
#define V4L2PCON_VERSION \
	KERNEL_VERSION(V4L2PCON_MAJOR_VERSION, V4L2PCON_MINOR_VERSION, V4L2PCON_RELEASE)

struct list_head v4l2pcon_info_list;
int v4l2pcon_major = -1;
int v4l2pcon_num_minors = 1;
int v4l2pcon_fake_vblank_slack = 1;
static unsigned int vid_limit = 16;
int debug; /* Enable the printing of debugging information */

/* NOTE: applications will set their preferred format.  That does not mean it
 *	 is our preferred format.  We would like applications to use bgr32 but
 *	 they will likely pick another format and we have to convert for them,
 *	 which takes additional cpu resources.  At high resolutions is gets bad.
 */
static struct v4l2pcon_fmt formats[] = {
	{
		.name		= "BGR-8-8-8-8",
		.fourcc		= V4L2_PIX_FMT_BGR32,
		.depth		= 32,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	},
/*
	{
		.name		= "RGB-8-8-8-8",
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= 32,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	},
	{
		.name		= "BGR-8-8-8",
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.depth		= 24,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	},
	{
		.name		= "RGB-8-8-8",
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.depth		= 24,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	},
	{
		.name		= "RGB-5-6-5",
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= 16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	},
	{
		.name		= "RGB-5-5-5",
		.fourcc		= V4L2_PIX_FMT_RGB555,
		.depth		= 16,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	},
*/
};

static struct v4l2pcon_fmt *get_format(struct v4l2_format *f)
{
	struct v4l2pcon_fmt *fmt;
	uint32_t i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		fmt = &formats[i];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (i == ARRAY_SIZE(formats))
		return NULL;

	return &formats[i];
}

/************************************************************************/
/* stream thread                                                        */
/************************************************************************/

static void
v4l2pcon_fillbuff(struct v4l2pcon_info *v4l2pcon_info, struct videobuf_buffer *vb)
{
	struct v4l2pcon_flow_info *vhd;
	struct timeval ts;
	uint8_t *fb, *fbend, *dst;
	uint32_t fbsize;
	uint32_t w, h, d;

	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return;
	dst = videobuf_to_vmalloc(vb);
	if (!dst)
		return;
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return;

	w  = vhd->vcrtcm_fb.hdisplay;
	h = vhd->vcrtcm_fb.vdisplay;
	d = v4l2pcon_info->fmt->depth;

	mutex_lock(&v4l2pcon_info->sb_lock);
	switch (v4l2pcon_info->fmt->fourcc) {
	case V4L2_PIX_FMT_BGR32:
		/* native format so just copy */
		memcpy(dst, fb, fbsize);
		vb->size = fbsize;
		break;
	case V4L2_PIX_FMT_RGB32:
		/* reorder the bytes */
		fbend = fb + fbsize;
		while (fb < fbend) {
			dst[0] = fb[2];
			dst[1] = fb[1];
			dst[2] = fb[0];
			dst[3] = fb[3];
			fb += 4;
			dst += 4;
		}
		break;
	case V4L2_PIX_FMT_BGR24:
		/* get rid of alpha */
		fbend = fb + fbsize;
		while (fb < fbend) {
			dst[0] = fb[0];
			dst[1] = fb[1];
			dst[2] = fb[2];
			fb += 4;
			dst += 3;
		}
		break;
	case V4L2_PIX_FMT_RGB24:
		/* reorder the bytes, get rid of alpha */
		fbend = fb + fbsize;
		while (fb < fbend) {
			dst[0] = fb[2];
			dst[1] = fb[1];
			dst[2] = fb[0];
			fb += 4;
			dst += 3;
		}
		break;
	case V4L2_PIX_FMT_RGB565:
		fbend = fb + fbsize;
		while (fb < fbend) {
			dst[0] = ((fb[1] & 0x1C) << 3) | ((fb[0] >> 3) & 0xFF);
			dst[1] = (fb[2] & 0xF8) | ((fb[1] >> 5) & 0xFF);
			fb += 4;
			dst += 2;
		}
		break;
	case V4L2_PIX_FMT_RGB555:
		fbend = fb + fbsize;
		while (fb < fbend) {
			dst[0] = ((fb[1] & 0xF8) << 2) | ((fb[0] >> 3) & 0xFF);
			dst[1] = ((fb[2] & 0xF8) >> 1) | ((fb[1] & 0xF8) >> 6);
			fb += 4;
			dst += 2;
		}
		break;
	default:
		d = 0;
		break;
	}
	mutex_unlock(&v4l2pcon_info->sb_lock);

	vb->size = w * h * (d >> 3);
	vb->field_count++;
	do_gettimeofday(&ts);
	vb->ts = ts;
	vb->state = VIDEOBUF_DONE;
}

static void v4l2pcon_thread_tick(struct v4l2pcon_info *v4l2pcon_info)
{
	struct videobuf_buffer *vb;
	unsigned long flags = 0;
	uint8_t *fb;
	uint32_t fbsize;

	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return;

	spin_lock_irqsave(&v4l2pcon_info->slock, flags);
	if (list_empty(&v4l2pcon_info->active))
		goto unlock;

	vb = list_entry(v4l2pcon_info->active.next,
			struct videobuf_buffer, queue);
	if (!waitqueue_active(&vb->done))
		goto unlock;
	list_del(&vb->queue);
	v4l2pcon_fillbuff(v4l2pcon_info, vb);
	wake_up(&vb->done);
unlock:
	spin_unlock_irqrestore(&v4l2pcon_info->slock, flags);
}

static int v4l2pcon_thread(void *data)
{
	struct v4l2pcon_info *v4l2pcon_info;
	unsigned long sleep_time;

	v4l2pcon_info = data;
	sleep_time = msecs_to_jiffies(0);
	/* set_freezable(); */
	while (!kthread_should_stop()) {
		v4l2pcon_thread_tick(v4l2pcon_info);
		if (kthread_should_stop())
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(sleep_time);
	}
	return 0;
}

static void v4l2pcon_start_generating(struct file *file)
{
	struct v4l2pcon_info *v4l2pcon_info;
	v4l2pcon_info = video_drvdata(file);

	if (test_and_set_bit(0, &v4l2pcon_info->generating))
		return;
	file->private_data = v4l2pcon_info;

	v4l2pcon_info->kthread = kthread_run(v4l2pcon_thread, v4l2pcon_info,
					v4l2pcon_info->v4l2_dev.name);
	if (IS_ERR(v4l2pcon_info->kthread)) {
		clear_bit(0, &v4l2pcon_info->generating);
		return;
	}
}

static void v4l2pcon_stop_generating(struct file *file)
{
	struct v4l2pcon_info *v4l2pcon_info;
	v4l2pcon_info = video_drvdata(file);

	if (!file->private_data)
		return;
	if (!v4l2pcon_info)
		return;
	if (!test_and_clear_bit(0, &v4l2pcon_info->generating))
		return;

	if (v4l2pcon_info->kthread) {
		kthread_stop(v4l2pcon_info->kthread);
		v4l2pcon_info->kthread = NULL;
	}
	videobuf_stop(&v4l2pcon_info->vb_vidq);
	videobuf_mmap_free(&v4l2pcon_info->vb_vidq);
}

static int v4l2pcon_is_generating(struct v4l2pcon_info *v4l2pcon_info)
{
	return test_bit(0, &v4l2pcon_info->generating);
}

/************************************************************************/
/* videobuf                                                             */
/************************************************************************/

static int
buf_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pcon_info = vq->priv_data;
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;
	*size = fbsize;
	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;
	return 0;
}

static void free_buf(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	videobuf_vmalloc_free(vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int
buf_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	uint8_t *fb;
	uint32_t fbsize;
	int ret;

	v4l2pcon_info = vq->priv_data;
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;
	vb->size = fbsize;
	if (0 != vb->baddr && vb->bsize < vb->size)
		return -EINVAL;

	vb->width  = vhd->vcrtcm_fb.hdisplay;
	vb->height = vhd->vcrtcm_fb.vdisplay;
	vb->field  = field;

	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret < 0)
			goto fail;
	}
	vb->state = VIDEOBUF_PREPARED;
	return 0;

fail:
	free_buf(vq, vb);
	return ret;
}

static void
buf_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct v4l2pcon_info *v4l2pcon_info;
	v4l2pcon_info = vq->priv_data;
	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &v4l2pcon_info->active);
}

static void buf_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	free_buf(vq, vb);
}

/************************************************************************/
/* ioctl                                                                */
/************************************************************************/

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct v4l2pcon_info *v4l2pcon_info;

	v4l2pcon_info = video_drvdata(file);
	strcpy(cap->driver, "v4l2pcon");
	strcpy(cap->card, "v4l2pcon");
	strlcpy(cap->bus_info, v4l2pcon_info->v4l2_dev.name,
		sizeof(cap->bus_info));
	cap->version = V4L2PCON_VERSION;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	struct v4l2pcon_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;
	fmt = &formats[f->index];
	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	struct v4l2pcon_fmt *fmt;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;
	fmt = v4l2pcon_info->fmt;
	if (!fmt)
		return -EINVAL;

	f->fmt.pix.width        = vhd->vcrtcm_fb.hdisplay;
	f->fmt.pix.height       = vhd->vcrtcm_fb.vdisplay;
	f->fmt.pix.field        = V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat  = fmt->fourcc;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * (fmt->depth >> 3));
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = fmt->colorspace;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	struct v4l2pcon_fmt *fmt;
	uint8_t *fb;
	uint32_t fbsize;
	enum v4l2_field field;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;
	fmt = get_format(f);
	if (!fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != field)
		return -EINVAL;

	field = V4L2_FIELD_NONE;

	f->fmt.pix.field        = field;
	f->fmt.pix.width        = vhd->vcrtcm_fb.hdisplay;
	f->fmt.pix.height       = vhd->vcrtcm_fb.vdisplay;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * (fmt->depth >> 3));
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = fmt->colorspace;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	struct v4l2pcon_fmt *fmt;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	if (v4l2pcon_is_generating(v4l2pcon_info))
		return -EBUSY;

	fmt = get_format(f);
	if (!fmt)
		return -EINVAL;
	v4l2pcon_info->fmt = fmt;
	f->fmt.pix.width        = vhd->vcrtcm_fb.hdisplay;
	f->fmt.pix.height       = vhd->vcrtcm_fb.vdisplay;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * (fmt->depth >> 3));
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = fmt->colorspace;
	v4l2pcon_info->vb_vidq.field = f->fmt.pix.field;

	return 0;
}

static ssize_t
v4l2pcon_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	v4l2pcon_start_generating(file);
	return videobuf_read_stream(&v4l2pcon_info->vb_vidq,
				    data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static unsigned int
v4l2pcon_poll(struct file *file, struct poll_table_struct *wait)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	v4l2pcon_start_generating(file);
	return videobuf_poll_stream(file, &v4l2pcon_info->vb_vidq, wait);
}

static int v4l2pcon_close(struct file *file)
{
	v4l2pcon_stop_generating(file);
	return 0;
}

static int v4l2pcon_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	uint8_t *fb;
	uint32_t fbsize;
	int ret;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fb = v4l2pcon_info->shadowbuf;
	fbsize = v4l2pcon_info->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	ret = videobuf_mmap_mapper(&v4l2pcon_info->vb_vidq, vma);
	return ret;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_UNKNOWN;
	sprintf(inp->name, "v4l2pcon %u", inp->index);
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;
	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct v4l2pcon_info *v4l2pcon_info;
	v4l2pcon_info = video_drvdata(file);
	return videobuf_reqbufs(&v4l2pcon_info->vb_vidq, p);
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pcon_info *v4l2pcon_info;
	v4l2pcon_info = video_drvdata(file);
	return videobuf_querybuf(&v4l2pcon_info->vb_vidq, p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pcon_info *v4l2pcon_info;
	v4l2pcon_info = video_drvdata(file);
	return videobuf_qbuf(&v4l2pcon_info->vb_vidq, p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pcon_info *v4l2pcon_info;
	v4l2pcon_info = video_drvdata(file);
	return videobuf_dqbuf(&v4l2pcon_info->vb_vidq, p,
				file->f_flags & O_NONBLOCK);
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct v4l2pcon_info *v4l2pcon_info;
	int ret;

	v4l2pcon_info = video_drvdata(file);
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = videobuf_streamon(&v4l2pcon_info->vb_vidq);
	if (ret)
		return ret;
	v4l2pcon_start_generating(file);
	return 0;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct v4l2pcon_info *v4l2pcon_info;
	int ret;

	v4l2pcon_info = video_drvdata(file);
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = videobuf_streamoff(&v4l2pcon_info->vb_vidq);
	if (!ret)
		v4l2pcon_stop_generating(file);
	return ret;
}

static int
vidioc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *parm)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	struct v4l2_fract timeperframe;
	int fps;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;
	fps = HZ / vhd->fb_xmit_period_jiffies;

	if (parm->type !=  V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	timeperframe.numerator = 1001;
	timeperframe.denominator = fps * 1000;

	parm->parm.capture.capability |=  V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm->parm.capture.timeperframe = timeperframe;
	return 0;
}

static int
vidioc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *parm)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;

	if (parm->type !=  V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* ffplay will freak out if this doesn't return 0 */
	return 0;
	/* return -EINVAL; */
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
					struct v4l2_frmsizeenum *fsize)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	uint32_t i;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;

	if (fsize->index != 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (fsize->pixel_format == formats[i].fourcc) {
			fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			fsize->discrete.width = vhd->vcrtcm_fb.hdisplay;
			fsize->discrete.height = vhd->vcrtcm_fb.vdisplay;
			return 0;
		}

	return -EINVAL;
}

static int vidioc_enum_frameintervals(struct file *file, void *fh,
					struct v4l2_frmivalenum *fival)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct v4l2pcon_flow_info *vhd;
	uint32_t i;
	int fps;

	v4l2pcon_info = video_drvdata(file);
	vhd = v4l2pcon_info->v4l2pcon_flow_info;
	if (!vhd)
		return -EINVAL;

	if (fival->index != 0)
		return -EINVAL;
	if (fival->width != vhd->vcrtcm_fb.hdisplay &&
			fival->height != vhd->vcrtcm_fb.hdisplay)
		return -EINVAL;

	fps = HZ / vhd->fb_xmit_period_jiffies;
	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (fival->pixel_format == formats[i].fourcc) {
			fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
			fival->discrete.numerator = 1001;
			fival->discrete.denominator = fps * 1000;
			return 0;
		}
	return -EINVAL;
}

/************************************************************************/
/* shadowbuf alloc/free                                                 */
/************************************************************************/

int v4l2pcon_alloc_shadowbuf(struct v4l2pcon_info *v4l2pcon_info,
				unsigned long size)
{
	struct page **pages;
	unsigned int num_pages;
	uint8_t *shadowbuf;
	int result;

	if (!v4l2pcon_info)
		return -EINVAL;

	v4l2pcon_free_shadowbuf(v4l2pcon_info);

	num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE > 0)
		num_pages++;

	pages = v4l2pcon_kmalloc(v4l2pcon_info,
				sizeof(struct page *) * num_pages,
				GFP_KERNEL);
	if (!pages)
		goto sb_alloc_err;
	result = v4l2pcon_alloc_multiple_pages(v4l2pcon_info, GFP_KERNEL,
						pages, num_pages);
	if (result != 0)
		goto sb_alloc_mpages_err;
	shadowbuf = vm_map_ram(pages, num_pages, 0, PAGE_KERNEL);
	if (!shadowbuf)
		goto sb_alloc_map_err;
	memset(shadowbuf, 0, size);

	v4l2pcon_info->shadowbuf = shadowbuf;
	v4l2pcon_info->shadowbufsize = size;
	v4l2pcon_info->shadowbuf_pages = pages;
	v4l2pcon_info->shadowbuf_num_pages = num_pages;

	return 0;

sb_alloc_map_err:
	v4l2pcon_free_multiple_pages(v4l2pcon_info, pages, num_pages);
sb_alloc_mpages_err:
	if (pages)
		v4l2pcon_kfree(v4l2pcon_info, pages);
sb_alloc_err:

	return -ENOMEM;
}

void v4l2pcon_free_shadowbuf(struct v4l2pcon_info *v4l2pcon_info)
{
	if (!v4l2pcon_info)
		return;
	if (!v4l2pcon_info->shadowbuf)
		return;

	vm_unmap_ram(v4l2pcon_info->shadowbuf,
			v4l2pcon_info->shadowbuf_num_pages);
	v4l2pcon_free_multiple_pages(v4l2pcon_info,
					v4l2pcon_info->shadowbuf_pages,
					v4l2pcon_info->shadowbuf_num_pages);
	v4l2pcon_kfree(v4l2pcon_info, v4l2pcon_info->shadowbuf_pages);

	v4l2pcon_info->shadowbuf = NULL;
	v4l2pcon_info->shadowbufsize = 0;
	v4l2pcon_info->shadowbuf_pages = NULL;
	v4l2pcon_info->shadowbuf_num_pages = 0;

	return;
}

/************************************************************************/
/* funcs                                                                */
/************************************************************************/

static const struct v4l2_file_operations v4l2pcon_fops = {
	.owner		= THIS_MODULE,
	.release        = v4l2pcon_close,
	.read           = v4l2pcon_read,
	.poll		= v4l2pcon_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = v4l2pcon_mmap,
};

static struct videobuf_queue_ops v4l2pcon_video_qops = {
	.buf_setup      = buf_setup,
	.buf_prepare    = buf_prepare,
	.buf_queue      = buf_queue,
	.buf_release    = buf_release,
};

static const struct v4l2_ioctl_ops v4l2pcon_ioctl_ops = {
	.vidioc_querycap		= vidioc_querycap,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vidioc_s_fmt_vid_cap,
	.vidioc_enum_input		= vidioc_enum_input,
	.vidioc_g_input			= vidioc_g_input,
	.vidioc_s_input			= vidioc_s_input,
	.vidioc_reqbufs			= vidioc_reqbufs,
	.vidioc_querybuf		= vidioc_querybuf,
	.vidioc_qbuf			= vidioc_qbuf,
	.vidioc_dqbuf			= vidioc_dqbuf,
	.vidioc_streamon		= vidioc_streamon,
	.vidioc_streamoff		= vidioc_streamoff,
	.vidioc_g_parm			= vidioc_g_parm,
	.vidioc_s_parm			= vidioc_s_parm,
	.vidioc_enum_framesizes		= vidioc_enum_framesizes,
	.vidioc_enum_frameintervals	= vidioc_enum_frameintervals,
};

static struct video_device v4l2pcon_template = {
	.name		= "v4l2pcon",
	.fops           = &v4l2pcon_fops,
	.ioctl_ops	= &v4l2pcon_ioctl_ops,
	.release	= video_device_release,
	.tvnorms        = V4L2_STD_UNKNOWN,
	.current_norm   = V4L2_STD_UNKNOWN,
};

static struct vcrtcm_pcon_funcs v4l2pcon_vcrtcm_pcon_funcs = {
	.attach = v4l2pcon_attach,
	.detach = v4l2pcon_detach,
	.set_fb = v4l2pcon_set_fb,
	.get_fb = v4l2pcon_get_fb,
	.dirty_fb = v4l2pcon_dirty_fb,
	.wait_fb = v4l2pcon_wait_fb,
	.get_fb_status = v4l2pcon_get_fb_status,
	.set_fps = v4l2pcon_set_fps,
	.get_fps = v4l2pcon_get_fps,
	.set_cursor = v4l2pcon_set_cursor,
	.get_cursor = v4l2pcon_get_cursor,
	.set_dpms = v4l2pcon_set_dpms,
	.get_dpms = v4l2pcon_get_dpms,
	.disable = v4l2pcon_disable
};

static struct vcrtcm_pcon_props v4l2pcon_vcrtcm_pcon_props = {
	.xfer_mode = VCRTCM_PUSH_PULL
};

static struct v4l2pcon_info *v4l2pcon_create_minor(int minor)
{
	struct v4l2pcon_info *v4l2pcon_info;
	struct video_device *vfd;
	unsigned long flags;
	int ret;

	v4l2pcon_info = NULL;
	vfd = NULL;

	v4l2pcon_info = kzalloc(sizeof(struct v4l2pcon_info), GFP_KERNEL);
	if (!v4l2pcon_info) {
		PR_ERR("failed alloc of v4l2pcon_info\n");
		return NULL;
	}

	mutex_init(&v4l2pcon_info->mlock);
	spin_lock_init(&v4l2pcon_info->slock);

	v4l2pcon_info->shadowbuf = NULL;
	v4l2pcon_info->shadowbufsize = 0;
	mutex_init(&v4l2pcon_info->sb_lock);
	v4l2pcon_info->shadowbuf_pages = NULL;
	v4l2pcon_info->shadowbuf_num_pages = 0;

	snprintf(v4l2pcon_info->v4l2_dev.name,
			sizeof(v4l2pcon_info->v4l2_dev.name),
			"v4l2pcon-%03d", minor);
	ret = v4l2_device_register(NULL, &v4l2pcon_info->v4l2_dev);
	if (ret)
		goto free_info;
	videobuf_queue_vmalloc_init(&v4l2pcon_info->vb_vidq, &v4l2pcon_video_qops,
					NULL, &v4l2pcon_info->slock,
					V4L2_BUF_TYPE_VIDEO_CAPTURE,
					V4L2_FIELD_NONE,
					sizeof(struct videobuf_buffer),
					v4l2pcon_info, &v4l2pcon_info->mlock);
	INIT_LIST_HEAD(&v4l2pcon_info->active);
	vfd = video_device_alloc();
	if (!vfd)
		goto unreg_dev;
	*vfd = v4l2pcon_template;
	vfd->lock = &v4l2pcon_info->mlock;
	vfd->v4l2_dev = &v4l2pcon_info->v4l2_dev;
	vfd->minor = minor;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		goto rel_dev;
	video_set_drvdata(vfd, v4l2pcon_info);
	v4l2pcon_info->vfd = vfd;
	v4l2pcon_info->fmt = &formats[0];

	mutex_init(&v4l2pcon_info->buffer_mutex);
	spin_lock_init(&v4l2pcon_info->v4l2pcon_lock);

	v4l2pcon_info->minor = minor;
	INIT_LIST_HEAD(&v4l2pcon_info->list);

	init_waitqueue_head(&v4l2pcon_info->xmit_sync_queue);
	v4l2pcon_info->enabled_queue = 1;

	v4l2pcon_info->workqueue = create_workqueue("v4l2pcon_workers");

	v4l2pcon_info->v4l2pcon_flow_info = NULL;

	INIT_DELAYED_WORK(&v4l2pcon_info->fake_vblank_work, v4l2pcon_fake_vblank);

	spin_lock_irqsave(&v4l2pcon_info->v4l2pcon_lock, flags);
	v4l2pcon_info->status = 0;
	spin_unlock_irqrestore(&v4l2pcon_info->v4l2pcon_lock, flags);

	return v4l2pcon_info;
rel_dev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&v4l2pcon_info->v4l2_dev);
free_info:
	kfree(v4l2pcon_info);
	return NULL;

}

static int __init v4l2pcon_init(void)
{
	struct v4l2pcon_info *v4l2pcon_info;
	dev_t dev;
	int minor, num_created;
	int r;

	PR_INFO("v4l2 PCON, (C) Bell Labs, Alcatel-Lucent, Inc.\n");

	INIT_LIST_HEAD(&v4l2pcon_info_list);

	if (v4l2pcon_num_minors < 0)
		v4l2pcon_num_minors = 0;
	if (v4l2pcon_num_minors > V4L2PCON_MAX_MINOR)
		v4l2pcon_num_minors = V4L2PCON_MAX_MINOR;

	num_created = 0;
	for (minor = 0; minor < v4l2pcon_num_minors; minor++) {
		v4l2pcon_info = v4l2pcon_create_minor(minor);
		if (!v4l2pcon_info)
			break;
		list_add(&v4l2pcon_info->list, &v4l2pcon_info_list);
		num_created++;
	}

	if (list_empty(&v4l2pcon_info_list)) {
		v4l2pcon_major = -1;
		v4l2pcon_num_minors = 0;
		return 0;
	}

	v4l2pcon_num_minors = num_created;
	PR_INFO("Allocating/registering dynamic major number");
	r = alloc_chrdev_region(&dev, 0, v4l2pcon_num_minors, "v4l2pcon");
	v4l2pcon_major = MAJOR(dev);
	if (r) {
		PR_ERR("Can't get major device number, driver unusable\n");
		v4l2pcon_major = -1;
		v4l2pcon_num_minors = 0;
		return 0;
	}
	PR_INFO("Using major device number %d\n", v4l2pcon_major);

	list_for_each_entry(v4l2pcon_info, &v4l2pcon_info_list, list) {
		PR_DEBUG("Calling vcrtcm_pcon_add for v4l2pcon %p major %d minor %d\n",
				v4l2pcon_info, v4l2pcon_major, v4l2pcon_info->minor);
		if (vcrtcm_pcon_add(&v4l2pcon_vcrtcm_pcon_funcs, &v4l2pcon_vcrtcm_pcon_props,
				  v4l2pcon_major, v4l2pcon_info->minor, 0, v4l2pcon_info))
			PR_WARN("vcrtcm_pcon_add failed, v4l2pcon major %d, minor %d, "
				"won't work\n", v4l2pcon_major, v4l2pcon_info->minor);
		PR_INFO("successfully registered minor %d\n", v4l2pcon_info->minor);
	}

	PR_INFO("v4l2 PCON Loaded\n");

	return 0;
}

static void __exit v4l2pcon_exit(void)
{
	struct v4l2pcon_info *v4l2pcon_info, *tmp;
	PR_INFO("Cleaning up v4l2pcon\n");
	list_for_each_entry_safe(v4l2pcon_info, tmp, &v4l2pcon_info_list, list) {
		if (v4l2pcon_major >= -1) {
			video_unregister_device(v4l2pcon_info->vfd);
			v4l2_device_unregister(&v4l2pcon_info->v4l2_dev);

			/* unregister with VCRTCM */
			PR_DEBUG("Calling vcrtcm_pcon_del for "
				"v4l2pcon %p, major %d, minor %d\n",
				v4l2pcon_info, v4l2pcon_major,
				v4l2pcon_info->minor);
			cancel_delayed_work_sync(&v4l2pcon_info->fake_vblank_work);
			vcrtcm_pcon_del(v4l2pcon_major, v4l2pcon_info->minor, 0);

			mutex_lock(&v4l2pcon_info->sb_lock);
			if (v4l2pcon_info->shadowbuf)
				v4l2pcon_free_shadowbuf(v4l2pcon_info);
			mutex_unlock(&v4l2pcon_info->sb_lock);

			unregister_chrdev_region(MKDEV(v4l2pcon_major, 0),
							v4l2pcon_num_minors);

			PR_DEBUG("freeing main buffer: %p, cursor %p\n",
					v4l2pcon_info->main_buffer,
					v4l2pcon_info->cursor);
			PR_DEBUG("freeing v4l2pcon_info data %p\n",
				v4l2pcon_info);
			PR_DEBUG("page_track : %d\n",
				v4l2pcon_info->page_track);
			PR_DEBUG("kmalloc_track: %d\n",
				v4l2pcon_info->kmalloc_track);
			PR_DEBUG("vmalloc_track: %d\n",
				v4l2pcon_info->vmalloc_track);

			list_del(&v4l2pcon_info->list);
			kfree(v4l2pcon_info);
			v4l2pcon_num_minors--;
		}
	}

	return;
}

module_init(v4l2pcon_init);
module_exit(v4l2pcon_exit);

MODULE_PARM_DESC(debug, "Enable debugging information.");
module_param(debug, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(num_minors, "Number of minors (default=1)");
module_param_named(num_minors, v4l2pcon_num_minors, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("v4l2 PCON");
MODULE_AUTHOR("Hans Christian Woithe (hans.woithe@alcatel-lucent.com)");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
