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

#include <drm/drmP.h>
#include <drm/drm_cgroup_helper.h>

/**
 * DOC: cgroup helper library
 *
 * This helper library provides implementations for the DRM cgroup parameter
 * entry points.  Most drivers will wish to store driver-specific data
 * associated with individual cgroups; this helper will manage the storage
 * and lookup of these data structures and will ensure that they are properly
 * destroyed when the corresponding cgroup is destroyed.
 *
 * This helper library should be initialized by calling drm_cgrp_helper_init()
 * and torn down (on driver unload) by calling drm_cgrp_helper_shutdown().
 * Drivers wishing to make use of this helper library should subclass the
 * &drm_cgroup_helper_data structure to store values for any driver-specific
 * cgroup parameters and provide implementations of at least
 * &drm_cgroup_helper.alloc_params, &drm_cgroup_helper.update_param, and
 * &drm_cgroup_helper.read_param.
 */

/**
 * drm_cgrp_helper_set_param - set parameter value for cgroup
 * @dev: DRM device
 * @cgrp: cgroup to set parameter for
 * @param: parameter to set
 * @val: value to assign to parameter
 *
 * Provides a default handler for the CGROUP_SETPARAM ioctl.  At this time
 * parameters may only be set on cgroups in the v2 hierarchy.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int
drm_cgrp_helper_set_param(struct drm_device *dev,
			  struct cgroup *cgrp,
			  uint64_t param,
			  int64_t val)
{
	struct drm_cgroup_helper *helper = dev->cgroup_helper;
	struct drm_cgroup_helper_data *data;
	int ret;

	if (WARN_ON(!helper))
		return -EINVAL;

	mutex_lock(&helper->hash_mutex);

	/*
	 * Search for an existing parameter set for this cgroup and update
	 * it if found.
	 */
	hash_for_each_possible(helper->param_hash, data, node, cgrp->id) {
		if (data->cgroup == cgrp) {
			DRM_DEBUG("Updating existing data for cgroup %d\n",
				  cgrp->id);
			ret = helper->update_param(data, param, val);
			goto out;
		}
	}

	/*
	 * Looks like this is the first time we've tried to set a parameter
	 * on this cgroup.  We need to allocate a new parameter storage
	 * structure.  Note that we'll still allocate the structure and
	 * associate it with the cgroup even if the setting of the specific
	 * parameter fails.
	 */
	DRM_DEBUG("Allocating new data for cgroup %d\n", cgrp->id);
	data = helper->alloc_params();
	if (IS_ERR(data)) {
		ret = PTR_ERR(data);
		goto out;
	}

	data->dev = dev;
	data->cgroup = cgrp;
	hash_add(helper->param_hash, &data->node, cgrp->id);

	ret = helper->update_param(data, param, val);

out:
	mutex_unlock(&helper->hash_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_cgrp_helper_set_param);

/**
 * drm_cgrp_helper_get_param - retrieve parameter value for cgroup
 * @dev: DRM device
 * @cgrp: cgroup to set parameter for
 * @param: parameter to retrieve
 * @val: parameter value returned by reference
 *
 * Helper function that drivers may call to lookup a parameter associated
 * with a specific cgroup.
 *
 * RETURNS:
 * If a parameter value is found for this cgroup, returns zero and sets
 * 'val' to the value retrieved.  If no parameters have been explicitly
 * set for this cgroup in the past, returns -EINVAL and does not update
 * 'val.'  If other errors occur, a negative error code will be returned
 * and 'val' will not be modified.
 */
int
drm_cgrp_helper_get_param(struct drm_device *dev,
			  struct cgroup *cgrp,
			  uint64_t param,
			  int64_t *val)
{
	struct drm_cgroup_helper *helper = dev->cgroup_helper;
	struct drm_cgroup_helper_data *data;
	int ret;

	if (WARN_ON(!helper))
		return -EINVAL;

	mutex_lock(&helper->hash_mutex);

	ret = -EINVAL;
	hash_for_each_possible(helper->param_hash, data, node, cgrp->id) {
		if (data->cgroup == cgrp) {
			ret = helper->read_param(data, param, val);
			break;
		}
	}

	mutex_unlock(&helper->hash_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_cgrp_helper_get_param);

/*
 * Notifier callback for cgroup destruction.  Search for any driver-specific
 * data associated with the cgroup and free it.
 */
static int
cgrp_destroyed(struct notifier_block *nb,
	       unsigned long val,
	       void *ptr)
{
	struct cgroup *cgrp = ptr;
	struct drm_cgroup_helper *helper = container_of(nb,
							struct drm_cgroup_helper,
							cgrp_notifier);
	struct drm_cgroup_helper_data *data;
	struct hlist_node *tmp;

	mutex_lock(&helper->hash_mutex);

	hash_for_each_possible_safe(helper->param_hash, data, tmp, node,
				    cgrp->id) {
		if (data->cgroup == cgrp) {
			if (helper->remove_params)
				helper->remove_params(data);
			else
				kfree(data);

			DRM_DEBUG("Destroyed DRM parameters for cgroup %d\n",
				  cgrp->id);

			break;
		}
	}

	mutex_unlock(&helper->hash_mutex);

	return 0;
}

/**
 * drm_cgrp_helper_init - initialize cgroup helper library
 * @dev: DRM device
 *
 * Drivers that wish to make use of the cgroup helper library should
 * call this function during driver load.
 */
void
drm_cgrp_helper_init(struct drm_device *dev,
		     struct drm_cgroup_helper *helper)
{
	dev->cgroup_helper = helper;
	helper->dev = dev;

	hash_init(helper->param_hash);
	mutex_init(&helper->hash_mutex);

	helper->cgrp_notifier.notifier_call = cgrp_destroyed;
	blocking_notifier_chain_register(&cgroup_destroy_notifier_list,
					 &helper->cgrp_notifier);
}
EXPORT_SYMBOL(drm_cgrp_helper_init);

/**
 * drm_cgrp_helper_shutdown - tear down cgroup helper library
 * @helper: cgroup helper structure
 *
 * Drivers making use of the cgroup helper library should call this function
 * when unloaded.
 */
void
drm_cgrp_helper_shutdown(struct drm_cgroup_helper *helper)
{
	struct drm_cgroup_helper_data *data;
	struct hlist_node *tmp;
	int i;

	mutex_lock(&helper->hash_mutex);
	hash_for_each_safe(helper->param_hash, i, tmp, data, node) {
		hash_del(&data->node);
		if (helper->remove_params)
			helper->remove_params(data);
		else
			kfree(data);
	}
	mutex_unlock(&helper->hash_mutex);

	blocking_notifier_chain_unregister(&cgroup_destroy_notifier_list,
					   &helper->cgrp_notifier);
}
EXPORT_SYMBOL(drm_cgrp_helper_shutdown);
