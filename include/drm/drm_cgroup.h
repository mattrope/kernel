/* SPDX-License-Identifier: MIT */
/*
 * drm_cgroup.h - Linux cgroups integration for DRM
 *
 * Copyright (C) 2018 Intel Corporation
 */

#ifndef DRM_CGROUP_H
#define DRM_CGROUP_H

#include <linux/cgroup.h>

struct drm_cgroup_priv {
       struct kref ref;
       struct rcu_head rcu;

       int priority_offset;
       int display_boost;
};

static inline struct drm_cgroup_priv *
cgrp_ref_to_drm(struct kref *ref)
{
       return container_of(ref, struct drm_cgroup_priv, ref);
}

#if IS_ENABLED(CONFIG_CGROUPS)
int drm_cgroup_init(struct drm_device *dev);
void drm_cgroup_shutdown(struct drm_device *dev);
int drm_cgroup_setparam_ioctl(struct drm_device *dev,
			      void *data,
			      struct drm_file *file);
int drm_cgroup_get_current_prio_offset(struct drm_device *dev);
int drm_cgroup_get_current_dispboost(struct drm_device *dev);
#else
int drm_cgroup_init(struct drm_device *dev) { return 0; }
void drm_cgroup_shutdown(struct drm_device *dev) {}
static inline int
drm_cgroup_get_current_prio_offset(struct drm_device *dev)
{
	return 0;
}
static inline int drm_cgroup_get_current_dispboost(struct drm_device *dev)
{
	return dev->cgroup.default_disp_boost;
}
#endif

#endif
