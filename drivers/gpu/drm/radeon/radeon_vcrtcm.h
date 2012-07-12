/*
 * Copyright 2010 Alcatel-Lucent Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author: Ilija Hadzic <ihadzic@research.bell-labs.com>
 */

#ifndef __RADEON_VCRTCM_H__
#define __RADEON_VCRTCM_H__

typedef struct {

	/* op code, tells the ioctl what to do to */
	int op_code;

	/* display index tells ioctl which CRTC to apply the command to */
	int display_index;

	/* pointer to userland buffer if more than two 32-bit arguments */
	/* are necessary or if the ioctl reads the data */
	void *user;

	/* arguments, members of the union correspond to */
	/* argument names that are meaningful for each op code */
	/* if you are adding an op code, add the argument name here */
	/* unless you are on a mission to write unmaintainable code */
	/* or are just lazy (in which case you can just use arg1 and arg2) */
	union {
		int user_len;	/* for opcodes that pass a pointer to userland buffer */
		/* this is the buffer length */
		int fps;	/* frames per second (for set rate call) */
		int major;	/* hal major device (for attach) */
		int arg1;	/* for undecided and lazy programmers */
		uint32_t pconid;
	} arg1;

	union {
		int flags;	/* for opcodes that require additional flags (e.g. grab) */
		int minor;	/* hal minor (for attach) */
		int arg2;	/* for undecided and lazy programmers */
	} arg2;

	union {
		int flow;	/* hal flow (for attach) */
		int arg3;	/* for undecided and lazy programmers */
	} arg3;

} radeon_vcrtcm_ctl_descriptor_t;

#define RADEON_VCRTCM_FPS_HARD_LIMIT  100

#define DRM_RADEON_VCRTCM 0x2c
#define DRM_IOCTL_RADEON_VCRTCM   DRM_IOW(DRM_COMMAND_BASE+DRM_RADEON_VCRTCM, radeon_vcrtcm_ctl_descriptor_t)

/* opcodes */
#define RADEON_VCRTCM_CTL_OP_CODE_NOP         0x00
#define RADEON_VCRTCM_CTL_OP_CODE_SET_RATE    0x01
#define RADEON_VCRTCM_CTL_OP_CODE_ATTACH      0x02
#define RADEON_VCRTCM_CTL_OP_CODE_DETACH      0x03
#define RADEON_VCRTCM_CTL_OP_CODE_XMIT        0x04

#endif
