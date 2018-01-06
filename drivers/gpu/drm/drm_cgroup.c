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
#include <drm/drm_cgroup.h>

/**
 * DOC: cgroup handling
 *
 * cgroups are a core kernel mechanism for organizing OS processes into logical
 * groupings to which policy configuration or resource management may be
 * applied.  Some DRM drivers may control resources or have policy settings
 * that a system integrator would wish to configure according to the system
 * cgroups hierarchy.  To support such use cases, the DRM framework allows
 * drivers to track 'parameters' on a per-cgroup basis.  Parameters are a (u64
 * key, s64 value) pair which would generally be set on specific cgroups
 * during system configuration (e.g., by a sysv init script or systemd service)
 * and then used by the driver at runtime to manage GPU-specific resources or
 * control driver-specific policy.
 */

/**
 * drm_cgroup_setparam_ioctl
 * @dev: DRM device
 * @data: data pointer for the ioctl
 * @file_priv: DRM file handle for the ioctl call
 *
 * Set DRM-specific parameters for a cgroup
 */
int
drm_cgroup_setparam_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_cgroup_setparam *req = data;
	struct cgroup *cgrp;
	struct file *f;
	struct inode *inode = NULL;
	int ret;

	/*
	 * We'll let drivers create their own parameters with ID's from
	 * 0-0xFFFFFF.  We'll reserve parameter ID's above that range for
	 * anything the DRM core wants to control directly; today we don't
	 * have any such core-managed parameters, so just reject attempts
	 * to set a cgroup parameter in this range.
	 */
	if (req->param > DRM_CGROUP_MAX_DRIVER_PARAM) {
		DRM_DEBUG("Invalid cgroup parameter ID\n");
		return -EINVAL;
	}

	if (!dev->cgroup) {
		DRM_DEBUG("Invalid cgroup parameter ID\n");
		return -EINVAL;
	}

	/* Make sure the file descriptor really is a cgroup fd */
	cgrp = cgroup_get_from_fd(req->cgroup_fd);
	if (IS_ERR(cgrp)) {
		DRM_DEBUG("Invalid cgroup file descriptor\n");
		return PTR_ERR(cgrp);
	}

	/*
	 * Restrict this functionality to cgroups on the cgroups-v2
	 * (i.e., default) hierarchy.
	 */
	if (!cgroup_on_dfl(cgrp)) {
		DRM_DEBUG("setparam only supported on cgroup-v2 hierarchy\n");
		return -EINVAL;
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
	f = fget_raw(req->cgroup_fd);
	if (WARN_ON(!f))
		return -EBADF;

	inode = kernfs_get_inode(f->f_path.dentry->d_sb, cgrp->kn);
	if (inode)
		ret = inode_permission(inode, MAY_WRITE);
	else
		ret = -ENOMEM;

	iput(inode);
	fput(f);

	return ret ?: dev->cgroup->set_param(dev, cgrp, req->param,
					     req->value);
}
