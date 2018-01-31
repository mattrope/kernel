/* SPDX-License-Identifier: MIT */
/*
 * i915_cgroup.c - Linux cgroups integration for i915
 *
 * Copyright (C) 2018 Intel Corporation
 */

#include <linux/cgroup.h>

#include "i915_drv.h"

struct i915_cgroup_data {
	struct kref ref;
};

static inline struct i915_cgroup_data *
cgrp_ref_to_i915(struct kref *ref)
{
	return container_of(ref, struct i915_cgroup_data, ref);
}

static void
i915_cgroup_free(struct kref *ref)
{
	struct i915_cgroup_data *priv;

	priv = cgrp_ref_to_i915(ref);
	kfree(priv);
}

int
i915_cgroup_init(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	dev_priv->cgroup_priv_key = cgroup_priv_getkey(i915_cgroup_free);
	if (dev_priv->cgroup_priv_key < 0) {
		DRM_DEBUG_DRIVER("Failed to get a cgroup private data key\n");
		ret = dev_priv->cgroup_priv_key;
	}

	mutex_init(&dev_priv->cgroup_lock);

	return ret;
}

void
i915_cgroup_shutdown(struct drm_i915_private *dev_priv)
{
	cgroup_priv_destroykey(dev_priv->cgroup_priv_key);
}

/*
 * Return i915 cgroup private data, creating and registering it if one doesn't
 * already exist for this cgroup.
 */
__maybe_unused
static struct i915_cgroup_data *
get_or_create_cgroup_data(struct drm_i915_private *dev_priv,
			  struct cgroup *cgrp)
{
	struct kref *ref;
	struct i915_cgroup_data *priv;

	mutex_lock(&dev_priv->cgroup_lock);

	ref = cgroup_priv_get(cgrp, dev_priv->cgroup_priv_key);
	if (ref) {
		priv = cgrp_ref_to_i915(ref);
	} else {
		priv = kzalloc(sizeof *priv, GFP_KERNEL);
		if (!priv) {
			priv = ERR_PTR(-ENOMEM);
			goto out;
		}

		kref_init(&priv->ref);
		cgroup_priv_install(cgrp, dev_priv->cgroup_priv_key,
				    &priv->ref);
	}

out:
	mutex_unlock(&dev_priv->cgroup_lock);

	return priv;
}

/**
 * i915_cgroup_setparam_ioctl - ioctl to alter i915 settings for a cgroup
 * @dev: DRM device
 * @data: data pointer for the ioctl
 * @file_priv: DRM file handle for the ioctl call
 *
 * Allows i915-specific parameters to be set for a Linux cgroup.
 */
int
i915_cgroup_setparam_ioctl(struct drm_device *dev,
			   void *data,
			   struct drm_file *file)
{
	struct drm_i915_cgroup_param *req = data;
	struct cgroup *cgrp;
	int ret;

	/* We don't actually support any flags yet. */
	if (req->flags) {
		DRM_DEBUG_DRIVER("Invalid flags\n");
		return -EINVAL;
	}

	/*
	 * Make sure the file descriptor really is a cgroup fd and is on the
	 * v2 hierarchy.
	 */
	cgrp = cgroup_get_from_fd(req->cgroup_fd);
	if (IS_ERR(cgrp)) {
		DRM_DEBUG_DRIVER("Invalid cgroup file descriptor\n");
		return PTR_ERR(cgrp);
	}

	/*
	 * Access control:  For now we grant access via CAP_SYS_RESOURCE _or_
	 * DRM master status.
	 */
	if (!capable(CAP_SYS_RESOURCE) && !drm_is_current_master(file)) {
		DRM_DEBUG_DRIVER("Insufficient permissions to adjust i915 cgroup settings\n");
		goto out;
	}

	switch (req->param) {
	default:
		DRM_DEBUG_DRIVER("Invalid cgroup parameter %lld\n", req->param);
		ret = -EINVAL;
	}

out:
	cgroup_put(cgrp);

	return ret;
}
