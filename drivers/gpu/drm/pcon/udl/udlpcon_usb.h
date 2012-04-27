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

#ifndef __DLUSB_H
#define __DLUSB_H

#include <linux/usb.h>

/* DisplayLink rendering functions */
static int udlpcon_trim_hline(const u8 *bback, const u8 **bfront, int *width_bytes);
static void udlpcon_compress_hline_16(
	const uint16_t **pixel_start_ptr,
	const uint16_t *const pixel_end,
	uint32_t *device_address_ptr,
	uint8_t **command_buffer_ptr,
	const uint8_t *const cmd_buffer_end);
static void udlpcon_compress_hline_8(
	const uint8_t **pixel_start_ptr,
	const uint8_t *const pixel_end,
	uint32_t *device_address_ptr,
	uint8_t **command_buffer_ptr,
	const uint8_t *const cmd_buffer_end);
static int udlpcon_render_hline(struct udlpcon_info *udlpcon_info, struct urb **urb_ptr,
				const char *front, char **urb_buf_ptr,
				u32 byte_offset, u32 byte_width,
				int *ident_ptr, int *sent_ptr);
static int udlpcon_blank_hw_fb(struct udlpcon_info *udlpcon_info, unsigned color);

/* USB management functions */
static int udlpcon_usb_probe(struct usb_interface *interface,
			const struct usb_device_id *id);
static void udlpcon_usb_disconnect(struct usb_interface *interface);

void udlpcon_free(struct kref *kref);


/* DisplayLink stuff */
static int udlpcon_select_std_channel(struct udlpcon_info *udlpcon_info);
static int udlpcon_parse_vendor_descriptor(struct udlpcon_info *udlpcon_info,
					struct usb_device *usbdev);
static int udlpcon_get_edid(struct udlpcon_info *udlpcon_info,
				char *edid, int len);
static int udlpcon_is_valid_mode(struct udlpcon_info *udlpcon_info,
				int xres, int yres);

static int udlpcon_alloc_scratch_memory(struct udlpcon_info *udlpcon_info,
					int line_bytes, int num_lines);
static void udlpcon_free_scratch_memory(struct udlpcon_info *udlpcon_info);
static int udlpcon_map_scratch_memory(struct udlpcon_info *udlpcon_info);
static void udlpcon_unmap_scratch_memory(struct udlpcon_info *udlpcon_info);
static int udlpcon_set_video_mode(struct udlpcon_info *udlpcon_info,
				struct udlpcon_video_mode *mode);
static void udlpcon_query_edid(struct work_struct *work);

/* DisplayLink low level stuff */
static char *udlpcon_set_register(char *buf, u8 reg, u8 val);
static char *udlpcon_vidreg_lock(char *buf);
static char *udlpcon_vidreg_unlock(char *buf);
static char *udlpcon_enable_hvsync(char *buf, bool enable);
static char *udlpcon_set_color_depth(char *buf, u8 selection);
static char *udlpcon_set_base16bpp(char *wrptr, u32 base);
static char *udlpcon_set_base8bpp(char *wrptr, u32 base);
static char *udlpcon_set_register_16(char *wrptr, u8 reg, u16 value);
static char *udlpcon_set_register_16be(char *wrptr, u8 reg, u16 value);
static u16 udlpcon_lfsr16(u16 actual_count);
static char *udlpcon_set_register_lfsr16(char *wrptr, u8 reg, u16 value);
static char *udlpcon_set_vid_cmds(char *wrptr, struct udlpcon_video_mode *mode);

/* USB stuff */
static void udlpcon_urb_completion(struct urb *urb);
static void udlpcon_free_urb_list(struct udlpcon_info *udlpcon_info);
static int udlpcon_alloc_urb_list(struct udlpcon_info *udlpcon_info,
				int count, size_t size);
static struct urb *udlpcon_get_urb(struct udlpcon_info *udlpcon_info);
static int udlpcon_submit_urb(struct udlpcon_info *udlpcon_info,
				struct urb *urb, size_t len);
static void udlpcon_release_urb_work(struct work_struct *work);

#define NR_USB_REQUEST_I2C_SUB_IO 0x02
#define NR_USB_REQUEST_CHANNEL 0x12

/* -BULK_SIZE as per usb-skeleton. Can we get full page and avoid overhead? */
#define BULK_SIZE 512
#define MAX_TRANSFER (PAGE_SIZE*16 - BULK_SIZE)
#define WRITES_IN_FLIGHT (4)

#define MAX_VENDOR_DESCRIPTOR_SIZE 256

#define GET_URB_TIMEOUT	HZ
#define FREE_URB_TIMEOUT (HZ*2)

#define MAX_CMD_PIXELS		255

#define RLX_HEADER_BYTES	7
#define MIN_RLX_PIX_BYTES       4
#define MIN_RLX_CMD_BYTES	(RLX_HEADER_BYTES + MIN_RLX_PIX_BYTES)

/*
#define RLX8_HEADER_BYTES 	7
#define MIN_RLX8_PIX_BYTES	2
#define MIN_RLX8_CMD_BYTES	(RLX8_HEADER_BYTES + MIN_RLX8_PIX_BYTES)
*/

#define RLE_HEADER_BYTES	6
#define MIN_RLE_PIX_BYTES	3
#define MIN_RLE_CMD_BYTES	(RLE_HEADER_BYTES + MIN_RLE_PIX_BYTES)

#define RAW_HEADER_BYTES	6
#define MIN_RAW_PIX_BYTES	2
#define MIN_RAW_CMD_BYTES	(RAW_HEADER_BYTES + MIN_RAW_PIX_BYTES)

#define DL_DEFIO_WRITE_DELAY    5 /* fb_deferred_io.delay in jiffies */
#define DL_DEFIO_WRITE_DISABLE  (HZ*60) /* "disable" with long delay */

/* remove these once align.h patch is taken into kernel */
#define DL_ALIGN_UP(x, a) ALIGN(x, a)
#define DL_ALIGN_DOWN(x, a) ALIGN(x-(a-1), a)

/* Stuff from libdlo */

/** Return red/green component of a 16 bpp colour number. */
#define RG16(red, grn) (uint8_t)((((red) & 0xF8) | ((grn) >> 5)) & 0xFF)

/** Return green/blue component of a 16 bpp colour number. */
#define GB16(grn, blu) (uint8_t)(((((grn) & 0x1C) << 3) | ((blu) >> 3)) & 0xFF)

/** Return 8 bpp colour number from red, green and blue components. */
#define RGB8(red, grn, blu) ((((red) << 5) | (((grn) & 3) << 3) | ((blu) & 7)) & 0xFF)

/** Return a 32 bpp colour number when given the three RGB components. */
#define RGB(red, grn, blu) (uint32_t)(((red) & 0xFF) | (((grn) & 0xFF) << 8) | (((blu) & 0xFF) << 16))

/** Set the red component (0..255) of a 32 bpp colour. */
#define RGB_SETRED(col, red) (uint32_t)(((col) & ~0xFF) | ((red) & 0xFF))

/** Set the green component (0..255) of a 32 bpp colour. */
#define RGB_SETGRN(col, grn) (uint32_t)(((col) & ~0xFF00) | (((grn) & 0xFF) << 8))

/** Set the blue component (0..255) of a 32 bpp colour. */
#define RGB_SETBLU(col, blu) (uint32_t)(((col) & ~0xFF0000) | (((blu) & 0xFF) << 16))

/** Read the red component (0..255) of a 32 bpp colour. */
#define RGB_GETRED(col) (uint8_t)((col) & 0xFF)

/** Read the green component (0..255) of a 32 bpp colour. */
#define RGB_GETGRN(col) (uint8_t)(((col) >> 8) & 0xFF)

/** Read the blue component (0..255) of a 32 bpp colour. */
#define RGB_GETBLU(col) (uint8_t)(((col) >> 16) & 0xFF)

inline uint8_t rgb8(const uint32_t *ptr)

{
	uint8_t red = RGB_GETRED(*ptr);
	uint8_t grn = RGB_GETGRN(*ptr);
	uint8_t blu = RGB_GETBLU(*ptr);

	return RGB8(red, grn, blu);
}

inline uint16_t rgb16(const uint32_t *ptr)
{
	uint8_t red = RGB_GETRED(*ptr);
	uint8_t grn = RGB_GETGRN(*ptr);
	uint8_t blu = RGB_GETBLU(*ptr);

	return (RG16(blu, grn) << 8) + GB16(grn, red);
}

inline void split_pixel_argb32(const uint32_t *pixel32,
		uint16_t *pixel16, uint8_t *pixel8)
{
	uint8_t red = RGB_GETRED(*pixel32);
	uint8_t grn = RGB_GETGRN(*pixel32);
	uint8_t blu = RGB_GETBLU(*pixel32);

	if (pixel16)
		*pixel16 = (RG16(blu, grn) << 8) + GB16(grn, red);
	if (pixel8)
		*pixel8 = RGB8(blu, grn, red);

	return;
}

inline void alpha_overlay_argb32(uint32_t *fb_pixel, uint32_t *overlay_pixel)
{
	if (*overlay_pixel >> 24 > 0)
		*fb_pixel = *overlay_pixel;

	return;
}

inline void overlay_cursor(uint32_t *cursor_pixel,
		uint16_t *hline16_pixel, uint8_t *hline8_pixel)
{
	if (*cursor_pixel >> 24 > 0) {
		split_pixel_argb32(cursor_pixel, hline16_pixel, hline8_pixel);
	}

	return;
}

#endif
