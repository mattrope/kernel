/* SPDX-License-Identifier: MIT */
/*
 * i915_cgroup.c - Linux cgroups integration for i915
 *
 * Copyright (C) 2018 Intel Corporation
 */

#include <linux/cgroup.h>

#include "i915_drv.h"

struct i915_cgroup_data {
	struct cgroup_priv base;

	struct list_head node;
};

static inline struct i915_cgroup_data *
cgrp_to_i915(struct cgroup_priv *priv)
{
	return container_of(priv, struct i915_cgroup_data, base);
}

static void
i915_cgroup_free(struct cgroup_priv *priv)
{
	struct cgroup *cgrp = priv->cgroup;
	struct i915_cgroup_data *ipriv;

	WARN_ON(!mutex_is_locked(&cgrp->privdata_mutex));

	ipriv = cgrp_to_i915(priv);

	/*
	 * Remove private data from both cgroup's hashtable and i915's list.
	 * If this function is being called as a result of cgroup removal
	 * (as opposed to an i915 unload), it will have already been removed from
	 * the hashtable, but the hash_del() call here is still safe.
	 */
	hash_del(&priv->hnode);
	list_del(&ipriv->node);

	kfree(ipriv);
}

void
i915_cgroup_init(struct drm_i915_private *dev_priv)
{
	INIT_LIST_HEAD(&dev_priv->cgroup_list);
}

void
i915_cgroup_shutdown(struct drm_i915_private *dev_priv)
{
	struct i915_cgroup_data *priv, *tmp;
	struct cgroup *cgrp;

	mutex_lock(&cgroup_mutex);

	list_for_each_entry_safe(priv, tmp, &dev_priv->cgroup_list, node) {
		cgrp = priv->base.cgroup;

		mutex_lock(&cgrp->privdata_mutex);
		i915_cgroup_free(&priv->base);
		mutex_unlock(&cgrp->privdata_mutex);
	}

	mutex_unlock(&cgroup_mutex);
}

/*
 * Return i915 cgroup private data, creating and registering it if one doesn't
 * already exist for this cgroup.
 */
static struct i915_cgroup_data *
get_or_create_cgroup_data(struct drm_i915_private *dev_priv,
			  struct cgroup *cgrp)
{
	struct cgroup_priv *priv;
	struct i915_cgroup_data *ipriv;

	mutex_lock(&cgrp->privdata_mutex);

	priv = cgroup_priv_lookup(cgrp, dev_priv);
	if (priv) {
		ipriv = cgrp_to_i915(priv);
	} else {
		ipriv = kzalloc(sizeof *ipriv, GFP_KERNEL);
		if (!ipriv) {
			ipriv = ERR_PTR(-ENOMEM);
			goto out;
		}

		ipriv->base.key = dev_priv;
		ipriv->base.free = i915_cgroup_free;
		list_add(&ipriv->node, &dev_priv->cgroup_list);

		cgroup_priv_install(cgrp, &ipriv->base);
	}

out:
	mutex_unlock(&cgrp->privdata_mutex);

	return ipriv;
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
	 * Access control:  The strategy for using cgroups in a given
	 * environment is generally determined by the system integrator
	 * and/or OS vendor, so the specific policy about who can/can't
	 * manipulate them tends to be domain-specific (and may vary
	 * depending on the location in the cgroup hierarchy).  Rather than
	 * trying to tie permission on this ioctl to a DRM-specific concepts
	 * like DRM master, we'll allow cgroup parameters to be set by any
	 * process that has been granted write access on the cgroup's
	 * virtual file system (i.e., the same permissions that would
	 * generally be needed to update the virtual files provided by
	 * cgroup controllers).
	 */
	ret = cgroup_permission(req->cgroup_fd, MAY_WRITE);
	if (ret)
		goto out;

	switch (req->param) {
	default:
		DRM_DEBUG_DRIVER("Invalid cgroup parameter %lld\n", req->param);
		ret = -EINVAL;
	}

out:
	cgroup_put(cgrp);

	return ret;
}
