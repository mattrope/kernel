/* SPDX-License-Identifier: MIT */
/*
 * drm_cgroup.c - Linux cgroups integration for DRM
 *
 * Copyright (C) 2018 Intel Corporation
 */

#include <drm/drmP.h>
#include <drm/drm_auth.h>
#include <drm/drm_cgroup.h>

static void
drm_cgroup_free(struct kref *ref)
{
       struct drm_cgroup_priv *priv;

       priv = cgrp_ref_to_drm(ref);
       kfree_rcu(priv, rcu);
}

int
drm_cgroup_init(struct drm_device *dev)
{
       int ret = 0;

       dev->cgroup.priv_key = cgroup_priv_createkey(drm_cgroup_free);
       if (dev->cgroup.priv_key < 0) {
               DRM_DEBUG("Failed to obtain cgroup private data key\n");
               ret = dev->cgroup.priv_key;
       }

       mutex_init(&dev->cgroup.lock);

       return ret;
}
EXPORT_SYMBOL(drm_cgroup_init);

void
drm_cgroup_shutdown(struct drm_device *dev)
{
       cgroup_priv_destroykey(dev->cgroup.priv_key);
}
EXPORT_SYMBOL(drm_cgroup_shutdown);

/*
 * Return drm cgroup private data, creating and registering it if one doesn't
 * already exist for this cgroup.
 */
static struct drm_cgroup_priv *
get_or_create_cgroup_data(struct drm_device *dev,
                         struct cgroup *cgrp)
{
       struct kref *ref;
       struct drm_cgroup_priv *priv;

       mutex_lock(&dev->cgroup.lock);

       ref = cgroup_priv_get(cgrp, dev->cgroup.priv_key);
       if (ref) {
               priv = cgrp_ref_to_drm(ref);
       } else {
               priv = kzalloc(sizeof(*priv), GFP_KERNEL);
               if (!priv) {
                       priv = ERR_PTR(-ENOMEM);
                       goto out;
               }

	       priv->display_boost = dev->cgroup.default_dispboost;

               kref_init(&priv->ref);
               cgroup_priv_install(cgrp, dev->cgroup.priv_key,
                                   &priv->ref);
       }

out:
       mutex_unlock(&dev->cgroup.lock);

       return priv;
}

/**
 * drm_cgroup_setparam_ioctl - ioctl to alter drm settings for a cgroup
 * @dev: DRM device
 * @data: data pointer for the ioctl
 * @file_priv: DRM file handle for the ioctl call
 *
 * Allows drm-specific parameters to be set for a Linux cgroup.
 */
int
drm_cgroup_setparam_ioctl(struct drm_device *dev,
                          void *data,
                          struct drm_file *file)
{
       struct drm_cgroup_param *req = data;
       struct drm_cgroup_priv *priv;
       struct cgroup *cgrp;
       int ret = 0;

       /* We don't actually support any flags yet. */
       if (req->flags) {
               DRM_DEBUG("Invalid flags\n");
               return -EINVAL;
       }

       /*
        * Make sure the file descriptor really is a cgroup fd and is on the
        * v2 hierarchy.
        */
       cgrp = cgroup_get_from_fd(req->cgroup_fd);
       if (IS_ERR(cgrp)) {
               DRM_DEBUG("Invalid cgroup file descriptor\n");
               return PTR_ERR(cgrp);
       }

       /*
        * Access control:  For now we grant access via CAP_SYS_RESOURCE _or_
        * DRM master status.
        */
       if (!capable(CAP_SYS_RESOURCE) && !drm_is_current_master(file)) {
               DRM_DEBUG("Insufficient permissions to adjust cgroups\n");
               goto out;
       }

       priv = get_or_create_cgroup_data(dev, cgrp);
       if (IS_ERR(priv)) {
	       ret = PTR_ERR(priv);
	       goto out;
       }

       switch (req->param) {
       case DRM_CGROUP_PARAM_PRIORITY_OFFSET:
	       if (!dev->cgroup.has_prio_offset) {
		       DRM_DEBUG_DRIVER("Driver does not honor priority offsets\n");
		       ret = -EINVAL;
	       } else if (req->value < dev->cgroup.min_prio_offset ||
			  req->value > dev->cgroup.max_prio_offset) {
		       DRM_DEBUG_DRIVER("Priority offset %lld not within driver supported range [%d,%d]\n",
					req->value,
					dev->cgroup.min_prio_offset,
					dev->cgroup.max_prio_offset);
		       ret = -EINVAL;
	       } else {
		       DRM_DEBUG_DRIVER("Setting cgroup priority offset to %lld\n",
					req->value);
		       priv->priority_offset = req->value;
	       }
	       break;

       case DRM_CGROUP_PARAM_DISPBOOST_PRIORITY:
	       if (!dev->cgroup.has_dispboost) {
		       DRM_DEBUG_DRIVER("Driver does not honor display boost\n");
		       ret = -EINVAL;
	       } else if (req->value > dev->cgroup.max_dispboost) {
		       DRM_DEBUG_DRIVER("Display boost %lld exceeds max allowed by driver (%d)\n",
					req->value,
					dev->cgroup.max_dispboost);
		       ret = -EINVAL;
	       } else {
		       DRM_DEBUG_DRIVER("Setting cgroup display boost priority to %lld\n",
					req->value);
		       priv->display_boost = req->value;
	       }
	       break;

       default:
               DRM_DEBUG("Invalid cgroup parameter %lld\n", req->param);
               ret = -EINVAL;
       }

       kref_put(&priv->ref, drm_cgroup_free);

out:
       cgroup_put(cgrp);

       return ret;
}

/*
 * Generator for simple getter functions that look up a cgroup private data
 * field for the current task's cgroup.  It's safe to call these before
 * a cgroup private data key has been registered; they'll just return the
 * default value in that case.
 */
#define CGROUP_GET(name, field, def) \
int drm_cgroup_get_current_##name(struct drm_device *dev)		\
{									\
	struct kref *ref;						\
	int val = def;							\
	if (!dev->cgroup.priv_key)					\
		return val;						\
	ref = cgroup_priv_get_current(dev->cgroup.priv_key);		\
	if (ref) {							\
		val = cgrp_ref_to_drm(ref)->field;			\
		kref_put(ref, drm_cgroup_free);				\
	}								\
	return val;							\
}									\
EXPORT_SYMBOL(drm_cgroup_get_current_##name);

CGROUP_GET(prio_offset, priority_offset, 0)
CGROUP_GET(dispboost, display_boost, dev->cgroup.default_dispboost)

#undef CGROUP_GET
