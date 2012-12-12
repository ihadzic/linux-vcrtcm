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

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/prefetch.h>
#include <vcrtcm/vcrtcm_pim.h>
#include <vcrtcm/vcrtcm_utils.h>
#include <vcrtcm/vcrtcm_alloc.h>
#include <edid.h>

#include "udlpim.h"
#include "udlpim_vcrtcm.h"
#include "udlpim_usb.h"

/*
 * There are many DisplayLink-based graphics products, all with unique PIDs.
 * So we match on DisplayLink's VID + Vendor-Defined Interface Class (0xff)
 * We also require a match on SubClass (0x00) and Protocol (0x00),
 * which is compatible with all known USB 2.0 era graphics chips and firmware,
 * but allows DisplayLink to increment those for any future incompatible chips
 */
static struct usb_device_id id_table[] = {
	{.idVendor = 0x17e9,
	 .bInterfaceClass = 0xff,
	 .bInterfaceSubClass = 0x00,
	 .bInterfaceProtocol = 0x00,
	 .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
		USB_DEVICE_ID_MATCH_INT_CLASS |
		USB_DEVICE_ID_MATCH_INT_SUBCLASS |
		USB_DEVICE_ID_MATCH_INT_PROTOCOL,
	},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

struct usb_driver udlpim_driver = {
	.name = "udlpim",
	.probe = udlpim_usb_probe,
	.disconnect = udlpim_usb_disconnect,
	.id_table = id_table,
};

static struct udlpim_video_mode fallback_mode = {
		.xres = 640,
		.yres = 480,
		.pixclock = 39682,
		.left_margin = 48,
		.right_margin = 16,
		.upper_margin = 33,
		.lower_margin = 10,
		.hsync_len = 92,
		.vsync_len = 2
};

static struct udlpim_minor *udlpim_create_minor(void)
{
	struct udlpim_minor *minor;
	int minornum;

	if (udlpim_num_minors == UDLPIM_MAX_MINORS) {
		VCRTCM_ERROR("Maximum number of minors already assigned.\n");
		return NULL;
	}
	minornum = vcrtcm_id_generator_get(&udlpim_minor_id_generator,
						VCRTCM_ID_REUSE);
	if (minornum < 0)
		return NULL;
	minor = vcrtcm_kzalloc(sizeof(struct udlpim_minor), GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);
	if (!minor) {
		vcrtcm_id_generator_put(&udlpim_minor_id_generator, minornum);
		return NULL;
	}
	minor->minor = minornum;
	/* we need to wait for both usb and vcrtcm to finish on disconnect */
	kref_init(&minor->kref); /* matching kref_put in udb .disconnect fn */
	/*kref_get(&minor->kref); */ /* matching kref_put in vcrtcm detach */

	minor->sku_pixel_limit = 2048 * 1152;  /* default to maximum */
	mutex_init(&minor->buffer_mutex);
	spin_lock_init(&minor->lock);
	init_waitqueue_head(&minor->xmit_sync_queue);
	INIT_LIST_HEAD(&minor->list);
	minor->workqueue =
			alloc_workqueue("udlpim_workers", WQ_MEM_RECLAIM, 5);
	minor->enabled_queue = 1;
	INIT_DELAYED_WORK(&minor->query_edid_work, udlpim_query_edid);
	memcpy(&minor->default_video_mode,
			&fallback_mode, sizeof(struct udlpim_video_mode));
	atomic_set(&minor->usb_active, 1);
	udlpim_num_minors++;
	return minor;
}

static void udlpim_destroy_minor(struct udlpim_minor *minor)
{
	int minornum = minor->minor;

	VCRTCM_INFO("destroying minor %d\n", minornum);
	cancel_delayed_work_sync(&minor->query_edid_work);

	/* this function will wait for all in-flight urbs to complete */
	if (minor->urbs.count > 0)
		udlpim_free_urb_list(minor);

	UDLPIM_DEBUG("freeing edid: %p\n", minor->edid);
	vcrtcm_kfree(minor->edid);
	vcrtcm_kfree(minor->last_vcrtcm_mode_list);
	udlpim_unmap_scratch_memory(minor);
	udlpim_free_scratch_memory(minor);
	list_del(&minor->list);
	vcrtcm_kfree(minor);
	vcrtcm_id_generator_put(&udlpim_minor_id_generator, minor->minor);
	udlpim_num_minors--;
	VCRTCM_INFO("finished destroying minor %d\n", minornum);
}

static void udlpim_destroy_minor_kref(struct kref *kref)
{
	struct udlpim_minor *minor =
		container_of(kref, struct udlpim_minor, kref);
	udlpim_destroy_minor(minor);
}

/******************************************************************************
 * USB/device setup
 * These functions setup the driver for each attached device
 *****************************************************************************/

/* USB probe
 * This gets called when the a device is first identified
 * or connected. Sets up the driver structures, and notifies
 * VCRTCM that the device is available.
 */
static int udlpim_usb_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	struct udlpim_minor *minor;
	int retval = -EINVAL;

	usbdev = interface_to_usbdev(interface);
	minor = udlpim_create_minor();
	if (minor == NULL) {
		VCRTCM_ERROR("udlpim_usb_probe: failed alloc of udlpim_minor\n");
		return -ENOMEM;
	}
	minor->udev = usbdev;
	minor->gdev = &usbdev->dev;  /* generic struct device */
	usb_set_intfdata(interface, minor);

	VCRTCM_INFO("%s %s - serial #%s\n",
		usbdev->manufacturer, usbdev->product, usbdev->serial);
	VCRTCM_INFO("vid_%04x&pid_%04x&rev_%04x driver's udlpim_minor struct at %p\n",
		usbdev->descriptor.idVendor, usbdev->descriptor.idProduct,
		usbdev->descriptor.bcdDevice, minor);

	if (!udlpim_parse_vendor_descriptor(minor, usbdev)) {
		VCRTCM_ERROR("Firmware not recognized. Assume incompatible device.\n");
		goto error;
	}

	if (!udlpim_alloc_urb_list(minor, WRITES_IN_FLIGHT, MAX_TRANSFER)) {
		VCRTCM_ERROR("udlpim_alloc_urb_list failed\n");
		retval = -ENOMEM;
		goto error;
	}

	/* TODO: Investigate USB class business */

	udlpim_select_std_channel(minor);
	udlpim_set_video_mode(minor, &minor->default_video_mode);
	udlpim_blank_hw_fb(minor, UDLPIM_BLANK_COLOR);

	VCRTCM_INFO("DisplayLink USB device attached.\n");
	VCRTCM_INFO("successfully registered minor %d\n", minor->minor);
	list_add_tail(&minor->list, &udlpim_minor_list);

	/* Do an initial query of the EDID */
	udlpim_query_edid_core(minor);
	queue_delayed_work(minor->workqueue, &minor->query_edid_work, 0);
	return 0;

error:
	/* Ref for framebuffer */
	kref_put(&minor->kref, udlpim_destroy_minor_kref);
	/* vcrtcm reference */
	/* kref_put(&minor->kref, udlpim_destroy_minor_kref); */
	return retval;
}

/* USB disconnect */
/* This gets called on driver unload or device disconnection */
static void udlpim_usb_disconnect(struct usb_interface *interface)
{
	struct udlpim_minor *minor;

	minor = usb_get_intfdata(interface);

	UDLPIM_DEBUG("USB disconnect starting\n");

	/* TODO: Do we need this? Maybe we can just detach and be done */
	/* we virtualize until everyone is done with it, then we free */
	minor->virtualized = true;

	/* When non-active we'll update virtual framebuffer, but no new urbs */
	atomic_set(&minor->usb_active, 0);

	usb_set_intfdata(interface, NULL);

	if (minor->pcon) {
		int pconid = minor->pcon->pconid;

		vcrtcm_p_disable_callbacks(pconid);
		udlpim_detach_pcon(minor->pcon);
		udlpim_destroy_pcon(minor->pcon);
		vcrtcm_p_destroy(pconid);
	}

	/* TODO: Deal with reference count stuff. Perhaps have reference count
	until udlpim_vcrtcm_detach completes */
	kref_put(&minor->kref, udlpim_destroy_minor_kref); /* last ref from kref_init */
	/* kref_put(&minor->kref, udlpim_destroy_minor_kref);*/ /* Ref for framebuffer */
}

static void udlpim_free_urb_list(struct udlpim_minor *minor)
{
	int count = minor->urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;
	int ret;
	unsigned long flags;

	VCRTCM_INFO("waiting for completes and freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {

		/* Getting interrupted means a leak, but ok at shutdown*/
		ret = down_interruptible(&minor->urbs.limit_sem);
		if (ret)
			break;

		spin_lock_irqsave(&minor->urbs.lock, flags);

		node = minor->urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irqrestore(&minor->urbs.lock, flags);

		unode = list_entry(node, struct urb_node, entry);
		urb = unode->urb;

		/* Free each separately allocated piece */
		usb_free_coherent(urb->dev, minor->urbs.size,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		vcrtcm_kfree(node);
	}

}

/******************************************************************************
 * These functions are called from outside
 * this file, by the VCRTCM implementation.
 *****************************************************************************/

 /* Sets the device mode and allocates framebuffers */
int udlpim_setup_screen(struct udlpim_minor *minor,
	struct udlpim_video_mode *mode, struct vcrtcm_fb *vcrtcm_fb)
{
	int result;

	udlpim_unmap_scratch_memory(minor);
	udlpim_free_scratch_memory(minor);

	result = udlpim_alloc_scratch_memory(minor,
			vcrtcm_fb->pitch, vcrtcm_fb->vdisplay);

	if (result) {
		VCRTCM_ERROR("Could not alloc scratch memory.\n");
		return 1;
	}

	result = udlpim_map_scratch_memory(minor);

	if (result) {
		VCRTCM_ERROR("Could not map scratch memory.\n");
		return 1;
	}

	result = udlpim_set_video_mode(minor, mode);

	if (result) {
		VCRTCM_ERROR("Could not set screen mode\n");
		return 1;
	}

	result = udlpim_blank_hw_fb(minor, UDLPIM_BLANK_COLOR);

	if (result) {
		VCRTCM_ERROR("Could not blank HW framebuffer\n");
		return 1;
	}

	UDLPIM_DEBUG("Done with setup_screen\n");
	return 0;
}

int udlpim_error_screen(struct udlpim_minor *minor)
{
	udlpim_set_video_mode(minor, &minor->default_video_mode);
	udlpim_blank_hw_fb(minor, UDLPIM_ERROR_COLOR);

	return 0;
}

/*
 * Generates the appropriate command sequences that
 * tells the video controller to put the monitor to sleep.
 */
int udlpim_dpms_sleep(struct udlpim_minor *minor)
{
	pr_info("udlctd_dpms_sleep not implemented\n");
	return 0;
}

/* Resets the mode to wake up the display */
int udlpim_dpms_wakeup(struct udlpim_minor *minor)
{
	pr_info("udlctd_dpms_wakeup not implemented\n");
	return 0;
}

/* Transmits the framebuffer over USB to the monitor */
int udlpim_transmit_framebuffer(struct udlpim_minor *minor)
{
	int i, ret;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	struct urb *urb;
	int xres = minor->current_video_mode.xres;
	int yres = minor->current_video_mode.yres;
	int bytes_per_pixel = minor->bpp / 8;
	struct udlpim_pcon *pcon = minor->pcon;
	struct vcrtcm_fb *vcrtcm_fb = &pcon->vcrtcm_fb;

	start_cycles = get_cycles();

	if (!atomic_read(&minor->usb_active))
		return 0;
	if (pcon->pbd_fb[pcon->push_buffer_index]->virgin)
		return 0;
	urb = udlpim_get_urb(minor);
	if (!urb)
		return 0;

	cmd = urb->transfer_buffer;

	for (i = 0; i < yres; i++) {
		const int byte_offset =
			vcrtcm_fb->pitch * i +
			vcrtcm_fb->viewport_x * (vcrtcm_fb->bpp >> 3);

		if (udlpim_render_hline(minor, &urb,
				&cmd, byte_offset, xres * bytes_per_pixel,
				&bytes_identical, &bytes_sent))
			goto error;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len = cmd - (char *) urb->transfer_buffer;
		ret = udlpim_submit_urb(minor, urb, len);
		bytes_sent += len;
	} else
		udlpim_urb_completion(urb);
error:
	atomic_add(bytes_sent, &minor->bytes_sent);
	atomic_add(bytes_identical, &minor->bytes_identical);
	atomic_add(xres*yres*2, &minor->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles) >> 10)),
			&minor->cpu_kcycles_used);

	return 0;
}

int udlpim_build_modelist(struct udlpim_minor *minor,
			struct udlpim_video_mode **modes, int *mode_count)
{
	int i;
	struct fb_monspecs monspecs;
	struct fb_modelist *fb_modelist_ptr;
	struct list_head modelist;
	struct udlpim_video_mode *udlpim_video_modes;
	int num_modes = 0;
	char *edid_copy = NULL;
	unsigned long flags;
	INIT_LIST_HEAD(&modelist);
	memset(&monspecs, 0, sizeof(monspecs));

	UDLPIM_DEBUG("In build_modelist\n");

	/* Allocate a buffer for a local copy of the EDID */
	edid_copy = vcrtcm_kmalloc(EDID_LENGTH, GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);
	if (!edid_copy)
		goto error;

	/* Get the spinlock and copy the EDID into our local buffer */
	spin_lock_irqsave(&minor->lock, flags);
	if (minor->edid)
		memcpy(edid_copy, minor->edid, EDID_LENGTH);
	spin_unlock_irqrestore(&minor->lock, flags);

	/* If we have an EDID, parse it */
	fb_edid_to_monspecs(edid_copy, &monspecs);

	/* If the EDID parsed ok (we expect it to), extract the modes */
	if (monspecs.modedb_len > 0) {
		for (i = 0; i < monspecs.modedb_len; i++) {
			if (udlpim_is_valid_mode(
					minor,
					monspecs.modedb[i].xres,
					monspecs.modedb[i].yres)) {

				fb_add_videomode(&monspecs.modedb[i],
						&modelist);
			}
		}
		num_modes = monspecs.modedb_len;
	} else if (udlpim_enable_default_modes) {
		/*
		 * Add the standard VESA modes to our modelist
		 * Since we don't have EDID, there may be modes that
		 * overspec monitor and/or are incorrect aspect ratio, etc.
		 * But at least the user has a chance to choose
		 */
		for (i = 0; i < VESA_MODEDB_SIZE; i++) {
			if (udlpim_is_valid_mode(minor,
				((struct fb_videomode *)
						&vesa_modes[i])->xres,
				((struct fb_videomode *)
						&vesa_modes[i])->yres)) {

				fb_add_videomode(&vesa_modes[i],
						&modelist);
				num_modes++;
			}
		}
		num_modes = VESA_MODEDB_SIZE;
	}

	/* Destroy the modedb inside the monspecs */
	/* fb_edid_to_monspecs() kallocs memory for the modedb */
	/* and it needs to be freed. */
	if (monspecs.modedb)
		fb_destroy_modedb(monspecs.modedb);

	udlpim_video_modes =
		vcrtcm_kmalloc(sizeof(struct udlpim_video_mode) * num_modes,
			GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);
	UDLPIM_DEBUG("Size of modelist %d\n", num_modes);

	if (!udlpim_video_modes) {
		VCRTCM_ERROR("Could not allocate memory for modelist\n");
		goto error;
	}

	/* Now we can build the modelist to return. */

	/* Build the udlpim modelist from the fbdev modelist */
	i = 0;
	list_for_each_entry(fb_modelist_ptr, &modelist, list) {
		udlpim_video_modes[i].xres = fb_modelist_ptr->mode.xres;
		udlpim_video_modes[i].yres = fb_modelist_ptr->mode.yres;
		udlpim_video_modes[i].pixclock = fb_modelist_ptr->mode.pixclock;
		udlpim_video_modes[i].left_margin =
			fb_modelist_ptr->mode.left_margin;
		udlpim_video_modes[i].right_margin =
			fb_modelist_ptr->mode.right_margin;
		udlpim_video_modes[i].upper_margin =
			fb_modelist_ptr->mode.upper_margin;
		udlpim_video_modes[i].lower_margin =
			fb_modelist_ptr->mode.lower_margin;
		udlpim_video_modes[i].hsync_len =
			fb_modelist_ptr->mode.hsync_len;
		udlpim_video_modes[i].vsync_len =
			fb_modelist_ptr->mode.vsync_len;
		udlpim_video_modes[i].refresh = fb_modelist_ptr->mode.refresh;
		i++;
	}

	fb_destroy_modelist(&modelist);
	vcrtcm_kfree(edid_copy);
	*modes = udlpim_video_modes;
	*mode_count = i;
	return 0;

error:
	fb_destroy_modelist(&modelist);
	vcrtcm_kfree(edid_copy);
	*modes = NULL;
	*mode_count = 0;
	return -ENOMEM;
}

int udlpim_free_modelist(struct udlpim_minor *minor,
		struct udlpim_video_mode *modes)
{
	vcrtcm_kfree(modes);
	return 0;
}

/* Do an EDID query. This is normally called inside a delayed_work, */
/* but is also called once by attach */
void udlpim_query_edid_core(struct udlpim_minor *minor)
{
	struct udlpim_pcon *pcon;
	struct fb_monspecs monspecs;
	char *new_edid, *old_edid;
	int new_edid_valid = 0;
	int tries = UDLPIM_EDID_QUERY_TRIES;
	int i;
	unsigned long flags;

	UDLPIM_DEBUG("In udlpim_query_edid\n");

	pcon = minor->pcon;

	new_edid = vcrtcm_kmalloc(EDID_LENGTH, GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);

	if (!new_edid) {
		VCRTCM_ERROR("Could not allocate memory for EDID query.\n");
		return;
	}

	memset(&monspecs, 0, sizeof(monspecs));

	/*
	 * Try to (re)read EDID from hardware first
	 * EDID data may return, but not parse as valid
	 * Try again a few times, in case of e.g. analog cable noise
	 */
	while (tries-- && !new_edid_valid) {
		i = udlpim_get_edid(minor, new_edid, EDID_LENGTH);

		/* If we got an incomplete EDID. */
		if (i < EDID_LENGTH)
			continue;

		/* Otherwise, we try to parse it. */
		fb_edid_to_monspecs(new_edid, &monspecs);

		/* If it parses, it is valid. */
		if (monspecs.modedb_len > 0)
			new_edid_valid = 1;

		/* Destroy the modedb inside the monspecs */
		/* fb_edid_to_monspecs() kallocs memory for the modedb */
		/* and it needs to be freed. */
		if (monspecs.modedb)
			fb_destroy_modedb(monspecs.modedb);
	}

	if (!new_edid_valid) {
		vcrtcm_kfree(new_edid);
		new_edid = NULL;
	}

	spin_lock_irqsave(&minor->lock, flags);
	old_edid = minor->edid;
	minor->edid = new_edid;
	minor->monitor_connected = new_edid ? 1 : 0;
	spin_unlock_irqrestore(&minor->lock, flags);

	if (pcon && ((!old_edid && new_edid) ||
		(old_edid && !new_edid) || (old_edid && new_edid &&
		memcmp(old_edid, new_edid, EDID_LENGTH) != 0))) {
		UDLPIM_DEBUG("Calling hotplug.\n");
		vcrtcm_p_hotplug(pcon->pconid);
	}

	if (old_edid)
		vcrtcm_kfree(old_edid);
}

/******************************************************************************
 * ATTENTION:
 * Everything past here is static and only accessed from inside udlpim_usb.c
 *****************************************************************************/

/*
 * Trims identical data from front and back of line
 * Sets new front buffer address and width
 * And returns byte count of identical pixels
 * Assumes CPU natural alignment (unsigned long)
 * for back and front buffer ptrs and width
 */

static int udlpim_trim_hline(const u8 *bback, const u8 **bfront, int *width_bytes)
{
	int j, k;
	const unsigned long *back = (const unsigned long *) bback;
	const unsigned long *front = (const unsigned long *) *bfront;
	const int width = *width_bytes / sizeof(unsigned long);
	int identical = width;
	int start = width;
	int end = width;

	prefetch((void *) front);
	prefetch((void *) back);

	for (j = 0; j < width; j++) {
		if (back[j] != front[j]) {
			start = j;
			break;
		}
	}

	for (k = width - 1; k > j; k--) {
		if (back[k] != front[k]) {
			end = k+1;
			break;
		}
	}

	identical = start + (width - end);
	*bfront = (u8 *) &front[start];
	*width_bytes = (end - start) * sizeof(unsigned long);

	return identical * sizeof(unsigned long);
}

static void udlpim_compress_hline_16(
	const uint16_t **pixel_start_ptr,
	const uint16_t *const pixel_end,
	uint32_t *device_address_ptr,
	uint8_t **command_buffer_ptr,
	const uint8_t *const cmd_buffer_end)
{
	const uint16_t *pixel = *pixel_start_ptr;
	uint32_t dev_addr  = *device_address_ptr;
	uint8_t *cmd = *command_buffer_ptr;
	const int bpp = 2;

	while ((pixel_end > pixel) &&
	       (cmd_buffer_end - MIN_RLX_CMD_BYTES > cmd)) {
		uint8_t *raw_pixels_count_byte = 0;
		uint8_t *cmd_pixels_count_byte = 0;
		const uint16_t *raw_pixel_start = 0;
		const uint16_t *cmd_pixel_start, *cmd_pixel_end = 0;

		prefetchw((void *) cmd); /* pull in one cache line at least */

		*cmd++ = 0xAF;
		*cmd++ = 0x6B;
		*cmd++ = (uint8_t) ((dev_addr >> 16) & 0xFF);
		*cmd++ = (uint8_t) ((dev_addr >> 8) & 0xFF);
		*cmd++ = (uint8_t) ((dev_addr) & 0xFF);

		cmd_pixels_count_byte = cmd++; /*  we'll know this later */
		cmd_pixel_start = pixel;

		raw_pixels_count_byte = cmd++; /*  we'll know this later */
		raw_pixel_start = pixel;

		cmd_pixel_end = pixel + min(MAX_CMD_PIXELS + 1,
			min((int)(pixel_end - pixel),
			    (int)(cmd_buffer_end - cmd) / bpp));

		prefetch_range((void *) pixel, (cmd_pixel_end - pixel) * bpp);

		while (pixel < cmd_pixel_end) {
			const uint16_t * const repeating_pixel = pixel;

			/* UDLPIM_DEBUG("pixel16: %x\n", *pixel); */
			*(uint16_t *)cmd = cpu_to_be16p(pixel);
			/* UDLPIM_DEBUG("cmd16: %x\n", *cmd); */
			cmd += 2;
			pixel++;

			if (unlikely((pixel < cmd_pixel_end) &&
				     (*pixel == *repeating_pixel))) {
				/* go back and fill in raw pixel count */
				*raw_pixels_count_byte = ((repeating_pixel -
						raw_pixel_start) + 1) & 0xFF;

				while ((pixel < cmd_pixel_end)
				       && (*pixel == *repeating_pixel)) {
					pixel++;
				}

				/* immediately after raw data is repeat byte */
				*cmd++ = ((pixel - repeating_pixel) - 1) & 0xFF;

				/* Then start another raw pixel span */
				raw_pixel_start = pixel;
				raw_pixels_count_byte = cmd++;
			}
		}

		if (pixel > raw_pixel_start) {
			/* finalize last RAW span */
			*raw_pixels_count_byte = (pixel-raw_pixel_start) & 0xFF;
		}

		*cmd_pixels_count_byte = (pixel - cmd_pixel_start) & 0xFF;
		dev_addr += (pixel - cmd_pixel_start) * bpp;
	}

	if (cmd_buffer_end <= MIN_RLX_CMD_BYTES + cmd) {
		UDLPIM_DEBUG("16 padded with %d\n", cmd_buffer_end-cmd);
		/* Fill leftover bytes with no-ops */
		if (cmd_buffer_end > cmd)
			memset(cmd, 0xAF, cmd_buffer_end - cmd);
		cmd = (uint8_t *) cmd_buffer_end;
	}

	*command_buffer_ptr = cmd;
	*pixel_start_ptr = pixel;
	*device_address_ptr = dev_addr;

	return;
}

static void udlpim_compress_hline_8(
	const uint8_t **pixel_start_ptr,
	const uint8_t *const pixel_end,
	uint32_t *device_address_ptr,
	uint8_t **command_buffer_ptr,
	const uint8_t *const cmd_buffer_end)
{
	const uint8_t *pixel = *pixel_start_ptr;
	uint32_t dev_addr  = *device_address_ptr;
	uint8_t *cmd = *command_buffer_ptr;
	const int bpp = 1;

	while ((pixel_end > pixel) &&
	       (cmd_buffer_end - MIN_RLX_CMD_BYTES > cmd)) {
		uint8_t *raw_pixels_count_byte = 0;
		uint8_t *cmd_pixels_count_byte = 0;
		const uint8_t *raw_pixel_start = 0;
		const uint8_t *cmd_pixel_start, *cmd_pixel_end = 0;

		prefetchw((void *) cmd); /* pull in one cache line at least */

		*cmd++ = 0xAF;
		*cmd++ = 0x63;
		*cmd++ = (uint8_t) ((dev_addr >> 16) & 0xFF);
		*cmd++ = (uint8_t) ((dev_addr >> 8) & 0xFF);
		*cmd++ = (uint8_t) ((dev_addr) & 0xFF);

		cmd_pixels_count_byte = cmd++; /*  we'll know this later */
		cmd_pixel_start = pixel;

		raw_pixels_count_byte = cmd++; /*  we'll know this later */
		raw_pixel_start = pixel;

		cmd_pixel_end = pixel + min(MAX_CMD_PIXELS + 1,
			min((int)(pixel_end - pixel),
			    (int)(cmd_buffer_end - cmd) / bpp));

		prefetch_range((void *) pixel, (cmd_pixel_end - pixel) * bpp);

		while (pixel < cmd_pixel_end) {
			const uint8_t * const repeating_pixel = pixel;

			*(uint8_t *)cmd = *pixel;

			cmd += 1;
			pixel++;

			if (unlikely((pixel < cmd_pixel_end) &&
				     (*pixel == *repeating_pixel))) {
				/* go back and fill in raw pixel count */
				*raw_pixels_count_byte = ((repeating_pixel -
						raw_pixel_start) + 1) & 0xFF;

				while ((pixel < cmd_pixel_end)
				       && (*pixel == *repeating_pixel)) {
					pixel++;
				}

				/* immediately after raw data is repeat byte */
				*cmd++ = ((pixel - repeating_pixel) - 1) & 0xFF;

				/* Then start another raw pixel span */
				raw_pixel_start = pixel;
				raw_pixels_count_byte = cmd++;
			}
		}

		if (pixel > raw_pixel_start) {
			/* finalize last RAW span */
			*raw_pixels_count_byte = (pixel-raw_pixel_start) & 0xFF;
		}

		*cmd_pixels_count_byte = (pixel - cmd_pixel_start) & 0xFF;
		dev_addr += (pixel - cmd_pixel_start) * bpp;
	}

	if (cmd_buffer_end <= MIN_RLX_CMD_BYTES + cmd) {
		UDLPIM_DEBUG("8 padded with %d\n", cmd_buffer_end-cmd);
		/* Fill leftover bytes with no-ops */
		if (cmd_buffer_end > cmd)
			memset(cmd, 0xAF, cmd_buffer_end - cmd);
		cmd = (uint8_t *) cmd_buffer_end;
	}

	*command_buffer_ptr = cmd;
	*pixel_start_ptr = pixel;
	*device_address_ptr = dev_addr;

	return;
}

static int udlpim_render_hline(struct udlpim_minor *minor,
			       struct urb **urb_ptr, char **urb_buf_ptr,
			       u32 byte_offset, u32 byte_width,
			       int *ident_ptr, int *sent_ptr)
{
	const u8 *line_start, *line_end, *next_pixel,
		*line_end16, *line_end8,
		*next_pixel16, *next_pixel8;

	uint32_t *pixel32;
	uint16_t *pixel16;
	uint8_t *pixel8;

	u32 dev_addr16 = 0, dev_addr8 = 0;
	struct urb *urb = *urb_ptr;
	u8 *cmd = *urb_buf_ptr;
	u8 *cmd_end = (u8 *) urb->transfer_buffer +
				urb->transfer_buffer_length;
	struct udlpim_pcon *pcon = minor->pcon;
	struct vcrtcm_cursor *vcrtcm_cursor = &pcon->vcrtcm_cursor;
	struct vcrtcm_fb *vcrtcm_fb = &pcon->vcrtcm_fb;

	/* We need line_num for the dev_offset and to do the cursor overlay */
	int line_num = byte_offset / vcrtcm_fb->pitch;

	/* We need to recalculate the offset in the device framebuffer */
	/* because our byte_offset uses pitch */
	int dev_offset = line_num * minor->current_video_mode.xres * 4;

	/* These are offsets in the source (virtual) frame buffer */
	line_start = (u8 *)minor->main_buffer + byte_offset;
	next_pixel = line_start;
	line_end = next_pixel + byte_width;

	/* Calculate offsets in the device frame buffer */
	if (minor->bpp == 32) {
		dev_addr16 = minor->base16 + dev_offset / 2;
		dev_addr8 = minor->base8 + dev_offset / 4;
	} else if (minor->bpp == 16) {
		dev_addr16 = minor->base16 + dev_offset;
		dev_addr8 = 0;
	}

	/*
	 * Overlay the cursor
	 */
	if (minor->cursor &&
	    vcrtcm_cursor->flag != VCRTCM_CURSOR_FLAG_HIDE &&
		pcon->pbd_cursor[pcon->push_buffer_index] &&
	    !pcon->pbd_cursor[pcon->push_buffer_index]->virgin &&
	    line_num >= vcrtcm_cursor->location_y &&
	    line_num < vcrtcm_cursor->location_y + vcrtcm_cursor->height) {
		int i, clip = 0;
		uint32_t *hline_pixel = (uint32_t *)line_start;
		uint32_t *cursor_pixel = (uint32_t *)minor->cursor;
		cursor_pixel += (line_num-vcrtcm_cursor->location_y) *
			vcrtcm_cursor->width;
		if (vcrtcm_cursor->location_x >= 0)
			hline_pixel += vcrtcm_cursor->location_x;
		else
			clip = -vcrtcm_cursor->location_x;
		cursor_pixel += clip;
		for (i = 0; i < vcrtcm_cursor->width - clip; i++) {
			if (hline_pixel >= (uint32_t *) line_end)
				break;
			udlpim_alpha_overlay_argb32(hline_pixel, cursor_pixel);
			cursor_pixel++;
			hline_pixel++;
		}
	}

	/* Find out what part of the line has changed.
	 * We only transmit the changed part.
	 */

	if (minor->backing_buffer) {
		int offset;
		const u8 *back_start = (u8 *) (minor->backing_buffer
						+ byte_offset);
		*ident_ptr += udlpim_trim_hline(back_start, &next_pixel,
				&byte_width);

		offset = next_pixel - line_start;
		line_end = next_pixel + byte_width;

		if (minor->bpp == 32) {
			dev_addr16 += offset / 2;
			dev_addr8 += offset / 4;
		} else if (minor->bpp == 16) {
			dev_addr16 += offset;
		}

		back_start += offset;
		line_start += offset;
		memcpy((char *)back_start, (char *) line_start, byte_width);
	}

	/*
	 * Separate the RGB components of a line of pixels.
	 */
	pixel16 = (uint16_t *) minor->hline_16;
	pixel8 = (uint8_t *) minor->hline_8;

	for (pixel32 = (uint32_t *) next_pixel;
			pixel32 < (uint32_t *) line_end; pixel32++) {
		udlpim_split_pixel_argb32(pixel32, pixel16, pixel8);
		pixel16++;
		pixel8++;
	}

	next_pixel16 = minor->hline_16;
	next_pixel8 = minor->hline_8;
	line_end16 = (u8 *)(pixel16);
	line_end8 = (u8 *)(pixel8);

	if (!udlpim_true32bpp) {
		while (next_pixel16 < line_end16) {
			udlpim_compress_hline_16(
					(const uint16_t **) &next_pixel16,
					(const uint16_t *) line_end16,
					&dev_addr16,
					(u8 **) &cmd, (u8 *) cmd_end);

			if (cmd >= cmd_end) {
				int len = cmd - (u8 *) urb->transfer_buffer;
				if (udlpim_submit_urb(minor, urb, len))
					return 1; /* lost pixels is set */

				*sent_ptr += len;
				urb = udlpim_get_urb(minor);
				if (!urb)
					return 1; /* lost pixels is set */
				*urb_ptr = urb;
				cmd = urb->transfer_buffer;
				cmd_end = &cmd[urb->transfer_buffer_length];
			}
		}
	}

	else if (udlpim_true32bpp) {
		while (next_pixel16 < line_end16 || next_pixel8 < line_end8) {
			udlpim_compress_hline_16(
					(const uint16_t **) &next_pixel16,
					(const uint16_t *) line_end16,
					&dev_addr16,
					(u8 **) &cmd, (u8 *) cmd_end);

			udlpim_compress_hline_8(
					(const uint8_t **) &next_pixel8,
					(const uint8_t *) line_end8,
					&dev_addr8,
					(u8 **) &cmd, (u8 *) cmd_end);

			if (cmd >= cmd_end) {
				int len = cmd - (u8 *) urb->transfer_buffer;
				if (udlpim_submit_urb(minor, urb, len))
					return 1;

				*sent_ptr += len;
				urb = udlpim_get_urb(minor);
				if (!urb)
					return 1;
				*urb_ptr = urb;
				cmd = urb->transfer_buffer;
				cmd_end = &cmd[urb->transfer_buffer_length];
			}
		}
	}

	*urb_buf_ptr = cmd;

	return 0;
}

static int udlpim_blank_hw_fb(struct udlpim_minor *minor, unsigned color)
{
	u32 dev_addr16, dev_addr8;
	uint16_t *line_start16, *line_end16;
	uint8_t *line_start8, *line_end8;

	uint16_t *hline_16;
	uint8_t *hline_8;

	u32 blank_color32 = color;
	u16 blank_color16;
	u8 blank_color8;

	int i;
	int xres = minor->current_video_mode.xres;
	int yres = minor->current_video_mode.yres;

	struct urb *urb;
	u8 *cmd, *cmd_end;

	if (!atomic_read(&minor->usb_active))
		return 0;

	urb = udlpim_get_urb(minor);

	if (!urb)
		return 0;

	cmd = (u8 *) urb->transfer_buffer;
	cmd_end = (u8 *) urb->transfer_buffer + urb->transfer_buffer_length;

	hline_16 = vcrtcm_kmalloc(sizeof(uint16_t) * xres, GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);
	hline_8 = vcrtcm_kmalloc(sizeof(uint8_t) * xres, GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);

	udlpim_split_pixel_argb32(&blank_color32, &blank_color16, &blank_color8);

	for (i = 0; i < xres; i++) {
		hline_16[i] = blank_color16;
		hline_8[i] = blank_color8;
	}

	line_end16 = hline_16 + xres;
	line_end8 = hline_8 + xres;

	for (i = 0; i < yres; i++) {
		line_start16 = hline_16;
		line_start8 = hline_8;
		dev_addr16 = minor->base16 + xres * i * 2;
		dev_addr8 = minor->base8 + xres * i;

		while (line_start16 < line_end16 ||
				(udlpim_true32bpp && (line_start8 < line_end8))) {
			udlpim_compress_hline_16(
				(const uint16_t **) &line_start16,
				(const uint16_t *) line_end16,
				&dev_addr16,
				(u8 **) &cmd, (u8 *) cmd_end);

			if (udlpim_true32bpp)
				udlpim_compress_hline_8(
					(const uint8_t **) &line_start8,
					(const uint8_t *) line_end8,
					&dev_addr8,
					(u8 **) &cmd, (u8 *) cmd_end);

			if (cmd >= cmd_end) {
				int len = cmd - (u8 *) urb->transfer_buffer;
				if (udlpim_submit_urb(minor, urb, len))
					return 1;

				urb = udlpim_get_urb(minor);
				if (!urb)
					return 1;

				cmd = urb->transfer_buffer;
				cmd_end = &cmd[urb->transfer_buffer_length];
			}
		}
	}

	if (cmd > (u8 *) urb->transfer_buffer) {
		int len = cmd - (u8 *) urb->transfer_buffer;
		udlpim_submit_urb(minor, urb, len);
	} else {
		udlpim_urb_completion(urb);
	}

	vcrtcm_kfree(hline_16);
	vcrtcm_kfree(hline_8);

	return 0;
}

/*
 * This is necessary before we can communicate with the display controller.
 */
static int udlpim_select_std_channel(struct udlpim_minor *minor)
{
	int ret;
	u8 set_def_chn[] = {	   0x57, 0xCD, 0xDC, 0xA7,
				0x1C, 0x88, 0x5E, 0x15,
				0x60, 0xFE, 0xC6, 0x97,
				0x16, 0x3D, 0x47, 0xF2  };

	ret = usb_control_msg(minor->udev,
			usb_sndctrlpipe(minor->udev, 0),
			NR_USB_REQUEST_CHANNEL,
			(USB_DIR_OUT | USB_TYPE_VENDOR), 0, 0,
			set_def_chn, sizeof(set_def_chn),
			USB_CTRL_SET_TIMEOUT);
	return ret;
}

static int udlpim_parse_vendor_descriptor(struct udlpim_minor *minor,
					struct usb_device *usbdev)
{
	char *desc;
	char *buf;
	char *desc_end;

	u8 total_len = 0;

	buf = vcrtcm_kzalloc(MAX_VENDOR_DESCRIPTOR_SIZE, GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);
	if (!buf)
		return false;
	desc = buf;

	total_len = usb_get_descriptor(usbdev, 0x5f, /* vendor specific */
				    0, desc, MAX_VENDOR_DESCRIPTOR_SIZE);
	if (total_len > 5) {
		VCRTCM_INFO("vendor descriptor length:%x data:%02x %02x %02x %02x" \
			"%02x %02x %02x %02x %02x %02x %02x\n",
			total_len, desc[0],
			desc[1], desc[2], desc[3], desc[4], desc[5], desc[6],
			desc[7], desc[8], desc[9], desc[10]);

		if ((desc[0] != total_len) || /* descriptor length */
		    (desc[1] != 0x5f) ||   /* vendor descriptor type */
		    (desc[2] != 0x01) ||   /* version (2 bytes) */
		    (desc[3] != 0x00) ||
		    (desc[4] != total_len - 2)) /* length after type */
			goto unrecognized;

		desc_end = desc + total_len;
		desc += 5; /* the fixed header we've already parsed */

		while (desc < desc_end) {
			u8 length;
			u16 key;

			key = *((u16 *) desc);
			desc += sizeof(u16);
			length = *desc;
			desc++;

			switch (key) {
			case 0x0200: { /* max_area */
				u32 max_area;
				max_area = le32_to_cpu(*((u32 *)desc));
				VCRTCM_WARNING("DL chip limited to %d pixel modes\n",
					max_area);
				minor->sku_pixel_limit = max_area;
				break;
			}
			default:
				break;
			}
			desc += length;
		}
	}

	goto success;

unrecognized:
	/* allow driver to load for now even if firmware unrecognized */
	VCRTCM_ERROR("Unrecognized vendor firmware descriptor\n");

success:
	vcrtcm_kfree(buf);
	return true;
}

/* Get the EDID from the USB device */

static int udlpim_get_edid(struct udlpim_minor *minor,
				char *edid, int len)
{
	int i;
	int ret;
	char *rbuf;

	rbuf = vcrtcm_kmalloc(2, GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);
	if (!rbuf)
		return 0;

	for (i = 0; i < len; i++) {
		ret = usb_control_msg(minor->udev,
				    usb_rcvctrlpipe(minor->udev, 0), (0x02),
				    (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
				    HZ);
		if (ret < 1) {
			VCRTCM_ERROR("Read EDID byte %d failed err %x\n", i, ret);
			i--;
			break;
		}
		edid[i] = rbuf[1];
	}

	vcrtcm_kfree(rbuf);

	return i;
}

/*
 * Check whether a video mode is supported by the DisplayLink chip
 * We start from monitor's modes, so don't need to filter that here
 */
static int udlpim_is_valid_mode(struct udlpim_minor *minor,
				int xres, int yres)
{
	if (xres * yres > minor->sku_pixel_limit) {
		VCRTCM_WARNING("%dx%d beyond chip capabilities\n",
		       xres, yres);
		return 0;
	}

	UDLPIM_DEBUG("%dx%d valid mode\n", xres, yres);

	return 1;
}

static int udlpim_alloc_scratch_memory(struct udlpim_minor *minor,
					int line_bytes, int num_lines)
{
	int result;

	struct udlpim_scratch_memory_descriptor *scratch_memory;

	struct page **bb_pages;
	struct page **hline_16_pages;
	struct page **hline_8_pages;

	unsigned int bb_num_pages;
	unsigned int hline_16_num_pages;
	unsigned int hline_8_num_pages;

	/* allocate the backing buffer */
	bb_num_pages = line_bytes * num_lines / PAGE_SIZE;

	if (line_bytes * num_lines % PAGE_SIZE > 0)
		bb_num_pages++;

	bb_pages = vcrtcm_kmalloc(sizeof(struct page *) * bb_num_pages,
				GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);

	if (!bb_pages)
		goto bb_err;

	result = vcrtcm_alloc_multiple_pages(GFP_KERNEL, bb_pages,
			bb_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);

	if (result > 0)
		goto bb_err;


	/* allocate the hline_16 buffer */
	hline_16_num_pages = (line_bytes / 2) / PAGE_SIZE;

	if ((line_bytes / 2) % PAGE_SIZE > 0)
		hline_16_num_pages++;

	hline_16_pages =
		vcrtcm_kmalloc(sizeof(struct page *) * hline_16_num_pages,
			GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);

	if (!hline_16_pages)
		goto hline_16_err;

	result = vcrtcm_alloc_multiple_pages(GFP_KERNEL, hline_16_pages,
			hline_16_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);

	if (result > 0)
		goto hline_16_err;

	/* allocate the hline_8 buffer */
	hline_8_num_pages = (line_bytes / 4) / PAGE_SIZE;

	if ((line_bytes / 4) % PAGE_SIZE > 0)
		hline_8_num_pages++;

	hline_8_pages =
		vcrtcm_kmalloc(sizeof(struct page *) * hline_8_num_pages,
				GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);

	if (!hline_8_pages)
		goto hline_8_err;

	result = vcrtcm_alloc_multiple_pages(GFP_KERNEL, hline_8_pages,
			hline_8_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);

	if (result > 0)
		goto hline_8_err;


	goto success;


	/* If any error conditions are triggered, we only need to clean */
	/* up whatever was allocated successfully before. */
	/* The allocation that failed will be cleaned up inside the */
	/* allocator itself. */

hline_8_err:
	vcrtcm_kfree(hline_8_pages);
	vcrtcm_free_multiple_pages(hline_16_pages, hline_16_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);
	VCRTCM_ERROR("Error during hline_8 scratch memory allocation\n");
hline_16_err:
	vcrtcm_kfree(hline_16_pages);
	vcrtcm_free_multiple_pages(bb_pages, bb_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);
	VCRTCM_ERROR("Error during hline_16 scratch memory allocation\n");
bb_err:
	vcrtcm_kfree(bb_pages);
	VCRTCM_ERROR("Error during backing buffer scratch memory allocation\n");
	return -ENOMEM;
success:

	scratch_memory =
		vcrtcm_kzalloc(sizeof(struct udlpim_scratch_memory_descriptor),
			GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);

	scratch_memory->backing_buffer_pages = bb_pages;
	scratch_memory->backing_buffer_num_pages = bb_num_pages;
	scratch_memory->hline_16_pages = hline_16_pages;
	scratch_memory->hline_16_num_pages = hline_16_num_pages;
	scratch_memory->hline_8_pages = hline_8_pages;
	scratch_memory->hline_8_num_pages = hline_8_num_pages;

	minor->scratch_memory = scratch_memory;

	UDLPIM_DEBUG("Allocated backing buffer: %u pages\n", bb_num_pages);
	UDLPIM_DEBUG("Allocated hline_16 buffer: %u pages\n", hline_16_num_pages);
	UDLPIM_DEBUG("Allocated hline_8 buffer: %u pages\n", hline_8_num_pages);

	return 0;
}

static void udlpim_free_scratch_memory(struct udlpim_minor *minor)
{
	struct udlpim_scratch_memory_descriptor *scratch_memory =
			minor->scratch_memory;

	if (!scratch_memory)
		return;

	if (scratch_memory->backing_buffer_pages) {
		vcrtcm_free_multiple_pages(scratch_memory->backing_buffer_pages,
				scratch_memory->backing_buffer_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);
		vcrtcm_kfree(scratch_memory->backing_buffer_pages);
	}

	if (scratch_memory->hline_16_pages) {
		vcrtcm_free_multiple_pages(scratch_memory->hline_16_pages,
				scratch_memory->hline_16_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);
		vcrtcm_kfree(scratch_memory->hline_16_pages);
	}
	if (scratch_memory->hline_8_pages) {
		vcrtcm_free_multiple_pages(scratch_memory->hline_8_pages,
				scratch_memory->hline_8_num_pages, VCRTCM_OWNER_PIM | udlpim_pimid);
		vcrtcm_kfree(scratch_memory->hline_8_pages);
	}

	vcrtcm_kfree(scratch_memory);

	return;
}

static int udlpim_map_scratch_memory(struct udlpim_minor *minor)
{
	struct udlpim_scratch_memory_descriptor *scratch_memory =
			minor->scratch_memory;
	uint32_t blank_pixel_32 = UDLPIM_BLANK_COLOR;
	uint16_t blank_pixel_16;
	uint8_t blank_pixel_8;

	if (!scratch_memory)
		return 1;

	udlpim_split_pixel_argb32(&blank_pixel_32, &blank_pixel_16, &blank_pixel_8);
	minor->backing_buffer = vm_map_ram(
					scratch_memory->backing_buffer_pages,
					scratch_memory->backing_buffer_num_pages,
					0, PAGE_KERNEL);
	if (!minor->backing_buffer)
		goto bb_err;
	memset(minor->backing_buffer, blank_pixel_32,
			scratch_memory->backing_buffer_num_pages * PAGE_SIZE);
	UDLPIM_DEBUG("Mapped backing buffer pages, starting at: %p\n",
				minor->backing_buffer);
	minor->hline_16 = vm_map_ram(scratch_memory->hline_16_pages,
					   scratch_memory->hline_16_num_pages,
					   0, PAGE_KERNEL);
	if (!minor->hline_16)
		goto hline_16_err;
	memset(minor->hline_16, blank_pixel_16,
			scratch_memory->hline_16_num_pages * PAGE_SIZE);
	UDLPIM_DEBUG("Mapped hline_16 pages, starting at: %p\n",
				minor->hline_16);

	minor->hline_8 = vm_map_ram(scratch_memory->hline_8_pages,
					  scratch_memory->hline_8_num_pages,
					  0, PAGE_KERNEL);
	if (!minor->hline_8)
		goto hline_8_err;
	memset(minor->hline_8, blank_pixel_8,
			scratch_memory->hline_8_num_pages * PAGE_SIZE);
	UDLPIM_DEBUG("Mapped hline_8 pages, starting at: %p\n",
				minor->hline_8);
	return 0;

hline_8_err:
	vm_unmap_ram(minor->hline_16,
			scratch_memory->hline_16_num_pages);
	minor->hline_16 = NULL;
hline_16_err:
	vm_unmap_ram(minor->backing_buffer,
			scratch_memory->backing_buffer_num_pages);
	minor->backing_buffer = NULL;
bb_err:
	return -ENOMEM;
}

static void udlpim_unmap_scratch_memory(struct udlpim_minor *minor)
{
	struct udlpim_scratch_memory_descriptor *scratch_memory =
			minor->scratch_memory;

	if (!scratch_memory)
		return;

	if (minor->backing_buffer) {
		vm_unmap_ram(minor->backing_buffer,
				scratch_memory->backing_buffer_num_pages);
		minor->backing_buffer = NULL;
	}
	if (minor->hline_16) {
		vm_unmap_ram(minor->hline_16,
				scratch_memory->hline_16_num_pages);
		minor->hline_16 = NULL;
	}
	if (minor->hline_8) {
		vm_unmap_ram(minor->hline_8,
				scratch_memory->hline_8_num_pages);
		minor->hline_8 = NULL;
	}
}

/*
 * This generates the appropriate command sequence that then drives the
 * display controller to set the video mode.
 */
static int udlpim_set_video_mode(struct udlpim_minor *minor,
				struct udlpim_video_mode *mode)
{
	char *buf;
	char *wrptr;
	int retval = 0;
	int writesize;
	struct urb *urb;

	if (!atomic_read(&minor->usb_active))
		return -EPERM;

	urb = udlpim_get_urb(minor);
	if (!urb)
		return -ENOMEM;

	VCRTCM_INFO("Setting video mode to %dx%d.\n", mode->xres, mode->yres);

	buf = (char *) urb->transfer_buffer;

	/*
	* This first section has to do with setting the base address on the
	* controller * associated with the display. There are 2 base
	* pointers, currently, we only * use the 16 bpp segment.
	*/
	wrptr = udlpim_vidreg_lock(buf);

	if (!udlpim_true32bpp)
		wrptr = udlpim_set_color_depth(wrptr, 0x00);
	else if (udlpim_true32bpp)
		wrptr = udlpim_set_color_depth(wrptr, 0x01);

	/* set base for 16bpp segment to 0 */
	wrptr = udlpim_set_base16bpp(wrptr, 0);
	minor->base16 = 0;
	/* set base for 8bpp segment to end of fb */
	wrptr = udlpim_set_base8bpp(wrptr, mode->xres * mode->yres * 2);
	minor->base8 = mode->xres * mode->yres * 2;

	wrptr = udlpim_set_vid_cmds(wrptr, mode);
	wrptr = udlpim_enable_hvsync(wrptr, true);
	wrptr = udlpim_vidreg_unlock(wrptr);

	writesize = wrptr - buf;

	retval = udlpim_submit_urb(minor, urb, writesize);

	if (retval) {
		VCRTCM_ERROR("Error setting hardware mode.\n");
		return 1;
	}

	memcpy(&minor->current_video_mode,
			mode, sizeof(struct udlpim_video_mode));

	return retval;
}

/* Delayed work which runs periodically to query the monitor's EDID. */
static void udlpim_query_edid(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct udlpim_minor *minor =
		container_of(delayed_work, struct udlpim_minor, query_edid_work);

	udlpim_query_edid_core(minor);

	queue_delayed_work(minor->workqueue, &minor->query_edid_work,
		UDLPIM_EDID_QUERY_TIME);
}

/* The following are all low-level DisplayLink manipulation functions */

/*
 * All DisplayLink bulk operations start with 0xAF, followed by specific code
 * All operations are written to buffers which then later get sent to device
 */
static char *udlpim_set_register(char *buf, u8 reg, u8 val)
{
	*buf++ = 0xAF;
	*buf++ = 0x20;
	*buf++ = reg;
	*buf++ = val;
	return buf;
}

static char *udlpim_vidreg_lock(char *buf)
{
	return udlpim_set_register(buf, 0xFF, 0x00);
}

static char *udlpim_vidreg_unlock(char *buf)
{
	return udlpim_set_register(buf, 0xFF, 0xFF);
}

/*
 * On/Off for driving the DisplayLink framebuffer to the display
 *  0x00 H and V sync on
 *  0x01 H and V sync off (screen blank but powered)
 *  0x07 DPMS powerdown (requires modeset to come back)
 */
static char *udlpim_enable_hvsync(char *buf, bool enable)
{
	if (enable)
		return udlpim_set_register(buf, 0x1F, 0x00);
	else
		return udlpim_set_register(buf, 0x1F, 0x07);
}

static char *udlpim_set_color_depth(char *buf, u8 selection)
{
	return udlpim_set_register(buf, 0x00, selection);
}

static char *udlpim_set_base16bpp(char *wrptr, u32 base)
{
	/* the base pointer is 16 bits wide, 0x20 is hi byte. */
	wrptr = udlpim_set_register(wrptr, 0x20, base >> 16);
	wrptr = udlpim_set_register(wrptr, 0x21, base >> 8);
	return udlpim_set_register(wrptr, 0x22, base);
}

/*
 * DisplayLink HW has separate 16bpp and 8bpp framebuffers.
 * In 24bpp modes, the low 323 RGB bits go in the 8bpp framebuffer
 */
static char *udlpim_set_base8bpp(char *wrptr, u32 base)
{
	wrptr = udlpim_set_register(wrptr, 0x26, base >> 16);
	wrptr = udlpim_set_register(wrptr, 0x27, base >> 8);
	return udlpim_set_register(wrptr, 0x28, base);
}

static char *udlpim_set_register_16(char *wrptr, u8 reg, u16 value)
{
	wrptr = udlpim_set_register(wrptr, reg, value >> 8);
	return udlpim_set_register(wrptr, reg+1, value);
}

/*
 * This is kind of weird because the controller takes some
 * register values in a different byte order than other registers.
 */
static char *udlpim_set_register_16be(char *wrptr, u8 reg, u16 value)
{
	wrptr = udlpim_set_register(wrptr, reg, value);
	return udlpim_set_register(wrptr, reg+1, value >> 8);
}

/*
 * LFSR is linear feedback shift register. The reason we have this is
 * because the display controller needs to minimize the clock depth of
 * various counters used in the display path. So this code reverses the
 * provided value into the lfsr16 value by counting backwards to get
 * the value that needs to be set in the hardware comparator to get the
 * same actual count. This makes sense once you read above a couple of
 * times and think about it from a hardware perspective.
 */
static u16 udlpim_lfsr16(u16 actual_count)
{
	u32 lv = 0xFFFF; /* This is the lfsr value that the hw starts with */

	while (actual_count--) {
		lv =	 ((lv << 1) |
			(((lv >> 15) ^ (lv >> 4) ^ (lv >> 2) ^ (lv >> 1)) & 1))
			& 0xFFFF;
	}

	return (u16) lv;
}

/*
 * This does LFSR conversion on the value that is to be written.
 * See LFSR explanation above for more detail.
 */
static char *udlpim_set_register_lfsr16(char *wrptr, u8 reg, u16 value)
{
	return udlpim_set_register_16(wrptr, reg, udlpim_lfsr16(value));
}

/*
 * This takes a standard fbdev screeninfo struct and all of its monitor mode
 * details and converts them into the DisplayLink equivalent register commands.
 */
static char *udlpim_set_vid_cmds(char *wrptr, struct udlpim_video_mode *mode)
{
	u16 xds, yds;
	u16 xde, yde;
	u16 yec;

	/* x display start */
	xds = mode->left_margin + mode->hsync_len;
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x01, xds);
	/* x display end */
	xde = xds + mode->xres;
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x03, xde);

	/* y display start */
	yds = mode->upper_margin + mode->vsync_len;
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x05, yds);
	/* y display end */
	yde = yds + mode->yres;
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x07, yde);

	/* x end count is active + blanking - 1 */
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x09,
			xde + mode->right_margin - 1);

	/* libdlo hardcodes hsync start to 1 */
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x0B, 1);

	/* hsync end is width of sync pulse + 1 */
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x0D, mode->hsync_len + 1);

	/* hpixels is active pixels */
	wrptr = udlpim_set_register_16(wrptr, 0x0F, mode->xres);

	/* yendcount is vertical active + vertical blanking */
	yec = mode->yres + mode->upper_margin + mode->lower_margin +
			mode->vsync_len;
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x11, yec);

	/* libdlo hardcodes vsync start to 0 */
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x13, 0);

	/* vsync end is width of vsync pulse */
	wrptr = udlpim_set_register_lfsr16(wrptr, 0x15, mode->vsync_len);

	/* vpixels is active pixels */
	wrptr = udlpim_set_register_16(wrptr, 0x17, mode->yres);

	/* convert picoseconds to 5kHz multiple for pclk5k = x * 1E12/5k */
	wrptr = udlpim_set_register_16be(wrptr, 0x1B,
			200*1000*1000/mode->pixclock);

	return wrptr;
}

/* The following are all low-level USB functions */

static void udlpim_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct udlpim_minor *minor = (struct udlpim_minor *)unode->dev;
	unsigned long flags;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)) {
			VCRTCM_ERROR("%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);
			atomic_set(&minor->lost_pixels, 1);
		}
	}

	urb->transfer_buffer_length = minor->urbs.size; /* reset to actual */

	spin_lock_irqsave(&minor->urbs.lock, flags);
	list_add_tail(&unode->entry, &minor->urbs.list);
	minor->urbs.available++;
	spin_unlock_irqrestore(&minor->urbs.lock, flags);

	/*
	 * When using fb_defio, we deadlock if up() is called
	 * while another is waiting. So queue to another process.
	 */
	/*
	* if (fb_defio)
	*	schedule_delayed_work(&unode->release_urb_work, 0);
	* else
	*/
	up(&minor->urbs.limit_sem);
}

static int udlpim_alloc_urb_list(struct udlpim_minor *minor,
	int count, size_t size)
{
	int i = 0;
	struct urb *urb;
	struct urb_node *unode;
	char *buf;

	spin_lock_init(&minor->urbs.lock);

	minor->urbs.size = size;
	INIT_LIST_HEAD(&minor->urbs.list);

	while (i < count) {
		unode = vcrtcm_kzalloc(sizeof(struct urb_node), GFP_KERNEL, VCRTCM_OWNER_PIM | udlpim_pimid);
		if (!unode)
			break;
		unode->dev = minor;

		INIT_DELAYED_WORK(&unode->release_urb_work,
			  udlpim_release_urb_work);

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			vcrtcm_kfree(unode);
			break;
		}
		unode->urb = urb;

		buf = usb_alloc_coherent(minor->udev, MAX_TRANSFER, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!buf) {
			vcrtcm_kfree(unode);
			usb_free_urb(urb);
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, minor->udev,
			usb_sndbulkpipe(minor->udev, 1),
			buf, size, udlpim_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &minor->urbs.list);

		i++;
	}

	sema_init(&minor->urbs.limit_sem, i);
	minor->urbs.count = i;
	minor->urbs.available = i;

	VCRTCM_INFO("allocated %d %d byte urbs\n", i, (int) size);

	return i;
}

static struct urb *udlpim_get_urb(struct udlpim_minor *minor)
{
	int ret = 0;
	struct list_head *entry;
	struct urb_node *unode;
	struct urb *urb = NULL;
	unsigned long flags;

	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&minor->urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		atomic_set(&minor->lost_pixels, 1);
		VCRTCM_WARNING("wait for urb interrupted: %x available: %d\n",
		       ret, minor->urbs.available);
		goto error;
	}

	spin_lock_irqsave(&minor->urbs.lock, flags);

	BUG_ON(list_empty(&minor->urbs.list)); /* reserved one with limit_sem */
	entry = minor->urbs.list.next;
	list_del_init(entry);
	minor->urbs.available--;

	spin_unlock_irqrestore(&minor->urbs.lock, flags);

	unode = list_entry(entry, struct urb_node, entry);
	urb = unode->urb;

error:
	return urb;
}

static int udlpim_submit_urb(struct udlpim_minor *minor,
	struct urb *urb, size_t len)
{
	int ret;

	BUG_ON(len > minor->urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		udlpim_urb_completion(urb); /* because no one else will */
		atomic_set(&minor->lost_pixels, 1);
		VCRTCM_ERROR("usb_submit_urb error %x\n", ret);
	}
	return ret;
}

static void udlpim_release_urb_work(struct work_struct *work)
{
	struct urb_node *unode = container_of(work, struct urb_node,
					      release_urb_work.work);

	up(&unode->dev->urbs.limit_sem);
}
