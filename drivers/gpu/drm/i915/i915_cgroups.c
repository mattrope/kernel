/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * DOC: cgroups integration
 *
 * i915 makes use of the DRM cgroup helper library.  Currently i915 only
 * supports a single cgroup parameter:
 *
 * I915_CGRP_DEF_CONTEXT_PRIORITY -
 *   Setting this parameter on a cgroup will cause GPU contexts created by
 *   processes in the cgroup to start with the specified default priority (in
 *   the range of I915_CONTEXT_MIN_USER_PRIORITY to
 *   I915_CONTEXT_MAX_USER_PRIORITY) instead of the usual priority of
 *   I915_CONTEXT_DEFAULT_PRIORITY.  This cgroup parameter only provides
 *   a default starting point; the context priorities may still be overridden
 *   by other mechanisms (e.g., I915_CONTEXT_PARAM_PRIORITY) or adjusted at
 *   runtime due to system behavior.
 */

#include <linux/cgroup.h>
#include <drm/drm_cgroup.h>

#include "i915_drv.h"

static struct drm_cgroup_funcs i915_cgrp = {
	.set_param = drm_cgrp_helper_set_param,
};

static struct drm_cgroup_helper_data *
i915_cgrp_alloc_params(void)
{
	struct i915_cgroup_data *data;

	data = kzalloc(sizeof *data, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	return &data->base;
}

static int
i915_cgrp_update_param(struct drm_cgroup_helper_data *data,
		       uint64_t param, int64_t val)
{
	struct i915_cgroup_data *idata =
		container_of(data, struct i915_cgroup_data, base);

	if (param != I915_CGRP_DEF_CONTEXT_PRIORITY) {
		DRM_DEBUG_DRIVER("Invalid cgroup parameter %llu\n", param);
		return -EINVAL;
	}

	if (val > I915_CONTEXT_MAX_USER_PRIORITY ||
	    val < I915_CONTEXT_MIN_USER_PRIORITY) {
		DRM_DEBUG_DRIVER("Context priority must be in range (%d,%d)\n",
				 I915_CONTEXT_MIN_USER_PRIORITY,
				 I915_CONTEXT_MAX_USER_PRIORITY);
		return -EINVAL;
	}

	idata->priority = val;

	return 0;
}

static int
i915_cgrp_read_param(struct drm_cgroup_helper_data *data,
		     uint64_t param, int64_t *val)
{
	struct i915_cgroup_data *idata =
		container_of(data, struct i915_cgroup_data, base);

	switch (param)
	{
	case I915_CGRP_DEF_CONTEXT_PRIORITY:
		*val = idata->priority;
		break;
	default:
		DRM_DEBUG_DRIVER("Invalid cgroup parameter %llu\n", param);
		return -EINVAL;
	}

	return 0;
}

static struct drm_cgroup_helper i915_cgrp_helper = {
	.alloc_params = i915_cgrp_alloc_params,
	.update_param = i915_cgrp_update_param,
	.read_param = i915_cgrp_read_param,
};

void
i915_cgroup_init(struct drm_i915_private *dev_priv)
{
	dev_priv->drm.cgroup = &i915_cgrp;

	drm_cgrp_helper_init(&dev_priv->drm,
			     &i915_cgrp_helper);
}

void
i915_cgroup_shutdown(struct drm_i915_private *dev_priv)
{
	drm_cgrp_helper_shutdown(&i915_cgrp_helper);
}

/**
 * i915_cgroup_get_prio() - get priority associated with current proc's cgroup
 * @dev_priv: drm device
 * @file_priv: file handle for calling process
 *
 * RETURNS:
 * Priority associated with the calling process' cgroup in the default (v2)
 * hierarchy, otherwise I915_PRIORITY_NORMAL if no explicit priority has
 * been assigned.
 */
int
i915_cgroup_get_prio(struct drm_i915_private *dev_priv,
		     struct drm_i915_file_private *file_priv)
{
	struct cgroup *cgrp;
	int64_t prio;
	int ret;

	/* Ignore internally-created contexts not associated with a process */
	if (!file_priv)
		return I915_PRIORITY_NORMAL;

	cgrp = drm_file_get_cgroup(file_priv->file, &cgrp_dfl_root);
	if (WARN_ON(!cgrp))
		return I915_PRIORITY_NORMAL;

	ret = drm_cgrp_helper_get_param(&dev_priv->drm, cgrp,
					I915_CGRP_DEF_CONTEXT_PRIORITY,
					&prio);
	if (ret)
		/* No default priority has been associated with cgroup */
		return I915_PRIORITY_NORMAL;
	else
		return prio;
}
