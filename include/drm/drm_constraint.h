/*
 * Copyright (c) 2017 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_CONSTRAINT_H__
#define __DRM_CONSTRAINT_H__

#include <linux/list.h>

/**
 * struct drm_constraint - display object hardware constraints
 * @dev: DRM device this constraint relates to
 * @head: for list management
 * @type: constraint "opcode"
 *
 *
 * Describes a hardware-specific constraints on the use of DRM objects.
 * Constraints should represent invariants that do not change according to the
 * current system state (e.g., hardware planes that are _always_ mutually
 * exclusive).
 */
struct drm_plane_constraint {
	struct drm_device *dev;
	struct list_head head;

	uint32_t type;
	uint32_t data[7];
};

#endif
