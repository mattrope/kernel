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

#include <drm/drmP.h>
#include <drm/drm_constraint.h>

/**
 * DOC: overview
 *
 * A constraint represents a hardware-specific restriction on the usage of a
 * display resource.  Constraints are invariants that would be described in a
 * hardware specification and do not change according to runtime conditions.
 * Constraints may be queried by userspace compositors and serve as a hint
 * about various display configurations that should be avoided (i.e., will
 * never work).
 *
 * Constraints are represented by a 'type' followed by several bytes of data.
 * How the data bytes are interpreted will be determined by the specific
 * type (some data bytes may be unused for some constraint types).  New
 * constraint types will likely be added in the future, so userspace
 * compositors are expected to skip any constraints with types that they
 * do not recognize.
 */

static unsigned int drm_num_constraints(struct drm_device *dev)
{
	unsigned count = 0;
	struct drm_constraint *iter;

	list_for_each_entry(iter, dev->mode_config.constraint_list, head)
		count++;

	return count;
}
