/*
 * Copyright (C) 2011 Alcatel-Lucent, Inc.
 * Authors: Hans Christian Woithe <hans.woithe@alcatel-lucent.com>
 *          Bill Katsak <william.katsak@alcatel-lucent.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
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
#include <vcrtcm/vcrtcm_alloc.h>
#include "v4l2pim.h"
#include "v4l2pim_vcrtcm.h"

#define V4L2PIM_MAJOR_VERSION 0
#define V4L2PIM_MINOR_VERSION 2
#define V4L2PIM_RELEASE 0
#define V4L2PIM_VERSION \
	KERNEL_VERSION(V4L2PIM_MAJOR_VERSION, V4L2PIM_MINOR_VERSION, V4L2PIM_RELEASE)

LIST_HEAD(v4l2pim_minor_list);
int v4l2pim_num_minors;
int v4l2pim_fake_vblank_slack = 1;
static unsigned int vid_limit = 16;
int v4l2pim_debug;
int v4l2pim_log_pim_alloc_counts;
int v4l2pim_log_pcon_alloc_counts;
int v4l2pim_pimid = -1;

/* ID generator for allocating minor numbers */
static struct vcrtcm_id_generator minor_id_generator;

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

static int swizzle_pixel(char *dst, char *src, uint32_t fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_RGB32:
		/* reorder the bytes */
		dst[0] = src[2];
		dst[1] = src[1];
		dst[2] = src[0];
		dst[3] = src[3];
		break;
	case V4L2_PIX_FMT_BGR24:
		/* get rid of alpha */
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		break;
	case V4L2_PIX_FMT_RGB24:
		/* reorder the bytes, get rid of alpha */
		dst[0] = src[2];
		dst[1] = src[1];
		dst[2] = src[0];
		break;
	case V4L2_PIX_FMT_RGB565:
		dst[0] = ((src[1] & 0x1C) << 3) | ((src[0] >> 3) & 0xFF);
		dst[1] = (src[2] & 0xF8) | ((src[1] >> 5) & 0xFF);
		break;
	case V4L2_PIX_FMT_RGB555:
		dst[0] = ((src[1] & 0xF8) << 2) | ((src[0] >> 3) & 0xFF);
		dst[1] = ((src[2] & 0xF8) >> 1) | ((src[1] & 0xF8) >> 6);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int copy_line(char *dst, char *src, int hpixels, struct v4l2pim_fmt *fmt)
{
	int hlen, dst_bpp, src_bpp;
	char *src_end;

	/*
	 * FIXME: source bpp is hard-coded and assumes that GPU delivers 32-bit
	 * format. In a perfect world we should pick up the bpp from VCRTCM, but
	 * if we start supporting multiple source formats, then this whole
	 * function will change, so hard coding src_bpp is not much of a sin.
	 */
	src_bpp = 4;
	dst_bpp = fmt->depth >> 3;
	hlen = hpixels * dst_bpp;
	src_end = src + hpixels * src_bpp;
	if (fmt->fourcc == V4L2_PIX_FMT_BGR32) {
		/* native format so just copy */
		BUG_ON(src_bpp != dst_bpp);
		memcpy(dst, src, hlen);
	} else {
		while (src < src_end) {
			int r;

			r = swizzle_pixel(dst, src, fmt->fourcc);
			if (unlikely(r))
				return r;
			src += src_bpp;
			dst += dst_bpp;
		}
	}
	return hlen;
}

static uint32_t fb_size(struct v4l2pim_minor *minor)
{
	uint32_t r;

	r = minor->frame_width * minor->frame_height;
	r *= minor->fmt->depth >> 3;
	return r;
}

static void start_generating(struct v4l2pim_minor *minor)
{
	mutex_lock(&minor->buffer_mutex);
	set_bit(V4L2PIM_STATUS_GENERATING, &minor->status);
	mutex_unlock(&minor->buffer_mutex);
	return;
}

static void stop_generating(struct v4l2pim_minor *minor)
{
	mutex_lock(&minor->buffer_mutex);
	if (!test_and_clear_bit(V4L2PIM_STATUS_GENERATING, &minor->status)) {
		mutex_unlock(&minor->buffer_mutex);
		return;
	}
	videobuf_stop(&minor->vb_vidq);
	mutex_unlock(&minor->buffer_mutex);
}

static int is_generating(struct v4l2pim_minor *minor)
{
	return test_bit(V4L2PIM_STATUS_GENERATING, &minor->status);
}

static int is_open(struct v4l2pim_minor *minor)
{
	return test_bit(V4L2PIM_STATUS_OPEN, &minor->status);
}

static int is_active(struct v4l2pim_minor *minor)
{
	return test_bit(V4L2PIM_STATUS_ACTIVE, &minor->status);
}

/*
 * This function copies the frame from the VCRTCM push buffer into
 * videobuf. This is the primary interface betweeh the VCRTCM-facing
 * side of the driver and the V4L2-facing side of the driver.
 * The function references PCON structure, but it does not lock
 * the PCON because it is called in the context of VBLANK-emulation
 * thread, so the PCON is already locked.
 */
int v4l2pim_deliver_frame(struct v4l2pim_minor *minor, int push_buffer_index)
{
	struct videobuf_buffer *vb;
	struct v4l2pim_pcon *pcon = minor->pcon;
	unsigned long flags;
	unsigned int hpixels, vpixels, pitch;
	unsigned int vp_offset, vpx, vpy;
	int i, r = 0;
	int vbsize;
	char *src, *dst;
	struct timeval ts;

	BUG_ON(!pcon);
	if (!is_generating(minor))
		return -EINVAL;
	spin_lock_irqsave(&minor->slock, flags);
	if (list_empty(&minor->active))
		goto unlock;
	vb = list_entry(minor->active.next,
			struct videobuf_buffer, queue);
	list_del(&vb->queue);
	dst = videobuf_to_vmalloc(vb);
	if (!dst) {
		r = -EFAULT;
		goto unlock;
	}
	hpixels = pcon->vcrtcm_fb.hdisplay;
	vpixels = pcon->vcrtcm_fb.vdisplay;
	vpx = pcon->vcrtcm_fb.viewport_x;
	vpy = pcon->vcrtcm_fb.viewport_y;
	pitch = pcon->vcrtcm_fb.pitch;
	vp_offset = pitch * vpy + vpx * (pcon->vcrtcm_fb.bpp >> 3);
	src = pcon->pb_fb[push_buffer_index] + vp_offset;
	vbsize = 0;
	for (i = 0; i < vpixels; i++) {
		int bcopied;

		bcopied = copy_line(dst, src, hpixels, minor->fmt);
		if (bcopied < 0) {
			r = bcopied;
			goto unlock;
		}
		src += pitch;
		dst += bcopied;
		vbsize += bcopied;
	}
	vb->size = vbsize;
	vb->field_count++;
	do_gettimeofday(&ts);
	vb->ts = ts;
	vb->state = VIDEOBUF_DONE;
	wake_up(&vb->done);
unlock:
	spin_unlock_irqrestore(&minor->slock, flags);
	return r;
}

static int
buf_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct v4l2pim_minor *minor;

	minor = vq->priv_data;
	*size = fb_size(minor);
	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;
	return 0;
}

static void free_buf(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct videobuf_vmalloc_memory *mem = vb->priv;

	BUG_ON(!mem);
	if (mem->vaddr)
		videobuf_vmalloc_free(vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int
buf_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct v4l2pim_minor *minor;
	int ret;

	minor = vq->priv_data;
	if (0 != vb->baddr && vb->bsize < fb_size(minor))
		return -EINVAL;

	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		vb->size = fb_size(minor);
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret < 0)
			goto fail;
		vb->width = minor->frame_width;
		vb->height = minor->frame_height;
		vb->field = field;
	}
	vb->state = VIDEOBUF_PREPARED;
	return 0;

fail:
	free_buf(vq, vb);
	vb->size = 0;
	return ret;
}

static void
buf_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct v4l2pim_minor *minor;
	minor = vq->priv_data;
	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &minor->active);
}

static void buf_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	free_buf(vq, vb);
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct v4l2pim_minor *minor;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	strcpy(cap->driver, "v4l2pim");
	strcpy(cap->card, "v4l2pim");
	strlcpy(cap->bus_info, minor->v4l2_dev.name,
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

static void populate_v4l2_format(struct v4l2_format *f, int w, int h,
				 struct v4l2pim_fmt *fmt, enum v4l2_field field)
{
	f->fmt.pix.width = w;
	f->fmt.pix.height = h;
	f->fmt.pix.field = field;
	f->fmt.pix.pixelformat  = fmt->fourcc;
	f->fmt.pix.colorspace = fmt->colorspace;
	f->fmt.pix.bytesperline = w * (fmt->depth >> 3) ;
	f->fmt.pix.sizeimage = w * f->fmt.pix.bytesperline;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2pim_minor *minor;
	struct v4l2pim_fmt *fmt;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	fmt = minor->fmt;
	if (!fmt)
		return -EINVAL;
	populate_v4l2_format(f, minor->frame_width, minor->frame_height,
			     fmt, V4L2_FIELD_NONE);
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct v4l2pim_minor *minor;
	struct v4l2pim_fmt *fmt;
	enum v4l2_field field;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	if (is_generating(minor))
		return -EBUSY;
	fmt = get_format(f);
	if (!fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != field)
		return -EINVAL;
	populate_v4l2_format(f, minor->frame_width, minor->frame_height,
			     fmt, field);
	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2pim_minor *minor;
	int r;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	r = vidioc_try_fmt_vid_cap(file, priv, f);
	if (r)
		return r;
	minor->fmt = get_format(f);
	minor->vb_vidq.field = f->fmt.pix.field;
	return 0;
}

static ssize_t
v4l2pim_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct v4l2pim_minor *minor;
	int r;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	start_generating(minor);
	r = videobuf_read_stream(&minor->vb_vidq, data, count, ppos, 0,
				 file->f_flags & O_NONBLOCK);
	atomic_dec(&minor->syscall_count);
	return r;
}

static unsigned int
v4l2pim_poll(struct file *file, struct poll_table_struct *wait)
{
	struct v4l2pim_minor *minor;
	int r;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	start_generating(minor);
	r = videobuf_poll_stream(file, &minor->vb_vidq, wait);
	atomic_dec(&minor->syscall_count);
	return r;
}

static int v4l2pim_open(struct file *file)
{
	struct v4l2pim_minor *minor;

	minor = video_drvdata(file);
	if (!minor || !is_active(minor))
		return -ENODEV;
	/* claim the device and do not allow multiple openers */
	if (test_and_set_bit(V4L2PIM_STATUS_OPEN, &minor->status))
		return -EBUSY;
	atomic_inc(&minor->users);
	v4l2pim_get_fb_attrs(minor);
	return 0;
}

static int v4l2pim_release(struct file *file)
{
	struct v4l2pim_minor *minor;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	/* mark the state for shutdown */
	if (!test_and_clear_bit(V4L2PIM_STATUS_OPEN, &minor->status))
		return -EBADFD;
	/* give chance to system calls to finish */
	while(atomic_read(&minor->syscall_count)) {
		/* FIX ME: raise the hell if syscall won't return */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(10));
	}
	/* now it's safe to clean up */
	stop_generating(minor);
	atomic_dec(&minor->users);
	return 0;
}

static int v4l2pim_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct v4l2pim_minor *minor;
	int ret;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	ret = videobuf_mmap_mapper(&minor->vb_vidq, vma);
	atomic_dec(&minor->syscall_count);
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
	struct v4l2pim_minor *minor;
	int r;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	r = videobuf_reqbufs(&minor->vb_vidq, p);
	atomic_dec(&minor->syscall_count);
	return r;
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pim_minor *minor;
	int r;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	r = videobuf_querybuf(&minor->vb_vidq, p);
	atomic_dec(&minor->syscall_count);
	return r;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pim_minor *minor;
	int r;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	r = videobuf_qbuf(&minor->vb_vidq, p);
	atomic_dec(&minor->syscall_count);
	return r;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2pim_minor *minor;
	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	return videobuf_dqbuf(&minor->vb_vidq, p,
				file->f_flags & O_NONBLOCK);
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct v4l2pim_minor *minor;
	int ret;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	ret = videobuf_streamon(&minor->vb_vidq);
	if (ret) {
		atomic_dec(&minor->syscall_count);
		return ret;
	}
	start_generating(minor);
	atomic_dec(&minor->syscall_count);
	return 0;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct v4l2pim_minor *minor;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	atomic_inc(&minor->syscall_count);
	if (!is_open(minor)) {
		atomic_dec(&minor->syscall_count);
		return -ESHUTDOWN;
	}
	stop_generating(minor);
	atomic_dec(&minor->syscall_count);
	return 0;
}

static int
vidioc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *parm)
{
	struct v4l2pim_minor *minor;
	struct v4l2_fract timeperframe;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	if (parm->type !=  V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	timeperframe.numerator = 1001;
	timeperframe.denominator = v4l2pim_get_fps(minor) * 1000;

	parm->parm.capture.capability |=  V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm->parm.capture.timeperframe = timeperframe;
	return 0;
}

static int
vidioc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *parm)
{
	struct v4l2pim_minor *minor;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	if (parm->type !=  V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* ffplay will freak out if this doesn't return 0 */
	return 0;
	/* return -EINVAL; */
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
					struct v4l2_frmsizeenum *fsize)
{
	struct v4l2pim_minor *minor;
	uint32_t i;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	if (fsize->index != 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (fsize->pixel_format == formats[i].fourcc) {
			fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			fsize->discrete.width = minor->frame_width;
			fsize->discrete.height = minor->frame_height;
			return 0;
		}

	return -EINVAL;
}

static int vidioc_enum_frameintervals(struct file *file, void *fh,
					struct v4l2_frmivalenum *fival)
{
	struct v4l2pim_minor *minor;
	uint32_t i;

	minor = video_drvdata(file);
	if (!minor)
		return -ENODEV;
	if (fival->index != 0)
		return -EINVAL;
	if (fival->width != minor->frame_width &&
	    fival->height != minor->frame_height)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (fival->pixel_format == formats[i].fourcc) {
			int fps = v4l2pim_get_fps(minor);

			fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
			fival->discrete.numerator = 1001;
			fival->discrete.denominator = fps * 1000;
			return 0;
		}
	return -EINVAL;
}

int v4l2pim_alloc_shadowbuf(struct v4l2pim_minor *minor, int w, int h, int bpp)
{
	struct page **pages;
	unsigned int num_pages;
	uint8_t *shadowbuf;
	int result;
	int size;
	unsigned long flags;

	if (!minor)
		return -EINVAL;

	v4l2pim_free_shadowbuf(minor);
	size = w * h * (bpp >> 3);
	num_pages = size / PAGE_SIZE;
	if (size % PAGE_SIZE > 0)
		num_pages++;

	pages = vcrtcm_kmalloc(sizeof(struct page *) * num_pages,
		GFP_KERNEL, VCRTCM_OWNER_PIM | v4l2pim_pimid);
	if (!pages)
		goto sb_alloc_err;
	result = vcrtcm_alloc_multiple_pages(GFP_KERNEL, pages,
		num_pages, VCRTCM_OWNER_PIM | v4l2pim_pimid);
	if (result != 0)
		goto sb_alloc_mpages_err;
	shadowbuf = vm_map_ram(pages, num_pages, 0, PAGE_KERNEL);
	if (!shadowbuf)
		goto sb_alloc_map_err;
	memset(shadowbuf, 0, size);

	spin_lock_irqsave(&minor->sb_lock, flags);
	minor->shadowbuf = shadowbuf;
	minor->shadowbufsize = size;
	minor->shadowbuf_pages = pages;
	minor->shadowbuf_num_pages = num_pages;
	minor->frame_width = w;
	minor->frame_height = h;
	spin_unlock_irqrestore(&minor->sb_lock, flags);

	return 0;

sb_alloc_map_err:
	vcrtcm_free_multiple_pages(pages, num_pages,
				   VCRTCM_OWNER_PIM | v4l2pim_pimid);
sb_alloc_mpages_err:
	vcrtcm_kfree(pages);
sb_alloc_err:

	return -ENOMEM;
}

void v4l2pim_free_shadowbuf(struct v4l2pim_minor *minor)
{
	unsigned long flags;
	uint8_t *shadowbuf;
	struct page **shadowbuf_pages;
	unsigned int shadowbuf_num_pages;

	if (!minor)
		return;
	spin_lock_irqsave(&minor->slock, flags);
	if (!minor->shadowbuf) {
		spin_unlock_irqrestore(&minor->slock, flags);
		return;
	}
	shadowbuf = minor->shadowbuf;
	shadowbuf_pages = minor->shadowbuf_pages;
	shadowbuf_num_pages = minor->shadowbuf_num_pages;
	minor->shadowbuf = NULL;
	minor->shadowbufsize = 0;
	minor->shadowbuf_pages = NULL;
	minor->shadowbuf_num_pages = 0;
	spin_unlock_irqrestore(&minor->slock, flags);
	vm_unmap_ram(shadowbuf,
		shadowbuf_num_pages);
	vcrtcm_free_multiple_pages(shadowbuf_pages,
		shadowbuf_num_pages, VCRTCM_OWNER_PIM | v4l2pim_pimid);
	vcrtcm_kfree(shadowbuf_pages);
}

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
	struct v4l2pim_minor *minor;
	struct video_device *vfd;
	int ret;
	int minornum = -1;

	minor = NULL;
	vfd = NULL;

	if (v4l2pim_num_minors == V4L2PIM_MAX_MINORS)
		return NULL;
	v4l2pim_num_minors++;

	minornum = vcrtcm_id_generator_get(&minor_id_generator,
					   VCRTCM_ID_REUSE);
	if (minornum < 0)
		goto minor_dec;

	minor = vcrtcm_kzalloc(sizeof(struct v4l2pim_minor),
			       GFP_KERNEL, VCRTCM_OWNER_PIM | v4l2pim_pimid);
	if (!minor)
		goto gen_put;

	spin_lock_init(&minor->slock);
	atomic_set(&minor->users, 0);
	atomic_set(&minor->syscall_count, 0);
	spin_lock_init(&minor->sb_lock);

	snprintf(minor->v4l2_dev.name,
			sizeof(minor->v4l2_dev.name),
			"v4l2pim-%03d", minornum);
	ret = v4l2_device_register(NULL, &minor->v4l2_dev);
	if (ret)
		goto free_info;
	videobuf_queue_vmalloc_init(&minor->vb_vidq, &v4l2pim_video_qops,
					NULL, &minor->slock,
					V4L2_BUF_TYPE_VIDEO_CAPTURE,
					V4L2_FIELD_NONE,
					sizeof(struct videobuf_buffer),
					minor, NULL);
	INIT_LIST_HEAD(&minor->active);
	vfd = video_device_alloc();
	if (!vfd)
		goto unreg_dev;
	*vfd = v4l2pim_template;
	vfd->v4l2_dev = &minor->v4l2_dev;
	vfd->minor = minornum;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		goto rel_dev;
	video_set_drvdata(vfd, minor);
	minor->vfd = vfd;
	minor->fmt = &formats[0];

	mutex_init(&minor->buffer_mutex);
	minor->minor = minornum;
	INIT_LIST_HEAD(&minor->list);
	minor->pcon = NULL;
	list_add(&minor->list, &v4l2pim_minor_list);
	return minor;
rel_dev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&minor->v4l2_dev);
free_info:
	vcrtcm_kfree(minor);
gen_put:
	vcrtcm_id_generator_put(&minor_id_generator, minornum);
minor_dec:
	v4l2pim_num_minors--;	
	return NULL;

}

void v4l2pim_destroy_minor(struct v4l2pim_minor *minor)
{
	video_unregister_device(minor->vfd);
	v4l2_device_unregister(&minor->v4l2_dev);
	v4l2pim_free_shadowbuf(minor);
	vcrtcm_id_generator_put(&minor_id_generator, minor->minor);
	BUG_ON(v4l2pim_num_minors == 0);
	v4l2pim_num_minors--;
	list_del(&minor->list);
	vcrtcm_kfree(minor);
}

static struct vcrtcm_pim_funcs v4l2pim_pim_funcs = {
	.instantiate = v4l2pim_instantiate,
	.destroy = v4l2pim_destroy
};

static int __init v4l2pim_init(void)
{
	VCRTCM_INFO("v4l2 PCON, (C) Bell Labs, Alcatel-Lucent, Inc.\n");
	vcrtcm_pim_register(V4L2PIM_PIM_NAME, &v4l2pim_pim_funcs,
			    &v4l2pim_pimid);
	vcrtcm_pim_log_alloc_cnts(v4l2pim_pimid, v4l2pim_log_pim_alloc_counts);
	vcrtcm_id_generator_init(&minor_id_generator, V4L2PIM_MAX_MINORS);
	if (V4L2PIM_VID_LIMIT_MAX < vid_limit) {
		VCRTCM_WARNING("vid_limit (%d) too high, max = %d\n",
			       vid_limit, V4L2PIM_VID_LIMIT_MAX);
		vid_limit = V4L2PIM_VID_LIMIT_MAX;
	}
	VCRTCM_INFO("Maximum stream memory allowable is %d\n", vid_limit);
	vcrtcm_pim_enable_callbacks(v4l2pim_pimid);
	return 0;
}

static void __exit v4l2pim_exit(void)
{
	struct v4l2pim_minor *minor, *tmp;

	VCRTCM_INFO("shutting down v4l2pim\n");
	vcrtcm_pim_disable_callbacks(v4l2pim_pimid);
	list_for_each_entry_safe(minor, tmp, &v4l2pim_minor_list, list) {
		int pconid = minor->pcon->pconid;

		vcrtcm_p_lock_pconid(pconid);
		v4l2pim_detach_pcon(minor->pcon, 1); /* ignore return code */
		v4l2pim_destroy_pcon(minor->pcon);
		vcrtcm_p_unlock_pconid(pconid);
		v4l2pim_destroy_minor(minor);
	}
	vcrtcm_pim_unregister(v4l2pim_pimid);
	vcrtcm_id_generator_destroy(&minor_id_generator);
	VCRTCM_INFO("exiting v4l2pim\n");
}

module_init(v4l2pim_init);
module_exit(v4l2pim_exit);

MODULE_PARM_DESC(debug, "Enable debugging information");
module_param_named(debug, v4l2pim_debug, int,
		   S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(stream_mem, "MB of memory for streaming buffers (default=16)");
module_param_named(stream_mem, vid_limit, uint,
		   S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(log_pim_alloc_cnts,
		 "When set to 1, log all per-PIM alloc counts (default = 0)");
module_param_named(log_pim_alloc_cnts, v4l2pim_log_pim_alloc_counts,
		   int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(log_pcon_alloc_cnts,
		 "When set to 1, log all per-PCON alloc counts (default = 0)");
module_param_named(log_pcon_alloc_cnts, v4l2pim_log_pcon_alloc_counts,
		   int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("v4l2 PCON");
MODULE_AUTHOR("Hans Christian Woithe (hans.woithe@alcatel-lucent.com)");
MODULE_AUTHOR("William Katsak (william.katsak@alcatel-lucent.com)");
