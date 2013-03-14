/*
 * Copyright 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * description of display timings
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_DISPLAY_TIMING_H
#define __LINUX_DISPLAY_TIMING_H

#include <linux/bitops.h>
#include <linux/types.h>

/* VESA display monitor timing parameters */
#define VESA_DMT_HSYNC_LOW		BIT(0)
#define VESA_DMT_HSYNC_HIGH		BIT(1)
#define VESA_DMT_VSYNC_LOW		BIT(2)
#define VESA_DMT_VSYNC_HIGH		BIT(3)

/* display specific flags */
#define DISPLAY_FLAGS_DE_LOW		BIT(0)	/* data enable flag */
#define DISPLAY_FLAGS_DE_HIGH		BIT(1)
#define DISPLAY_FLAGS_PIXDATA_POSEDGE	BIT(2)	/* drive data on pos. edge */
#define DISPLAY_FLAGS_PIXDATA_NEGEDGE	BIT(3)	/* drive data on neg. edge */
#define DISPLAY_FLAGS_INTERLACED	BIT(4)
#define DISPLAY_FLAGS_DOUBLESCAN	BIT(5)

/*
 * A single signal can be specified via a range of minimal and maximal values
 * with a typical value, that lies somewhere inbetween.
 */
struct timing_entry {
	u32 min;
	u32 typ;
	u32 max;
};

enum timing_entry_index {
	TE_MIN = 0,
	TE_TYP = 1,
	TE_MAX = 2,
};

/*
 * Single "mode" entry. This describes one set of signal timings a display can
 * have in one setting. This struct can later be converted to struct videomode
 * (see include/video/videomode.h). As each timing_entry can be defined as a
 * range, one struct display_timing may become multiple struct videomodes.
 *
 * Example: hsync active high, vsync active low
 *
 *				    Active Video
 * Video  ______________________XXXXXXXXXXXXXXXXXXXXXX_____________________
 *	  |<- sync ->|<- back ->|<----- active ----->|<- front ->|<- sync..
 *	  |	     |	 porch  |		     |	 porch	 |
 *
 * HSync _|¯¯¯¯¯¯¯¯¯¯|___________________________________________|¯¯¯¯¯¯¯¯¯
 *
 * VSync ¯|__________|¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|_________
 */
struct display_timing {
	struct timing_entry pixelclock;

	struct timing_entry hactive;		/* hor. active video */
	struct timing_entry hfront_porch;	/* hor. front porch */
	struct timing_entry hback_porch;	/* hor. back porch */
	struct timing_entry hsync_len;		/* hor. sync len */

	struct timing_entry vactive;		/* ver. active video */
	struct timing_entry vfront_porch;	/* ver. front porch */
	struct timing_entry vback_porch;	/* ver. back porch */
	struct timing_entry vsync_len;		/* ver. sync len */

	unsigned int dmt_flags;			/* VESA DMT flags */
	unsigned int data_flags;		/* video data flags */
};

/*
 * This describes all timing settings a display provides.
 * The native_mode is the default setting for this display.
 * Drivers that can handle multiple videomodes should work with this struct and
 * convert each entry to the desired end result.
 */
struct display_timings {
	unsigned int num_timings;
	unsigned int native_mode;

	struct display_timing **timings;
};

/* get value specified by index from struct timing_entry */
static inline u32 display_timing_get_value(const struct timing_entry *te,
					   enum timing_entry_index index)
{
	switch (index) {
	case TE_MIN:
		return te->min;
		break;
	case TE_TYP:
		return te->typ;
		break;
	case TE_MAX:
		return te->max;
		break;
	default:
		return te->typ;
	}
}

/* get one entry from struct display_timings */
static inline struct display_timing *display_timings_get(const struct
							 display_timings *disp,
							 unsigned int index)
{
	if (disp->num_timings > index)
		return disp->timings[index];
	else
		return NULL;
}

void display_timings_release(struct display_timings *disp);

#endif
