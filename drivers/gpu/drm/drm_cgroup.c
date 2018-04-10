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

       dev->cgroup_priv_key = cgroup_priv_createkey(drm_cgroup_free);
       if (dev->cgroup_priv_key < 0) {
               DRM_DEBUG("Failed to obtain cgroup private data key\n");
               ret = dev->cgroup_priv_key;
       }

       mutex_init(&dev->cgroup_lock);

       return ret;
}
EXPORT_SYMBOL(drm_cgroup_init);

void
drm_cgroup_shutdown(struct drm_device *dev)
{
       cgroup_priv_destroykey(dev->cgroup_priv_key);
}
EXPORT_SYMBOL(drm_cgroup_shutdown);

/*
 * Return drm cgroup private data, creating and registering it if one doesn't
 * already exist for this cgroup.
 */
__maybe_unused
static struct drm_cgroup_priv *
get_or_create_cgroup_data(struct drm_device *dev,
                         struct cgroup *cgrp)
{
       struct kref *ref;
       struct drm_cgroup_priv *priv;

       mutex_lock(&dev->cgroup_lock);

       ref = cgroup_priv_get(cgrp, dev->cgroup_priv_key);
       if (ref) {
               priv = cgrp_ref_to_drm(ref);
       } else {
               priv = kzalloc(sizeof(*priv), GFP_KERNEL);
               if (!priv) {
                       priv = ERR_PTR(-ENOMEM);
                       goto out;
               }

               kref_init(&priv->ref);
               cgroup_priv_install(cgrp, dev->cgroup_priv_key,
                                   &priv->ref);
       }

out:
       mutex_unlock(&dev->cgroup_lock);

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
       struct cgroup *cgrp;
       int ret;

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

       switch (req->param) {
       default:
               DRM_DEBUG("Invalid cgroup parameter %lld\n", req->param);
               ret = -EINVAL;
       }

out:
       cgroup_put(cgrp);

	return ret;
}
