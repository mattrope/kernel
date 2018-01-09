/*
 * Copyright (c) 2018 Intel Corporation
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

#ifndef __DRM_CGROUP_HELPER_H__

#include <linux/cgroup.h>
#include <linux/hashtable.h>

#ifdef CONFIG_CGROUPS

/**
 * struct drm_cgroup_helper_data - storage of cgroup-specific information
 * @dev: DRM device
 * @node: hashtable node
 * @cgroup: individual cgroup that this structure instance is associated with
 *
 * Drivers should subclass this structure and add fields for all parameters
 * that they wish to track on a per-cgroup basis.  The cgroup helper library
 * will allocate a new instance of this structure the first time the
 * CGROUP_SETPARAM ioctl is called for a cgroup and will destroy this structure
 * if the corresponding cgroup is destroyed or if the DRM driver is unloaded.
 */
struct drm_cgroup_helper_data {
	struct drm_device *dev;
	struct hlist_node node;
	struct cgroup *cgroup;
};

/**
 * struct drm_cgroup_helper - main cgroup helper data structure
 * @dev: DRM device
 * @param_hash: hashtable used to store per-cgroup parameter data
 * @hash_mutex: mutex to protect access to hash_mutex
 * @cgrp_notifier: blocking notifier for cgroup destruction
 */
struct drm_cgroup_helper {
	struct drm_device *dev;

	DECLARE_HASHTABLE(param_hash, 5);
	struct mutex hash_mutex;

	struct notifier_block cgrp_notifier;

	/**
	 * @alloc_params:
	 *
	 * Driver callback to allocate driver-specific parameter data
	 * associated with a single cgroup.  This callback will be called if
	 * CGROUP_SETPARAM is issued for a cgroup that does not already have
	 * driver-specific storage allocated.
	 *
	 * This callback is mandatory.
	 *
	 * RETURNS:
	 *
	 * The driver should allocate and return a driver-specific subclass
	 * of drm_cgroup_helper_data.  Returns -ENOMEM on allocation failure.
	 */
	struct drm_cgroup_helper_data *(*alloc_params)(void);

	/**
	 * @update_param:
	 *
	 * Driver callback to update a parameter's value in a specific
	 * cgroup's driver-side storage structure.
	 *
	 * This callback is mandatory.
	 *
	 * RETURNS:
	 *
	 * The driver should return 0 on success or a negative error code
	 * on failure.
	 */
	int (*update_param)(struct drm_cgroup_helper_data *data,
			    uint64_t param, int64_t val);

	/**
	 * @read_param:
	 *
	 * Driver callback to read a parameter's value from a specific
	 * cgroup's driver-side storage structure.  If successful, the
	 * parameter val will be updated with the appropriate value.
	 *
	 * This callback is mandatory.
	 *
	 * RETURNS:
	 *
	 * The driver should return 0 on success or a negative error code
	 * on failure.
	 */
	int (*read_param)(struct drm_cgroup_helper_data *data,
			  uint64_t param, int64_t *val);

	/**
	 * @remove_params:
	 *
	 * Driver callback to reap the driver-specific data structure
	 * after the corresponding cgroup has been removed.
	 *
	 * This callback is optional.  If not provided, the helper library
	 * will call kfree() on the driver-specific structure.
	 */
	void (*remove_params)(struct drm_cgroup_helper_data *data);
};


void drm_cgrp_helper_init(struct drm_device *dev,
			  struct drm_cgroup_helper *helper);
void drm_cgrp_helper_shutdown(struct drm_cgroup_helper *helper);
int drm_cgrp_helper_set_param(struct drm_device *dev,
			      struct cgroup *cgrp,
			      uint64_t param,
			      int64_t val);
int drm_cgrp_helper_get_param(struct drm_device *dev,
			      struct cgroup *cgrp,
			      uint64_t param,
			      int64_t *val);

#else /* CGROUPS */

void drm_cgrp_helper_init(struct drm_device *dev) {}
void drm_cgrp_helper_shutdown(struct drm_device *dev) {}
int drm_cgrp_helper_set_param(struct drm_device *dev,
			      struct cgroup *cgrp,
			      uint64_t param,
			      int64_t val) { return -EINVAL; }
int drm_cgrp_helper_get_param(struct drm_device *dev,
			      struct cgroup *cgrp,
			      uint64_t param,
			      int64_t *val) { return -EINVAL; }

#endif

#endif
