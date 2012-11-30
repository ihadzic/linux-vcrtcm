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
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_utils.h>

#include "v4l2pim.h"
#include "v4l2pim_vcrtcm.h"

#define V4L2PIM_MAJOR_VERSION 0
#define V4L2PIM_MINOR_VERSION 2
#define V4L2PIM_RELEASE 0
#define V4L2PIM_VERSION \
	KERNEL_VERSION(V4L2PIM_MAJOR_VERSION, V4L2PIM_MINOR_VERSION, V4L2PIM_RELEASE)

struct list_head v4l2pim_minor_list;
int v4l2pim_major = -1;
int v4l2pim_num_minors;
int v4l2pim_fake_vblank_slack = 1;
static unsigned int vid_limit = 16;
int v4l2pim_debug; /* Enable the printing of debugging information */

/* ID generator for allocating minor numbers */
static struct vcrtcm_id_generator v4l2pim_minor_id_generator;

/* NOTE: applications will set their preferred format.  That does not mean it
 *	 is our preferred format.  We would like applications to use bgr32 but
 *	 they will likely pick another format and we have to convert for them,
 *	 which takes additional cpu resources.  At high resolutions is gets bad.
 */
static struct v4l2pim_fmt formats[] = {
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

static struct v4l2pim_fmt *get_format(struct v4l2_format *f)
{
	struct v4l2pim_fmt *fmt;
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
v4l2pim_fillbuff(struct v4l2pim_minor *v4l2pim_minor, struct videobuf_buffer *vb)
{
	struct v4l2pim_pcon *pcon;
	struct timeval ts;
	uint8_t *fb, *fbend, *dst;
	uint32_t fbsize;
	uint32_t w, h, d;

	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return;
	dst = videobuf_to_vmalloc(vb);
	if (!dst)
		return;
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return;

	w  = pcon->vcrtcm_fb.hdisplay;
	h = pcon->vcrtcm_fb.vdisplay;
	d = v4l2pim_minor->fmt->depth;

	mutex_lock(&v4l2pim_minor->sb_lock);
	switch (v4l2pim_minor->fmt->fourcc) {
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
	mutex_unlock(&v4l2pim_minor->sb_lock);

	vb->size = w * h * (d >> 3);
	vb->field_count++;
	do_gettimeofday(&ts);
	vb->ts = ts;
	vb->state = VIDEOBUF_DONE;
}

static void v4l2pim_thread_tick(struct v4l2pim_minor *v4l2pim_minor)
{
	struct videobuf_buffer *vb;
	unsigned long flags = 0;
	uint8_t *fb;
	uint32_t fbsize;

	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return;

	spin_lock_irqsave(&v4l2pim_minor->slock, flags);
	if (list_empty(&v4l2pim_minor->active))
		goto unlock;

	vb = list_entry(v4l2pim_minor->active.next,
			struct videobuf_buffer, queue);
	if (!waitqueue_active(&vb->done))
		goto unlock;
	list_del(&vb->queue);
	v4l2pim_fillbuff(v4l2pim_minor, vb);
	wake_up(&vb->done);
unlock:
	spin_unlock_irqrestore(&v4l2pim_minor->slock, flags);
}

static int v4l2pim_thread(void *data)
{
	struct v4l2pim_minor *v4l2pim_minor;
	unsigned long sleep_time;

	v4l2pim_minor = data;
	sleep_time = msecs_to_jiffies(0);
	/* set_freezable(); */
	while (!kthread_should_stop()) {
		v4l2pim_thread_tick(v4l2pim_minor);
		if (kthread_should_stop())
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(sleep_time);
	}
	return 0;
}

static void v4l2pim_start_generating(struct file *file)
{
	struct v4l2pim_minor *v4l2pim_minor;
	v4l2pim_minor = video_drvdata(file);

	if (test_and_set_bit(0, &v4l2pim_minor->generating))
		return;
	file->private_data = v4l2pim_minor;

	v4l2pim_minor->kthread = kthread_run(v4l2pim_thread, v4l2pim_minor,
					v4l2pim_minor->v4l2_dev.name);
	if (IS_ERR(v4l2pim_minor->kthread)) {
		clear_bit(0, &v4l2pim_minor->generating);
		return;
	}
}

static void v4l2pim_stop_generating(struct file *file)
{
	struct v4l2pim_minor *v4l2pim_minor;
	v4l2pim_minor = video_drvdata(file);

	if (!file->private_data)
		return;
	if (!v4l2pim_minor)
		return;
	if (!test_and_clear_bit(0, &v4l2pim_minor->generating))
		return;

	if (v4l2pim_minor->kthread) {
		kthread_stop(v4l2pim_minor->kthread);
		v4l2pim_minor->kthread = NULL;
	}
	videobuf_stop(&v4l2pim_minor->vb_vidq);
	videobuf_mmap_free(&v4l2pim_minor->vb_vidq);
}

static int v4l2pim_is_generating(struct v4l2pim_minor *v4l2pim_minor)
{
	return test_bit(0, &v4l2pim_minor->generating);
}

/************************************************************************/
/* videobuf                                                             */
/************************************************************************/

static int
buf_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pim_minor = vq->priv_data;
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
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
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	uint8_t *fb;
	uint32_t fbsize;
	int ret;

	v4l2pim_minor = vq->priv_data;
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;
	vb->size = fbsize;
	if (0 != vb->baddr && vb->bsize < vb->size)
		return -EINVAL;

	vb->width  = pcon->vcrtcm_fb.hdisplay;
	vb->height = pcon->vcrtcm_fb.vdisplay;
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
	struct v4l2pim_minor *v4l2pim_minor;
	v4l2pim_minor = vq->priv_data;
	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &v4l2pim_minor->active);
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
	struct v4l2pim_minor *v4l2pim_minor;

	v4l2pim_minor = video_drvdata(file);
	strcpy(cap->driver, "v4l2pim");
	strcpy(cap->card, "v4l2pim");
	strlcpy(cap->bus_info, v4l2pim_minor->v4l2_dev.name,
		sizeof(cap->bus_info));
	cap->version = V4L2PIM_VERSION;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	struct v4l2pim_fmt *fmt;

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
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	struct v4l2pim_fmt *fmt;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;
	fmt = v4l2pim_minor->fmt;
	if (!fmt)
		return -EINVAL;

	f->fmt.pix.width        = pcon->vcrtcm_fb.hdisplay;
	f->fmt.pix.height       = pcon->vcrtcm_fb.vdisplay;
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
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	struct v4l2pim_fmt *fmt;
	uint8_t *fb;
	uint32_t fbsize;
	enum v4l2_field field;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
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
	f->fmt.pix.width        = pcon->vcrtcm_fb.hdisplay;
	f->fmt.pix.height       = pcon->vcrtcm_fb.vdisplay;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * (fmt->depth >> 3));
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = fmt->colorspace;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	struct v4l2pim_fmt *fmt;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	if (v4l2pim_is_generating(v4l2pim_minor))
		return -EBUSY;

	fmt = get_format(f);
	if (!fmt)
		return -EINVAL;
	v4l2pim_minor->fmt = fmt;
	f->fmt.pix.width        = pcon->vcrtcm_fb.hdisplay;
	f->fmt.pix.height       = pcon->vcrtcm_fb.vdisplay;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * (fmt->depth >> 3));
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = fmt->colorspace;
	v4l2pim_minor->vb_vidq.field = f->fmt.pix.field;

	return 0;
}

static ssize_t
v4l2pim_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	v4l2pim_start_generating(file);
	return videobuf_read_stream(&v4l2pim_minor->vb_vidq,
				    data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static unsigned int
v4l2pim_poll(struct file *file, struct poll_table_struct *wait)
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	uint8_t *fb;
	uint32_t fbsize;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	v4l2pim_start_generating(file);
	return videobuf_poll_stream(file, &v4l2pim_minor->vb_vidq, wait);
}

static int v4l2pim_open(struct file *file)
{
	struct v4l2pim_minor *v4l2pim_minor;

	if (!try_module_get(THIS_MODULE))
		return -EBUSY;

	v4l2pim_minor = video_drvdata(file);
	atomic_inc(&v4l2pim_minor->users);

	return 0;
}

static int v4l2pim_release(struct file *file)
{
	struct v4l2pim_minor *v4l2pim_minor;

	v4l2pim_stop_generating(file);
	v4l2pim_minor = video_drvdata(file);
	atomic_dec(&v4l2pim_minor->users);
	module_put(THIS_MODULE);

	return 0;
}

static int v4l2pim_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	uint8_t *fb;
	uint32_t fbsize;
	int ret;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fb = v4l2pim_minor->shadowbuf;
	fbsize = v4l2pim_minor->shadowbufsize;
	if (!fb || fbsize <= 0)
		return -EINVAL;

	ret = videobuf_mmap_mapper(&v4l2pim_minor->vb_vidq, vma);
	return ret;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_UNKNOWN;
	sprintf(inp->name, "v4l2pim %u", inp->index);
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
	struct v4l2pim_minor *v4l2pim_minor;
	v4l2pim_minor = video_drvdata(file);
	return videobuf_reqbufs(&v4l2pim_minor->vb_vidq, p);
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pim_minor *v4l2pim_minor;
	v4l2pim_minor = video_drvdata(file);
	return videobuf_querybuf(&v4l2pim_minor->vb_vidq, p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pim_minor *v4l2pim_minor;
	v4l2pim_minor = video_drvdata(file);
	return videobuf_qbuf(&v4l2pim_minor->vb_vidq, p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pim_minor *v4l2pim_minor;
	v4l2pim_minor = video_drvdata(file);
	return videobuf_dqbuf(&v4l2pim_minor->vb_vidq, p,
				file->f_flags & O_NONBLOCK);
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct v4l2pim_minor *v4l2pim_minor;
	int ret;

	v4l2pim_minor = video_drvdata(file);
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = videobuf_streamon(&v4l2pim_minor->vb_vidq);
	if (ret)
		return ret;
	v4l2pim_start_generating(file);
	return 0;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct v4l2pim_minor *v4l2pim_minor;
	int ret;

	v4l2pim_minor = video_drvdata(file);
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = videobuf_streamoff(&v4l2pim_minor->vb_vidq);
	if (!ret)
		v4l2pim_stop_generating(file);
	return ret;
}

static int
vidioc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *parm)
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	struct v4l2_fract timeperframe;
	int fps;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;
	fps = HZ / pcon->fb_xmit_period_jiffies;

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
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
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
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	uint32_t i;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;

	if (fsize->index != 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (fsize->pixel_format == formats[i].fourcc) {
			fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			fsize->discrete.width = pcon->vcrtcm_fb.hdisplay;
			fsize->discrete.height = pcon->vcrtcm_fb.vdisplay;
			return 0;
		}

	return -EINVAL;
}

static int vidioc_enum_frameintervals(struct file *file, void *fh,
					struct v4l2_frmivalenum *fival)
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct v4l2pim_pcon *pcon;
	uint32_t i;
	int fps;

	v4l2pim_minor = video_drvdata(file);
	pcon = v4l2pim_minor->pcon;
	if (!pcon)
		return -EINVAL;

	if (fival->index != 0)
		return -EINVAL;
	if (fival->width != pcon->vcrtcm_fb.hdisplay &&
			fival->height != pcon->vcrtcm_fb.hdisplay)
		return -EINVAL;

	fps = HZ / pcon->fb_xmit_period_jiffies;
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

int v4l2pim_alloc_shadowbuf(struct v4l2pim_minor *v4l2pim_minor,
				unsigned long size)
{
	struct page **pages;
	unsigned int num_pages;
	uint8_t *shadowbuf;
	int result;

	if (!v4l2pim_minor)
		return -EINVAL;

	v4l2pim_free_shadowbuf(v4l2pim_minor);

	num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE > 0)
		num_pages++;

	pages = vcrtcm_kmalloc(sizeof(struct page *) * num_pages,
		GFP_KERNEL, &v4l2pim_minor->kmalloc_track);
	if (!pages)
		goto sb_alloc_err;
	result = vcrtcm_alloc_multiple_pages(GFP_KERNEL, pages,
		num_pages, &v4l2pim_minor->page_track);
	if (result != 0)
		goto sb_alloc_mpages_err;
	shadowbuf = vm_map_ram(pages, num_pages, 0, PAGE_KERNEL);
	if (!shadowbuf)
		goto sb_alloc_map_err;
	memset(shadowbuf, 0, size);

	v4l2pim_minor->shadowbuf = shadowbuf;
	v4l2pim_minor->shadowbufsize = size;
	v4l2pim_minor->shadowbuf_pages = pages;
	v4l2pim_minor->shadowbuf_num_pages = num_pages;

	return 0;

sb_alloc_map_err:
	vcrtcm_free_multiple_pages(pages, num_pages, &v4l2pim_minor->page_track);
sb_alloc_mpages_err:
	if (pages)
		vcrtcm_kfree(pages, &v4l2pim_minor->kmalloc_track);
sb_alloc_err:

	return -ENOMEM;
}

void v4l2pim_free_shadowbuf(struct v4l2pim_minor *v4l2pim_minor)
{
	if (!v4l2pim_minor)
		return;
	if (!v4l2pim_minor->shadowbuf)
		return;

	vm_unmap_ram(v4l2pim_minor->shadowbuf,
			v4l2pim_minor->shadowbuf_num_pages);
	vcrtcm_free_multiple_pages(v4l2pim_minor->shadowbuf_pages,
				v4l2pim_minor->shadowbuf_num_pages,
				&v4l2pim_minor->page_track);
	vcrtcm_kfree(v4l2pim_minor->shadowbuf_pages,
			&v4l2pim_minor->kmalloc_track);

	v4l2pim_minor->shadowbuf = NULL;
	v4l2pim_minor->shadowbufsize = 0;
	v4l2pim_minor->shadowbuf_pages = NULL;
	v4l2pim_minor->shadowbuf_num_pages = 0;

	return;
}

/************************************************************************/
/* funcs                                                                */
/************************************************************************/

static const struct v4l2_file_operations v4l2pim_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2pim_open,
	.release        = v4l2pim_release,
	.read           = v4l2pim_read,
	.poll		= v4l2pim_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = v4l2pim_mmap,
};

static struct videobuf_queue_ops v4l2pim_video_qops = {
	.buf_setup      = buf_setup,
	.buf_prepare    = buf_prepare,
	.buf_queue      = buf_queue,
	.buf_release    = buf_release,
};

static const struct v4l2_ioctl_ops v4l2pim_ioctl_ops = {
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

static struct video_device v4l2pim_template = {
	.name		= "v4l2pim",
	.fops           = &v4l2pim_fops,
	.ioctl_ops	= &v4l2pim_ioctl_ops,
	.release	= video_device_release,
	.tvnorms        = V4L2_STD_UNKNOWN,
	.current_norm   = V4L2_STD_UNKNOWN,
};

struct v4l2pim_minor *v4l2pim_create_minor()
{
	struct v4l2pim_minor *v4l2pim_minor;
	struct video_device *vfd;
	unsigned long flags;
	int ret;
	int new_minor = -1;

	v4l2pim_minor = NULL;
	vfd = NULL;

	if (v4l2pim_num_minors == V4L2PIM_MAX_MINORS)
		return NULL;

	/* Assign a minor number */
	new_minor = vcrtcm_id_generator_get(&v4l2pim_minor_id_generator,
						VCRTCM_ID_REUSE);
	if (new_minor < 0)
		return NULL;

	v4l2pim_minor = kzalloc(sizeof(struct v4l2pim_minor), GFP_KERNEL);
	if (!v4l2pim_minor) {
		VCRTCM_ERROR("failed alloc of v4l2pim_minor\n");
		return NULL;
	}

	mutex_init(&v4l2pim_minor->mlock);
	spin_lock_init(&v4l2pim_minor->slock);

	atomic_set(&v4l2pim_minor->kmalloc_track, 0);
	atomic_set(&v4l2pim_minor->page_track, 0);
	atomic_set(&v4l2pim_minor->vmalloc_track, 0);
	atomic_set(&v4l2pim_minor->users, 0);
	v4l2pim_minor->shadowbuf = NULL;
	v4l2pim_minor->shadowbufsize = 0;
	mutex_init(&v4l2pim_minor->sb_lock);
	v4l2pim_minor->shadowbuf_pages = NULL;
	v4l2pim_minor->shadowbuf_num_pages = 0;

	snprintf(v4l2pim_minor->v4l2_dev.name,
			sizeof(v4l2pim_minor->v4l2_dev.name),
			"v4l2pim-%03d", new_minor);
	ret = v4l2_device_register(NULL, &v4l2pim_minor->v4l2_dev);
	if (ret)
		goto free_info;
	videobuf_queue_vmalloc_init(&v4l2pim_minor->vb_vidq, &v4l2pim_video_qops,
					NULL, &v4l2pim_minor->slock,
					V4L2_BUF_TYPE_VIDEO_CAPTURE,
					V4L2_FIELD_NONE,
					sizeof(struct videobuf_buffer),
					v4l2pim_minor, &v4l2pim_minor->mlock);
	INIT_LIST_HEAD(&v4l2pim_minor->active);
	vfd = video_device_alloc();
	if (!vfd)
		goto unreg_dev;
	*vfd = v4l2pim_template;
	vfd->lock = &v4l2pim_minor->mlock;
	vfd->v4l2_dev = &v4l2pim_minor->v4l2_dev;
	vfd->minor = new_minor;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		goto rel_dev;
	video_set_drvdata(vfd, v4l2pim_minor);
	v4l2pim_minor->vfd = vfd;
	v4l2pim_minor->fmt = &formats[0];

	mutex_init(&v4l2pim_minor->buffer_mutex);
	spin_lock_init(&v4l2pim_minor->v4l2pim_lock);

	v4l2pim_minor->minor = new_minor;
	INIT_LIST_HEAD(&v4l2pim_minor->list);

	init_waitqueue_head(&v4l2pim_minor->xmit_sync_queue);
	v4l2pim_minor->enabled_queue = 1;

	v4l2pim_minor->workqueue = create_workqueue("v4l2pim_workers");

	v4l2pim_minor->pcon = NULL;

	INIT_DELAYED_WORK(&v4l2pim_minor->fake_vblank_work, v4l2pim_fake_vblank);

	spin_lock_irqsave(&v4l2pim_minor->v4l2pim_lock, flags);
	v4l2pim_minor->status = 0;
	spin_unlock_irqrestore(&v4l2pim_minor->v4l2pim_lock, flags);
	list_add(&v4l2pim_minor->list, &v4l2pim_minor_list);
	return v4l2pim_minor;
rel_dev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&v4l2pim_minor->v4l2_dev);
free_info:
	kfree(v4l2pim_minor);
	return NULL;

}

void v4l2pim_destroy_minor(struct v4l2pim_minor *v4l2pim_minor)
{
	video_unregister_device(v4l2pim_minor->vfd);
	v4l2_device_unregister(&v4l2pim_minor->v4l2_dev);
	cancel_delayed_work_sync(&v4l2pim_minor->fake_vblank_work);
	mutex_lock(&v4l2pim_minor->sb_lock);
	if (v4l2pim_minor->shadowbuf)
		v4l2pim_free_shadowbuf(v4l2pim_minor);
	mutex_unlock(&v4l2pim_minor->sb_lock);
	vcrtcm_id_generator_put(&v4l2pim_minor_id_generator,
					v4l2pim_minor->minor);
	v4l2pim_num_minors--;
	list_del(&v4l2pim_minor->list);
	kfree(v4l2pim_minor);
}

static struct vcrtcm_pim_funcs v4l2pim_pim_funcs = {
	.instantiate = v4l2pim_instantiate,
	.destroy = v4l2pim_destroy
};

static int __init v4l2pim_init(void)
{
	dev_t dev;
	int r;

	VCRTCM_INFO("v4l2 PCON, (C) Bell Labs, Alcatel-Lucent, Inc.\n");

	INIT_LIST_HEAD(&v4l2pim_minor_list);
	vcrtcm_id_generator_init(&v4l2pim_minor_id_generator,
					V4L2PIM_MAX_MINORS);

	VCRTCM_INFO("Allocating/registering dynamic major number");
	r = alloc_chrdev_region(&dev, 0, V4L2PIM_MAX_MINORS, "v4l2pim");
	if (r) {
		VCRTCM_ERROR("cannot get major device number\n");
		return r;
	}
	v4l2pim_major = MAJOR(dev);
	VCRTCM_INFO("Using major device number %d\n", v4l2pim_major);

	if (V4L2PIM_VID_LIMIT_MAX < vid_limit) {
		VCRTCM_WARNING("vid_limit (%d) too high, V4L2PIM_VID_LIMIT_MAX = %d\n", vid_limit, V4L2PIM_VID_LIMIT_MAX);
		vid_limit = V4L2PIM_VID_LIMIT_MAX;
	}
	VCRTCM_INFO("Maximum stream memory allowable is %d\n", vid_limit);

	VCRTCM_INFO("Registering with pimmgr\n");
	vcrtcm_pim_register(V4L2PIM_PIM_NAME, &v4l2pim_pim_funcs);

	VCRTCM_INFO("v4l2 PCON Loaded\n");

	return 0;
}

static void __exit v4l2pim_exit(void)
{
	struct v4l2pim_minor *minor, *tmp;

	VCRTCM_INFO("shutting down v4l2pim\n");
	vcrtcm_pim_unregister(V4L2PIM_PIM_NAME);
	list_for_each_entry_safe(minor, tmp, &v4l2pim_minor_list, list) {
		v4l2pim_detach_pcon(minor->pcon); /* ignore return code */
		v4l2pim_destroy_pcon(minor->pcon);
		v4l2pim_destroy_minor(minor);
	}
	unregister_chrdev_region(MKDEV(v4l2pim_major, 0), v4l2pim_num_minors);
	vcrtcm_id_generator_destroy(&v4l2pim_minor_id_generator);
	VCRTCM_INFO("exiting v4l2pim\n");
}

module_init(v4l2pim_init);
module_exit(v4l2pim_exit);

MODULE_PARM_DESC(v4l2pim_debug, "Enable debugging information.");
module_param_named(debug, v4l2pim_debug, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(vid_limit, "MB of memory allowed for streaming buffers (default=16)");
module_param_named(stream_mem, vid_limit, uint, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("v4l2 PCON");
MODULE_AUTHOR("Hans Christian Woithe (hans.woithe@alcatel-lucent.com)");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
