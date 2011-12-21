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

#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/prefetch.h>
#include <vcrtcm/vcrtcm_ctd.h>
#include <edid.h>

#include "udlctd.h"
#include "udlctd_vcrtcm.h"
#include "udlctd_usb.h"
#include "udlctd_utils.h"



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

struct usb_driver udlctd_driver = {
	.name = "udlctd",
	.probe = udlctd_usb_probe,
	.disconnect = udlctd_usb_disconnect,
	.id_table = id_table,
};

/******************************************************************************
 * USB/device setup
 * These functions setup the driver for each attached device
 *****************************************************************************/

/* USB probe
 * This gets called when the a device is first identified
 * or connected. Sets up the driver structures, and notifies
 * VCRTCM that the device is available.
 */
static int udlctd_usb_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	struct udlctd_info *udlctd_info;

	int retval = -ENOMEM;
	unsigned long flags;

	/* usb initialization */
	usbdev = interface_to_usbdev(interface);

	udlctd_info = kzalloc(sizeof(struct udlctd_info), GFP_KERNEL);

	if (udlctd_info == NULL) {
		PR_ERR("udlctd_usb_probe: failed alloc of udlctd_info\n");
		goto error;
	}

	/* we need to wait for both usb and vcrtcm to finish on disconnect */
	kref_init(&udlctd_info->kref); /* matching kref_put in udb .disconnect fn */
	/*kref_get(&udlctd_info->kref); */ /* matching kref_put in vcrtcm detach */

	udlctd_info->udev = usbdev;
	udlctd_info->gdev = &usbdev->dev;  /* generic struct device */
	usb_set_intfdata(interface, udlctd_info);

	PR_INFO("%s %s - serial #%s\n",
		usbdev->manufacturer, usbdev->product, usbdev->serial);
	PR_INFO("vid_%04x&pid_%04x&rev_%04x driver's udlctd_info struct at %p\n",
		usbdev->descriptor.idVendor, usbdev->descriptor.idProduct,
		usbdev->descriptor.bcdDevice, udlctd_info);

	udlctd_info->sku_pixel_limit = 2048 * 1152;  /* default to maximum */

	if (!udlctd_parse_vendor_descriptor(udlctd_info, usbdev)) {
		PR_ERR("Firmware not recognized. Assume incompatible device.\n");
		goto error;
	}

	if (!udlctd_alloc_urb_list(udlctd_info, WRITES_IN_FLIGHT, MAX_TRANSFER)) {
		retval = -ENOMEM;
		PR_ERR("udlctd_alloc_urb_list failed\n");
		goto error;
	}

	/* TODO: Investigate USB class business */

	/* Do non-USB/VCRTCM driver setup */
	INIT_LIST_HEAD(&udlctd_info->list);

	/* Assign a minor number */
	if (udlctd_num_minors <= udlctd_max_minor) {
		/* This case occurs if one or more minor that is less than */
		/* the maximum minor currently assigned becomes available. */
		/* In this case we want to reuse the lower number(s). */
		/* Note that this implementation will only work as long as */
		/* the total number of minors is less than 64. For this */
		/* reason we set UDLCTD_MAX_DEVICES to 64 in udlctd.h */
		int new_minor = -1;
		uint64_t used_minors = 0;
		struct udlctd_info *ptr;
		int i;

		list_for_each_entry(ptr, &udlctd_info_list, list) {
			used_minors |= (1 << ptr->minor);
		}

		for (i = 0; i < udlctd_max_minor; i++) {
			if (!(used_minors & (1 << i))) {
				new_minor = i;
				break;
			}
		}
		BUG_ON(new_minor < 0);
		udlctd_info->minor = new_minor;
		udlctd_num_minors++;

	} else {
		/* This handles the trivial case where there are no earlier */
		/* minors that have gone away. */

		/* If we ready have too many minors, error out. */
		if (udlctd_num_minors == UDLCTD_MAX_DEVICES) {
			PR_ERR("Maximum number of minors already assigned. "
				"Device will be unusable.\n");
			goto error;
		}

		udlctd_info->minor = udlctd_num_minors++;
		udlctd_max_minor++;
	}

	mutex_init(&udlctd_info->buffer_mutex);
	spin_lock_init(&udlctd_info->udlctd_lock);

	init_waitqueue_head(&udlctd_info->xmit_sync_queue);
	udlctd_info->enabled_queue = 1;

	udlctd_info->workqueue = create_workqueue("udlctd_workers");

	udlctd_info->udlctd_vcrtcm_hal_descriptor = NULL;
	udlctd_info->scratch_memory = NULL;

	INIT_DELAYED_WORK(&udlctd_info->fake_vblank_work, udlctd_fake_vblank);

	spin_lock_irqsave(&udlctd_info->udlctd_lock, flags);
	udlctd_info->status = 0;
	spin_unlock_irqrestore(&udlctd_info->udlctd_lock, flags);

	INIT_LIST_HEAD(&udlctd_info->fb_mode_list);

	retval = udlctd_setup_modes(udlctd_info);

	if (retval != 0) {
		PR_ERR("Unable to find common mode for display and adapter.\n");
		goto error;
	}

	atomic_set(&udlctd_info->usb_active, 1);
	udlctd_select_std_channel(udlctd_info);
	udlctd_set_video_mode(udlctd_info, &udlctd_info->default_video_mode);
	udlctd_blank_hw_fb(udlctd_info, UDLCTD_BLANK_COLOR);

	PR_INFO("DisplayLink USB device attached.\n");
	PR_INFO("successfully registered"
		" minor %d\n", udlctd_info->minor);


	PR_DEBUG("Calling vcrtcm_hw_add for udlctd %p major %d minor %d\n",
		udlctd_info, udlctd_major, udlctd_info->minor);
	if (vcrtcm_hw_add(&udlctd_vcrtcm_funcs, &udlctd_vcrtcm_hw_props,
			  udlctd_major, udlctd_info->minor, 0, udlctd_info))
		PR_WARN("vcrtcm_hw_add failed, udlctd major %d, minor %d,"
			" won't work\n", udlctd_major, udlctd_info->minor);

	list_add(&udlctd_info->list, &udlctd_info_list);

	return 0;

error:
	if (udlctd_info) {
		PR_ERR("Got to error in probe");
		/* Ref for framebuffer */
		kref_put(&udlctd_info->kref, udlctd_free);
		/* vcrtcm reference */
		/* kref_put(&udlctd_info->kref, udlctd_free); */
	}

	return retval;
}

/* USB disconnect */
/* This gets called on driver unload or device disconnection */
static void udlctd_usb_disconnect(struct usb_interface *interface)
{
	struct udlctd_info *udlctd_info;

	udlctd_info = usb_get_intfdata(interface);

	PR_DEBUG("USB disconnect starting\n");

	/* TODO: Do we need this? Maybe we can just detach and be done */
	/* we virtualize until everyone is done with it, then we free */
	udlctd_info->virtualized = true;

	/* When non-active we'll update virtual framebuffer, but no new urbs */
	atomic_set(&udlctd_info->usb_active, 0);

	usb_set_intfdata(interface, NULL);

	/* unregister with VCRTCM */
	PR_DEBUG("Calling vcrtcm_hw_del for "
		"udlctd %p, major %d, minor %d\n",
		udlctd_info, udlctd_major, udlctd_info->minor);

	cancel_delayed_work_sync(&udlctd_info->fake_vblank_work);
	vcrtcm_hw_del(udlctd_major, udlctd_info->minor, 0);

	/* release reference taken by kref_init in probe() */
	/* TODO: Deal with reference count stuff. Perhaps have reference count
	until udlctd_vcrtcm_detach completes */

	kref_put(&udlctd_info->kref, udlctd_free); /* last ref from kref_init */
	/* kref_put(&udlctd_info->kref, udlctd_free);*/ /* Ref for framebuffer */
}

/* This function frees the information for an individual device */
void udlctd_free(struct kref *kref)
{
	struct udlctd_info *udlctd_info =
		container_of(kref, struct udlctd_info, kref);
	struct udlctd_video_mode *udlctd_video_mode, *tmp;

	cancel_delayed_work_sync(&udlctd_info->fake_vblank_work);

	/* this function will wait for all in-flight urbs to complete */
	if (udlctd_info->urbs.count > 0)
		udlctd_free_urb_list(udlctd_info);

	PR_DEBUG("freeing edid: %p\n", udlctd_info->edid);
	udlctd_kfree(udlctd_info, udlctd_info->edid);

	PR_DEBUG("freeing mode list");
	list_for_each_entry_safe(udlctd_video_mode,
				tmp, &udlctd_info->fb_mode_list, list) {
		list_del(&udlctd_video_mode->list);
		udlctd_kfree(udlctd_info, udlctd_video_mode);
	}

	udlctd_unmap_scratch_memory(udlctd_info);
	udlctd_free_scratch_memory(udlctd_info);

	PR_WARN("freeing udlctd_info data %p\n", udlctd_info);
	PR_DEBUG("page_track : %d\n", udlctd_info->page_track);
	PR_DEBUG("kmalloc_track: %d\n", udlctd_info->kmalloc_track);
	PR_DEBUG("vmalloc_track: %d\n", udlctd_info->vmalloc_track);

	list_del(&udlctd_info->list);

	if (udlctd_max_minor == udlctd_info->minor)
		udlctd_max_minor--;

	udlctd_num_minors--;

	kfree(udlctd_info);
}

/******************************************************************************
 * These functions are called from outside
 * this file, by the VCRTCM implementation.
 *****************************************************************************/

 /* Sets the device mode and allocates framebuffers */
int udlctd_setup_screen(struct udlctd_info *udlctd_info,
	struct udlctd_video_mode *mode, struct vcrtcm_fb *vcrtcm_fb)
{
	int result;

	udlctd_unmap_scratch_memory(udlctd_info);
	udlctd_free_scratch_memory(udlctd_info);

	result = udlctd_alloc_scratch_memory(udlctd_info,
			vcrtcm_fb->pitch, vcrtcm_fb->vdisplay);

	if (result) {
		PR_ERR("Could not alloc scratch memory.\n");
		return 1;
	}

	result = udlctd_map_scratch_memory(udlctd_info);

	if (result) {
		PR_ERR("Could not map scratch memory.\n");
		return 1;
	}

	result = udlctd_set_video_mode(udlctd_info, mode);

	if (result) {
		PR_ERR("Could not set screen mode\n");
		return 1;
	}

	result = udlctd_blank_hw_fb(udlctd_info, UDLCTD_BLANK_COLOR);

	if (result) {
		PR_ERR("Could not blank HW framebuffer\n");
		return 1;
	}

	PR_DEBUG("Done with setup_screen\n");
	return 0;
}

int udlctd_error_screen(struct udlctd_info *udlctd_info)
{
	udlctd_set_video_mode(udlctd_info, &udlctd_info->default_video_mode);
	udlctd_blank_hw_fb(udlctd_info, UDLCTD_ERROR_COLOR);

	return 0;
}

/*
 * Generates the appropriate command sequences that
 * tells the video controller to put the monitor to sleep.
 */
int udlctd_dpms_sleep(struct udlctd_info *udlctd_info)
{
	pr_info("udlctd_dpms_sleep not implemented\n");
	return 0;
}

/* Resets the mode to wake up the display */
int udlctd_dpms_wakeup(struct udlctd_info *udlctd_info)
{
	pr_info("udlctd_dpms_wakeup not implemented\n");
	return 0;
}

/* Transmits the framebuffer over USB to the monitor */
int udlctd_transmit_framebuffer(struct udlctd_info *udlctd_info)
{
	int i, ret;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	struct urb *urb;
	int xres = udlctd_info->current_video_mode->xres;
	int yres = udlctd_info->current_video_mode->yres;
	int bytes_per_pixel = udlctd_info->bpp / 8;
	struct vcrtcm_fb *vcrtcm_fb =
			&udlctd_info->udlctd_vcrtcm_hal_descriptor->vcrtcm_fb;

	start_cycles = get_cycles();

	if (!atomic_read(&udlctd_info->usb_active))
		return 0;

	urb = udlctd_get_urb(udlctd_info);
	if (!urb)
		return 0;

	cmd = urb->transfer_buffer;

	for (i = 0; i < yres; i++) {
		const int byte_offset =
			vcrtcm_fb->pitch * i +
			vcrtcm_fb->viewport_x * (vcrtcm_fb->bpp >> 3);

		if (udlctd_render_hline(udlctd_info, &urb,
				(char *) udlctd_info->main_buffer,
				&cmd, byte_offset, xres * bytes_per_pixel,
				&bytes_identical, &bytes_sent))
			goto error;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len = cmd - (char *) urb->transfer_buffer;
		ret = udlctd_submit_urb(udlctd_info, urb, len);
		bytes_sent += len;
	} else
		udlctd_urb_completion(urb);
error:
	atomic_add(bytes_sent, &udlctd_info->bytes_sent);
	atomic_add(bytes_identical, &udlctd_info->bytes_identical);
	atomic_add(xres*yres*2, &udlctd_info->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles) >> 10)),
			&udlctd_info->cpu_kcycles_used);

	return 0;
}

/******************************************************************************
 * ATTENTION:
 * Everything past here is static and only accessed from inside udlctd_usb.c
 *****************************************************************************/

/*
 * Trims identical data from front and back of line
 * Sets new front buffer address and width
 * And returns byte count of identical pixels
 * Assumes CPU natural alignment (unsigned long)
 * for back and front buffer ptrs and width
 */

static int udlctd_trim_hline(const u8 *bback, const u8 **bfront, int *width_bytes)
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

static void udlctd_compress_hline_16(
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

			/* PR_DEBUG("pixel16: %x\n", *pixel); */
			*(uint16_t *)cmd = cpu_to_be16p(pixel);
			/* PR_DEBUG("cmd16: %x\n", *cmd); */
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
		PR_DEBUG("16 padded with %d\n", cmd_buffer_end-cmd);
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

static void udlctd_compress_hline_8(
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
		PR_DEBUG("8 padded with %d\n", cmd_buffer_end-cmd);
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

static int udlctd_render_hline(struct udlctd_info *udlctd_info, struct urb **urb_ptr,
				const char *front, char **urb_buf_ptr,
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

	struct udlctd_vcrtcm_hal_descriptor *uvhd =
			udlctd_info->udlctd_vcrtcm_hal_descriptor;
	struct vcrtcm_cursor *vcrtcm_cursor = &uvhd->vcrtcm_cursor;
	struct vcrtcm_fb *vcrtcm_fb = &uvhd->vcrtcm_fb;

	/* We need line_num for the dev_offset and to do the cursor overlay */
	int line_num = byte_offset / vcrtcm_fb->pitch;

	/* We need to recalculate the offset in the device framebuffer */
	/* because our byte_offset uses pitch */
	int dev_offset = line_num * udlctd_info->current_video_mode->xres * 4;

	/* These are offsets in the source (virtual) frame buffer */
	line_start = (u8 *) (front + byte_offset);
	next_pixel = line_start;
	line_end = next_pixel + byte_width;

	/* Calculate offsets in the device frame buffer */
	if (udlctd_info->bpp == 32) {
		dev_addr16 = udlctd_info->base16 + dev_offset / 2;
		dev_addr8 = udlctd_info->base8 + dev_offset / 4;
	} else if (udlctd_info->bpp == 16) {
		dev_addr16 = udlctd_info->base16 + dev_offset;
		dev_addr8 = 0;
	}

	/*
	 * Overlay the cursor
	 */
	if (udlctd_info->udlctd_vcrtcm_hal_descriptor && udlctd_info->cursor) {
		if (vcrtcm_cursor->flag != VCRTCM_CURSOR_FLAG_HIDE &&
		    line_num >= vcrtcm_cursor->location_y &&
		    line_num < vcrtcm_cursor->location_y + vcrtcm_cursor->height) {
			int i, clip = 0;
			uint32_t *hline_pixel = (uint32_t *)line_start;
			uint32_t *cursor_pixel = (uint32_t *)udlctd_info->cursor;
			cursor_pixel +=	(line_num-vcrtcm_cursor->location_y)*vcrtcm_cursor->width;
			if (vcrtcm_cursor->location_x >= 0)
				hline_pixel += vcrtcm_cursor->location_x;
			else
				clip = -vcrtcm_cursor->location_x;
			cursor_pixel += clip;
			for (i = 0; i < vcrtcm_cursor->width - clip; i++) {
				if (hline_pixel >= (uint32_t *) line_end)
					break;

				alpha_overlay_argb32(hline_pixel, cursor_pixel);
				cursor_pixel++;
				hline_pixel++;
			}
		}
	}

	/* Find out what part of the line has changed.
	 * We only transmit the changed part.
	 */

	if (udlctd_info->backing_buffer) {
		int offset;
		const u8 *back_start = (u8 *) (udlctd_info->backing_buffer
						+ byte_offset);
		*ident_ptr += udlctd_trim_hline(back_start, &next_pixel,
				&byte_width);

		offset = next_pixel - line_start;
		line_end = next_pixel + byte_width;

		if (udlctd_info->bpp == 32) {
			dev_addr16 += offset / 2;
			dev_addr8 += offset / 4;
		} else if (udlctd_info->bpp == 16) {
			dev_addr16 += offset;
		}

		back_start += offset;
		line_start += offset;
		memcpy((char *)back_start, (char *) line_start, byte_width);
	}

	/*
	 * Separate the RGB components of a line of pixels.
	 */
	pixel16 = (uint16_t *) udlctd_info->hline_16;
	pixel8 = (uint8_t *) udlctd_info->hline_8;

	for (pixel32 = (uint32_t *) next_pixel;
			pixel32 < (uint32_t *) line_end; pixel32++) {
		split_pixel_argb32(pixel32, pixel16, pixel8);
		pixel16++;
		pixel8++;
	}

	next_pixel16 = udlctd_info->hline_16;
	next_pixel8 = udlctd_info->hline_8;
	line_end16 = (u8 *)(pixel16);
	line_end8 = (u8 *)(pixel8);

	if (!true32bpp) {
		while (next_pixel16 < line_end16) {
			udlctd_compress_hline_16(
					(const uint16_t **) &next_pixel16,
					(const uint16_t *) line_end16,
					&dev_addr16,
					(u8 **) &cmd, (u8 *) cmd_end);

			if (cmd >= cmd_end) {
				int len = cmd - (u8 *) urb->transfer_buffer;
				if (udlctd_submit_urb(udlctd_info, urb, len))
					return 1; /* lost pixels is set */

				*sent_ptr += len;
				urb = udlctd_get_urb(udlctd_info);
				if (!urb)
					return 1; /* lost pixels is set */
				*urb_ptr = urb;
				cmd = urb->transfer_buffer;
				cmd_end = &cmd[urb->transfer_buffer_length];
			}
		}
	}

	else if (true32bpp) {
		while (next_pixel16 < line_end16 || next_pixel8 < line_end8) {
			udlctd_compress_hline_16(
					(const uint16_t **) &next_pixel16,
					(const uint16_t *) line_end16,
					&dev_addr16,
					(u8 **) &cmd, (u8 *) cmd_end);

			udlctd_compress_hline_8(
					(const uint8_t **) &next_pixel8,
					(const uint8_t *) line_end8,
					&dev_addr8,
					(u8 **) &cmd, (u8 *) cmd_end);

			if (cmd >= cmd_end) {
				int len = cmd - (u8 *) urb->transfer_buffer;
				if (udlctd_submit_urb(udlctd_info, urb, len))
					return 1;

				*sent_ptr += len;
				urb = udlctd_get_urb(udlctd_info);
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

static int udlctd_blank_hw_fb(struct udlctd_info *udlctd_info, unsigned color)
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
	int xres = udlctd_info->current_video_mode->xres;
	int yres = udlctd_info->current_video_mode->yres;

	struct urb *urb;
	u8 *cmd, *cmd_end;

	if (!atomic_read(&udlctd_info->usb_active))
		return 0;

	urb = udlctd_get_urb(udlctd_info);

	if (!urb)
		return 0;

	cmd = (u8 *) urb->transfer_buffer;
	cmd_end = (u8 *) urb->transfer_buffer + urb->transfer_buffer_length;

	hline_16 = udlctd_kmalloc(udlctd_info,
				sizeof(uint16_t) * xres, GFP_KERNEL);
	hline_8 = udlctd_kmalloc(udlctd_info,
				sizeof(uint8_t) * xres, GFP_KERNEL);

	split_pixel_argb32(&blank_color32, &blank_color16, &blank_color8);

	for (i = 0; i < xres; i++) {
		hline_16[i] = blank_color16;
		hline_8[i] = blank_color8;
	}

	line_end16 = hline_16 + xres;
	line_end8 = hline_8 + xres;

	for (i = 0; i < yres; i++) {
		line_start16 = hline_16;
		line_start8 = hline_8;
		dev_addr16 = udlctd_info->base16 + xres * i * 2;
		dev_addr8 = udlctd_info->base8 + xres * i;

		while (line_start16 < line_end16 ||
				(true32bpp && (line_start8 < line_end8))) {
			udlctd_compress_hline_16(
				(const uint16_t **) &line_start16,
				(const uint16_t *) line_end16,
				&dev_addr16,
				(u8 **) &cmd, (u8 *) cmd_end);

			if (true32bpp)
				udlctd_compress_hline_8(
					(const uint8_t **) &line_start8,
					(const uint8_t *) line_end8,
					&dev_addr8,
					(u8 **) &cmd, (u8 *) cmd_end);

			if (cmd >= cmd_end) {
				int len = cmd - (u8 *) urb->transfer_buffer;
				if (udlctd_submit_urb(udlctd_info, urb, len))
					return 1;

				urb = udlctd_get_urb(udlctd_info);
				if (!urb)
					return 1;

				cmd = urb->transfer_buffer;
				cmd_end = &cmd[urb->transfer_buffer_length];
			}
		}
	}

	if (cmd > (u8 *) urb->transfer_buffer) {
		int len = cmd - (u8 *) urb->transfer_buffer;
		udlctd_submit_urb(udlctd_info, urb, len);
	} else {
		udlctd_urb_completion(urb);
	}

	udlctd_kfree(udlctd_info, hline_16);
	udlctd_kfree(udlctd_info, hline_8);

	return 0;
}

/*
 * This is necessary before we can communicate with the display controller.
 */
static int udlctd_select_std_channel(struct udlctd_info *udlctd_info)
{
	int ret;
	u8 set_def_chn[] = {	   0x57, 0xCD, 0xDC, 0xA7,
				0x1C, 0x88, 0x5E, 0x15,
				0x60, 0xFE, 0xC6, 0x97,
				0x16, 0x3D, 0x47, 0xF2  };

	ret = usb_control_msg(udlctd_info->udev,
			usb_sndctrlpipe(udlctd_info->udev, 0),
			NR_USB_REQUEST_CHANNEL,
			(USB_DIR_OUT | USB_TYPE_VENDOR), 0, 0,
			set_def_chn, sizeof(set_def_chn),
			USB_CTRL_SET_TIMEOUT);
	return ret;
}

static int udlctd_parse_vendor_descriptor(struct udlctd_info *udlctd_info,
					struct usb_device *usbdev)
{
	char *desc;
	char *buf;
	char *desc_end;

	u8 total_len = 0;

	buf = udlctd_kzalloc(udlctd_info, MAX_VENDOR_DESCRIPTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return false;
	desc = buf;

	total_len = usb_get_descriptor(usbdev, 0x5f, /* vendor specific */
				    0, desc, MAX_VENDOR_DESCRIPTOR_SIZE);
	if (total_len > 5) {
		PR_INFO("vendor descriptor length:%x data:%02x %02x %02x %02x" \
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
				PR_WARN("DL chip limited to %d pixel modes\n",
					max_area);
				udlctd_info->sku_pixel_limit = max_area;
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
	PR_ERR("Unrecognized vendor firmware descriptor\n");

success:
	udlctd_kfree(udlctd_info, buf);
	return true;
}

/* Get the EDID from the USB device */

static int udlctd_get_edid(struct udlctd_info *udlctd_info,
				char *edid, int len)
{
	int i;
	int ret;
	char *rbuf;

	rbuf = udlctd_kmalloc(udlctd_info, 2, GFP_KERNEL);
	if (!rbuf)
		return 0;

	for (i = 0; i < len; i++) {
		ret = usb_control_msg(udlctd_info->udev,
				    usb_rcvctrlpipe(udlctd_info->udev, 0), (0x02),
				    (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
				    HZ);
		if (ret < 1) {
			PR_ERR("Read EDID byte %d failed err %x\n", i, ret);
			i--;
			break;
		}
		edid[i] = rbuf[1];
	}

	udlctd_kfree(udlctd_info, rbuf);

	return i;
}

/*
 * Check whether a video mode is supported by the DisplayLink chip
 * We start from monitor's modes, so don't need to filter that here
 */
static int udlctd_is_valid_mode(struct udlctd_info *udlctd_info,
				int xres, int yres)
{
	if (xres * yres > udlctd_info->sku_pixel_limit) {
		PR_WARN("%dx%d beyond chip capabilities\n",
		       xres, yres);
		return 0;
	}

	PR_INFO("%dx%d valid mode\n", xres, yres);

	return 1;
}

static int udlctd_setup_modes(struct udlctd_info *udlctd_info)
{
	int i;
	const struct fb_videomode *default_vmode = NULL;
	struct fb_monspecs monspecs;
	struct fb_modelist *fb_modelist_ptr;
	struct list_head modelist;
	struct udlctd_video_mode *udlctd_video_mode, *tmp;

	int result = 0;
	char *edid;
	int tries = 3;

	INIT_LIST_HEAD(&modelist);

	edid = udlctd_kmalloc(udlctd_info, EDID_LENGTH, GFP_KERNEL);

	if (!edid) {
		result = -ENOMEM;
		goto error;
	}

	list_for_each_entry_safe(udlctd_video_mode, tmp,
				&udlctd_info->fb_mode_list, list) {
		list_del(&udlctd_video_mode->list);
		udlctd_kfree(udlctd_info, udlctd_video_mode);
	}

	fb_destroy_modelist(&modelist);
	memset(&monspecs, 0, sizeof(monspecs));

	/*
	 * Try to (re)read EDID from hardware first
	 * EDID data may return, but not parse as valid
	 * Try again a few times, in case of e.g. analog cable noise
	 */
	while (tries--) {

		i = udlctd_get_edid(udlctd_info, edid, EDID_LENGTH);

		if (i >= EDID_LENGTH)
			fb_edid_to_monspecs(edid, &monspecs);

		if (monspecs.modedb_len > 0) {
			udlctd_info->edid = edid;
			udlctd_info->edid_size = i;
			break;
		}
	}

	/* If we've got modes, lets pick a best default mode */
	if (monspecs.modedb_len > 0) {
		for (i = 0; i < monspecs.modedb_len; i++) {
			if (udlctd_is_valid_mode(
				udlctd_info,
				monspecs.modedb[i].xres,
				monspecs.modedb[i].yres)) {

				fb_add_videomode(&monspecs.modedb[i],
					&modelist);
			} else {
				if (i == 0)
					/* if we've removed top/best mode */
					monspecs.misc &= ~FB_MISC_1ST_DETAIL;
			}
		}

		default_vmode = fb_find_best_display(&monspecs, &modelist);
	}

	/* If everything else has failed, fall back to safe default mode */
	if (default_vmode == NULL) {
		struct fb_videomode fb_vmode = {0};

		/*
		 * Add the standard VESA modes to our modelist
		 * Since we don't have EDID, there may be modes that
		 * overspec monitor and/or are incorrect aspect ratio, etc.
		 * But at least the user has a chance to choose
		 */
		for (i = 0; i < VESA_MODEDB_SIZE; i++) {
			if (udlctd_is_valid_mode(udlctd_info,
				((struct fb_videomode *)&vesa_modes[i])->xres,
				((struct fb_videomode *)&vesa_modes[i])->yres))

				fb_add_videomode(&vesa_modes[i],
					&modelist);
		}

		/*
		 * default to resolution safe for projectors
		 * (since they are most common case without EDID)
		 */
		fb_vmode.xres = 800;
		fb_vmode.yres = 600;
		fb_vmode.refresh = 60;
		default_vmode = fb_find_nearest_mode(&fb_vmode, &modelist);
	}

	/* Erase our old modelist */
	list_for_each_entry_safe(udlctd_video_mode, tmp,
				&udlctd_info->fb_mode_list, list) {
		udlctd_kfree(udlctd_info, udlctd_video_mode);
		list_del(&udlctd_video_mode->list);
	}

	/* Build our modelist from the fbdev modelist */
	list_for_each_entry(fb_modelist_ptr, &modelist, list) {
		udlctd_video_mode = udlctd_kmalloc(
			udlctd_info,
			sizeof(struct udlctd_video_mode),
			GFP_KERNEL);

		udlctd_video_mode->xres = fb_modelist_ptr->mode.xres;
		udlctd_video_mode->yres = fb_modelist_ptr->mode.yres;
		udlctd_video_mode->pixclock = fb_modelist_ptr->mode.pixclock;
		udlctd_video_mode->left_margin =
			fb_modelist_ptr->mode.left_margin;
		udlctd_video_mode->right_margin =
			fb_modelist_ptr->mode.right_margin;
		udlctd_video_mode->upper_margin =
			fb_modelist_ptr->mode.upper_margin;
		udlctd_video_mode->lower_margin =
			fb_modelist_ptr->mode.lower_margin;
		udlctd_video_mode->hsync_len =
			fb_modelist_ptr->mode.hsync_len;
		udlctd_video_mode->vsync_len =
			fb_modelist_ptr->mode.vsync_len;

		list_add(&udlctd_video_mode->list,
			&udlctd_info->fb_mode_list);
	}

	fb_destroy_modelist(&modelist);

	if (default_vmode != NULL) {
		udlctd_info->default_video_mode.xres = default_vmode->xres;
		udlctd_info->default_video_mode.yres = default_vmode->yres;
		udlctd_info->default_video_mode.pixclock =
			default_vmode->pixclock;
		udlctd_info->default_video_mode.left_margin =
			default_vmode->left_margin;
		udlctd_info->default_video_mode.right_margin =
			default_vmode->right_margin;
		udlctd_info->default_video_mode.upper_margin =
			default_vmode->upper_margin;
		udlctd_info->default_video_mode.lower_margin =
			default_vmode->lower_margin;
		udlctd_info->default_video_mode.hsync_len =
			default_vmode->hsync_len;
		udlctd_info->default_video_mode.vsync_len =
			default_vmode->vsync_len;
	} else
		result = -EINVAL;

error:
	if (edid && (udlctd_info->edid != edid))
		udlctd_kfree(udlctd_info, edid);

	return result;
}

static int udlctd_alloc_scratch_memory(struct udlctd_info *udlctd_info,
					int line_bytes, int num_lines)
{
	int result;

	struct udlctd_scratch_memory_descriptor *scratch_memory;

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

	bb_pages = udlctd_kmalloc(udlctd_info,
				sizeof(struct page *) * bb_num_pages,
				GFP_KERNEL);

	if (!bb_pages)
		goto bb_err;

	result = udlctd_alloc_multiple_pages(udlctd_info,
						GFP_KERNEL,
						bb_pages, bb_num_pages);

	if (result > 0)
		goto bb_err;


	/* allocate the hline_16 buffer */
	hline_16_num_pages = (line_bytes / 2) / PAGE_SIZE;

	if ((line_bytes / 2) % PAGE_SIZE > 0)
		hline_16_num_pages++;

	hline_16_pages = udlctd_kmalloc(udlctd_info,
				sizeof(struct page *) * hline_16_num_pages,
				GFP_KERNEL);

	if (!hline_16_pages)
		goto hline_16_err;

	result = udlctd_alloc_multiple_pages(udlctd_info,
						GFP_KERNEL,
						hline_16_pages,
						hline_16_num_pages);

	if (result > 0)
		goto hline_16_err;

	/* allocate the hline_8 buffer */
	hline_8_num_pages = (line_bytes / 4) / PAGE_SIZE;

	if ((line_bytes / 4) % PAGE_SIZE > 0)
		hline_8_num_pages++;

	hline_8_pages = udlctd_kmalloc(udlctd_info,
				sizeof(struct page *) * hline_8_num_pages,
				GFP_KERNEL);

	if (!hline_8_pages)
		goto hline_8_err;

	result = udlctd_alloc_multiple_pages(udlctd_info,
						GFP_KERNEL,
						hline_8_pages,
						hline_8_num_pages);

	if (result > 0)
		goto hline_8_err;


	goto success;


	/* If any error conditions are triggered, we only need to clean */
	/* up whatever was allocated successfully before. */
	/* The allocation that failed will be cleaned up inside the */
	/* allocator itself. */

hline_8_err:
	if (hline_8_pages)
		udlctd_kfree(udlctd_info, hline_8_pages);

	udlctd_free_multiple_pages(udlctd_info,
					hline_16_pages,
					hline_16_num_pages);
	PR_ERR("Error during hline_8 scratch memory allocation\n");
hline_16_err:
	if (hline_16_pages)
		udlctd_kfree(udlctd_info, hline_16_pages);

	udlctd_free_multiple_pages(udlctd_info, bb_pages, bb_num_pages);

	PR_ERR("Error during hline_16 scratch memory allocation\n");

bb_err:
	if (bb_pages)
		udlctd_kfree(udlctd_info, bb_pages);

	PR_ERR("Error during backing buffer scratch memory allocation\n");
	return -ENOMEM;

success:

	scratch_memory = udlctd_kzalloc(udlctd_info,
			sizeof(struct udlctd_scratch_memory_descriptor),
			GFP_KERNEL);

	scratch_memory->backing_buffer_pages = bb_pages;
	scratch_memory->backing_buffer_num_pages = bb_num_pages;
	scratch_memory->hline_16_pages = hline_16_pages;
	scratch_memory->hline_16_num_pages = hline_16_num_pages;
	scratch_memory->hline_8_pages = hline_8_pages;
	scratch_memory->hline_8_num_pages = hline_8_num_pages;

	udlctd_info->scratch_memory = scratch_memory;

	PR_DEBUG("Allocated backing buffer: %u pages\n", bb_num_pages);
	PR_DEBUG("Allocated hline_16 buffer: %u pages\n", hline_16_num_pages);
	PR_DEBUG("Allocated hline_8 buffer: %u pages\n", hline_8_num_pages);

	return 0;
}

static void udlctd_free_scratch_memory(struct udlctd_info *udlctd_info)
{
	struct udlctd_scratch_memory_descriptor *scratch_memory =
			udlctd_info->scratch_memory;

	if (!scratch_memory)
		return;

	if (scratch_memory->backing_buffer_pages) {
		udlctd_free_multiple_pages(udlctd_info,
				scratch_memory->backing_buffer_pages,
				scratch_memory->backing_buffer_num_pages);
		udlctd_kfree(udlctd_info,
				scratch_memory->backing_buffer_pages);
	}

	if (scratch_memory->hline_16_pages) {
		udlctd_free_multiple_pages(udlctd_info,
				scratch_memory->hline_16_pages,
				scratch_memory->hline_16_num_pages);
		udlctd_kfree(udlctd_info,
				scratch_memory->hline_16_pages);
	}
	if (scratch_memory->hline_8_pages) {
		udlctd_free_multiple_pages(udlctd_info,
				scratch_memory->hline_8_pages,
				scratch_memory->hline_8_num_pages);
		udlctd_kfree(udlctd_info,
				scratch_memory->hline_8_pages);
	}

	udlctd_kfree(udlctd_info, scratch_memory);

	return;
}

static int udlctd_map_scratch_memory(struct udlctd_info *udlctd_info)
{
	struct udlctd_scratch_memory_descriptor *scratch_memory =
			udlctd_info->scratch_memory;
	uint32_t blank_pixel_32 = UDLCTD_BLANK_COLOR;
	uint16_t blank_pixel_16;
	uint8_t blank_pixel_8;

	if (!scratch_memory)
		return 1;

	split_pixel_argb32(&blank_pixel_32, &blank_pixel_16, &blank_pixel_8);

	udlctd_info->backing_buffer = vm_map_ram(
					scratch_memory->
						backing_buffer_pages,
					scratch_memory->
						backing_buffer_num_pages,
					0, PAGE_KERNEL);

	memset(udlctd_info->backing_buffer, blank_pixel_32,
			scratch_memory->backing_buffer_num_pages * PAGE_SIZE);
	PR_DEBUG("Mapped backing buffer pages, starting at: %p\n",
				udlctd_info->backing_buffer);

	if (!udlctd_info->backing_buffer)
		goto bb_err;

	udlctd_info->hline_16 = vm_map_ram(
					scratch_memory->hline_16_pages,
					scratch_memory->hline_16_num_pages,
					0, PAGE_KERNEL);

	memset(udlctd_info->hline_16, blank_pixel_16,
			scratch_memory->hline_16_num_pages * PAGE_SIZE);
	PR_DEBUG("Mapped hline_16 pages, starting at: %p\n",
				udlctd_info->hline_16);

	if (!udlctd_info->hline_16)
		goto hline_16_err;


	udlctd_info->hline_8 = vm_map_ram(
					scratch_memory->hline_8_pages,
					scratch_memory->hline_8_num_pages,
					0, PAGE_KERNEL);

	memset(udlctd_info->hline_8, blank_pixel_8,
			scratch_memory->hline_8_num_pages * PAGE_SIZE);
	PR_DEBUG("Mapped hline_8 pages, starting at: %p\n",
				udlctd_info->hline_8);

	if (!udlctd_info->hline_8)
		goto hline_8_err;

	goto success;

hline_8_err:

	vm_unmap_ram(udlctd_info->hline_16,
			scratch_memory->hline_16_num_pages);
hline_16_err:
	vm_unmap_ram(udlctd_info->backing_buffer,
			scratch_memory->backing_buffer_num_pages);
bb_err:

	return -ENOMEM;

success:
	return 0;
}

static void udlctd_unmap_scratch_memory(struct udlctd_info *udlctd_info)
{
	struct udlctd_scratch_memory_descriptor *scratch_memory =
			udlctd_info->scratch_memory;

	if (!scratch_memory)
		return;

	if (udlctd_info->backing_buffer)
		vm_unmap_ram(udlctd_info->backing_buffer,
				scratch_memory->backing_buffer_num_pages);

	if (udlctd_info->hline_16)
		vm_unmap_ram(udlctd_info->hline_16,
				scratch_memory->hline_16_num_pages);

	if (udlctd_info->hline_8)
		vm_unmap_ram(udlctd_info->hline_8,
				scratch_memory->hline_8_num_pages);

	return;
}

/*
 * This generates the appropriate command sequence that then drives the
 * display controller to set the video mode.
 */
static int udlctd_set_video_mode(struct udlctd_info *udlctd_info,
				struct udlctd_video_mode *mode)
{
	char *buf;
	char *wrptr;
	int retval = 0;
	int writesize;
	struct urb *urb;

	if (!atomic_read(&udlctd_info->usb_active))
		return -EPERM;

	urb = udlctd_get_urb(udlctd_info);
	if (!urb)
		return -ENOMEM;

	PR_INFO("Setting video mode to %dx%d.\n", mode->xres, mode->yres);

	buf = (char *) urb->transfer_buffer;

	/*
	* This first section has to do with setting the base address on the
	* controller * associated with the display. There are 2 base
	* pointers, currently, we only * use the 16 bpp segment.
	*/
	wrptr = udlctd_vidreg_lock(buf);

	if (!true32bpp)
		wrptr = udlctd_set_color_depth(wrptr, 0x00);
	else if (true32bpp)
		wrptr = udlctd_set_color_depth(wrptr, 0x01);

	/* set base for 16bpp segment to 0 */
	wrptr = udlctd_set_base16bpp(wrptr, 0);
	udlctd_info->base16 = 0;
	/* set base for 8bpp segment to end of fb */
	wrptr = udlctd_set_base8bpp(wrptr, mode->xres * mode->yres * 2);
	udlctd_info->base8 = mode->xres * mode->yres * 2;

	wrptr = udlctd_set_vid_cmds(wrptr, mode);
	wrptr = udlctd_enable_hvsync(wrptr, true);
	wrptr = udlctd_vidreg_unlock(wrptr);

	writesize = wrptr - buf;

	retval = udlctd_submit_urb(udlctd_info, urb, writesize);

	if (retval) {
		PR_ERR("Error setting hardware mode.\n");
		return 1;
	}

	udlctd_info->current_video_mode = mode;

	return retval;
}

/* The following are all low-level DisplayLink manipulation functions */

/*
 * All DisplayLink bulk operations start with 0xAF, followed by specific code
 * All operations are written to buffers which then later get sent to device
 */
static char *udlctd_set_register(char *buf, u8 reg, u8 val)
{
	*buf++ = 0xAF;
	*buf++ = 0x20;
	*buf++ = reg;
	*buf++ = val;
	return buf;
}

static char *udlctd_vidreg_lock(char *buf)
{
	return udlctd_set_register(buf, 0xFF, 0x00);
}

static char *udlctd_vidreg_unlock(char *buf)
{
	return udlctd_set_register(buf, 0xFF, 0xFF);
}

/*
 * On/Off for driving the DisplayLink framebuffer to the display
 *  0x00 H and V sync on
 *  0x01 H and V sync off (screen blank but powered)
 *  0x07 DPMS powerdown (requires modeset to come back)
 */
static char *udlctd_enable_hvsync(char *buf, bool enable)
{
	if (enable)
		return udlctd_set_register(buf, 0x1F, 0x00);
	else
		return udlctd_set_register(buf, 0x1F, 0x07);
}

static char *udlctd_set_color_depth(char *buf, u8 selection)
{
	return udlctd_set_register(buf, 0x00, selection);
}

static char *udlctd_set_base16bpp(char *wrptr, u32 base)
{
	/* the base pointer is 16 bits wide, 0x20 is hi byte. */
	wrptr = udlctd_set_register(wrptr, 0x20, base >> 16);
	wrptr = udlctd_set_register(wrptr, 0x21, base >> 8);
	return udlctd_set_register(wrptr, 0x22, base);
}

/*
 * DisplayLink HW has separate 16bpp and 8bpp framebuffers.
 * In 24bpp modes, the low 323 RGB bits go in the 8bpp framebuffer
 */
static char *udlctd_set_base8bpp(char *wrptr, u32 base)
{
	wrptr = udlctd_set_register(wrptr, 0x26, base >> 16);
	wrptr = udlctd_set_register(wrptr, 0x27, base >> 8);
	return udlctd_set_register(wrptr, 0x28, base);
}

static char *udlctd_set_register_16(char *wrptr, u8 reg, u16 value)
{
	wrptr = udlctd_set_register(wrptr, reg, value >> 8);
	return udlctd_set_register(wrptr, reg+1, value);
}

/*
 * This is kind of weird because the controller takes some
 * register values in a different byte order than other registers.
 */
static char *udlctd_set_register_16be(char *wrptr, u8 reg, u16 value)
{
	wrptr = udlctd_set_register(wrptr, reg, value);
	return udlctd_set_register(wrptr, reg+1, value >> 8);
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
static u16 udlctd_lfsr16(u16 actual_count)
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
static char *udlctd_set_register_lfsr16(char *wrptr, u8 reg, u16 value)
{
	return udlctd_set_register_16(wrptr, reg, udlctd_lfsr16(value));
}

/*
 * This takes a standard fbdev screeninfo struct and all of its monitor mode
 * details and converts them into the DisplayLink equivalent register commands.
 */
static char *udlctd_set_vid_cmds(char *wrptr, struct udlctd_video_mode *mode)
{
	u16 xds, yds;
	u16 xde, yde;
	u16 yec;

	/* x display start */
	xds = mode->left_margin + mode->hsync_len;
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x01, xds);
	/* x display end */
	xde = xds + mode->xres;
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x03, xde);

	/* y display start */
	yds = mode->upper_margin + mode->vsync_len;
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x05, yds);
	/* y display end */
	yde = yds + mode->yres;
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x07, yde);

	/* x end count is active + blanking - 1 */
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x09,
			xde + mode->right_margin - 1);

	/* libdlo hardcodes hsync start to 1 */
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x0B, 1);

	/* hsync end is width of sync pulse + 1 */
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x0D, mode->hsync_len + 1);

	/* hpixels is active pixels */
	wrptr = udlctd_set_register_16(wrptr, 0x0F, mode->xres);

	/* yendcount is vertical active + vertical blanking */
	yec = mode->yres + mode->upper_margin + mode->lower_margin +
			mode->vsync_len;
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x11, yec);

	/* libdlo hardcodes vsync start to 0 */
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x13, 0);

	/* vsync end is width of vsync pulse */
	wrptr = udlctd_set_register_lfsr16(wrptr, 0x15, mode->vsync_len);

	/* vpixels is active pixels */
	wrptr = udlctd_set_register_16(wrptr, 0x17, mode->yres);

	/* convert picoseconds to 5kHz multiple for pclk5k = x * 1E12/5k */
	wrptr = udlctd_set_register_16be(wrptr, 0x1B,
			200*1000*1000/mode->pixclock);

	return wrptr;
}

/* The following are all low-level USB functions */

static void udlctd_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct udlctd_info *udlctd_info = (struct udlctd_info *)unode->dev;
	unsigned long flags;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)) {
			PR_ERR("%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);
			atomic_set(&udlctd_info->lost_pixels, 1);
		}
	}

	urb->transfer_buffer_length = udlctd_info->urbs.size; /* reset to actual */

	spin_lock_irqsave(&udlctd_info->urbs.lock, flags);
	list_add_tail(&unode->entry, &udlctd_info->urbs.list);
	udlctd_info->urbs.available++;
	spin_unlock_irqrestore(&udlctd_info->urbs.lock, flags);

	/*
	 * When using fb_defio, we deadlock if up() is called
	 * while another is waiting. So queue to another process.
	 */
	/*if (fb_defio)
		schedule_delayed_work(&unode->release_urb_work, 0);
	else*/
	up(&udlctd_info->urbs.limit_sem);
}

static void udlctd_free_urb_list(struct udlctd_info *udlctd_info)
{
	int count = udlctd_info->urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;
	int ret;
	unsigned long flags;

	pr_notice("Waiting for completes and freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {

		/* Getting interrupted means a leak, but ok at shutdown*/
		ret = down_interruptible(&udlctd_info->urbs.limit_sem);
		if (ret)
			break;

		spin_lock_irqsave(&udlctd_info->urbs.lock, flags);

		node = udlctd_info->urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irqrestore(&udlctd_info->urbs.lock, flags);

		unode = list_entry(node, struct urb_node, entry);
		urb = unode->urb;

		/* Free each separately allocated piece */
		usb_free_coherent(urb->dev, udlctd_info->urbs.size,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		udlctd_kfree(udlctd_info, node);
	}

}

static int udlctd_alloc_urb_list(struct udlctd_info *udlctd_info, int count, size_t size)
{
	int i = 0;
	struct urb *urb;
	struct urb_node *unode;
	char *buf;

	spin_lock_init(&udlctd_info->urbs.lock);

	udlctd_info->urbs.size = size;
	INIT_LIST_HEAD(&udlctd_info->urbs.list);

	while (i < count) {
		unode = udlctd_kzalloc(udlctd_info, sizeof(struct urb_node), GFP_KERNEL);
		if (!unode)
			break;
		unode->dev = udlctd_info;

		INIT_DELAYED_WORK(&unode->release_urb_work,
			  udlctd_release_urb_work);

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			udlctd_kfree(udlctd_info, unode);
			break;
		}
		unode->urb = urb;

		buf = usb_alloc_coherent(udlctd_info->udev, MAX_TRANSFER, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!buf) {
			udlctd_kfree(udlctd_info, unode);
			usb_free_urb(urb);
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, udlctd_info->udev, usb_sndbulkpipe(udlctd_info->udev, 1),
			buf, size, udlctd_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &udlctd_info->urbs.list);

		i++;
	}

	sema_init(&udlctd_info->urbs.limit_sem, i);
	udlctd_info->urbs.count = i;
	udlctd_info->urbs.available = i;

	pr_notice("allocated %d %d byte urbs\n", i, (int) size);

	return i;
}

static struct urb *udlctd_get_urb(struct udlctd_info *udlctd_info)
{
	int ret = 0;
	struct list_head *entry;
	struct urb_node *unode;
	struct urb *urb = NULL;
	unsigned long flags;

	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&udlctd_info->urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		atomic_set(&udlctd_info->lost_pixels, 1);
		PR_WARN("wait for urb interrupted: %x available: %d\n",
		       ret, udlctd_info->urbs.available);
		goto error;
	}

	spin_lock_irqsave(&udlctd_info->urbs.lock, flags);

	BUG_ON(list_empty(&udlctd_info->urbs.list)); /* reserved one with limit_sem */
	entry = udlctd_info->urbs.list.next;
	list_del_init(entry);
	udlctd_info->urbs.available--;

	spin_unlock_irqrestore(&udlctd_info->urbs.lock, flags);

	unode = list_entry(entry, struct urb_node, entry);
	urb = unode->urb;

error:
	return urb;
}

static int udlctd_submit_urb(struct udlctd_info *udlctd_info, struct urb *urb, size_t len)
{
	int ret;

	BUG_ON(len > udlctd_info->urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		udlctd_urb_completion(urb); /* because no one else will */
		atomic_set(&udlctd_info->lost_pixels, 1);
		PR_ERR("usb_submit_urb error %x\n", ret);
	}
	return ret;
}

static void udlctd_release_urb_work(struct work_struct *work)
{
	struct urb_node *unode = container_of(work, struct urb_node,
					      release_urb_work.work);

	up(&unode->dev->urbs.limit_sem);
}
