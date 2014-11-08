/*
 * Copyright © 2014 Intel Corporation
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

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include "intel_drv.h"

/**
 * intel_plane_duplicate_state - duplicate plane state
 * @plane: drm plane
 *
 * Allocates and returns a copy of the plane state (both common and
 * Intel-specific) for the specified plane.
 */
struct drm_plane_state *
intel_plane_duplicate_state(struct drm_plane *plane)
{
	struct intel_plane_state *state;

	if (plane->state)
		state = kmemdup(plane->state, sizeof(*state), GFP_KERNEL);
	else
		state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (state && state->base.fb)
		drm_framebuffer_reference(state->base.fb);

	return &state->base;
}

/**
 * intel_plane_destroy_state - destroy plane state
 * @plane: drm plane
 *
 * Allocates and returns a copy of the plane state (both common and
 * Intel-specific) for the specified plane.
 */
void
intel_plane_destroy_state(struct drm_plane *plane,
			  struct drm_plane_state *state)
{
	drm_atomic_helper_plane_destroy_state(plane, state);
}


/**
 * intel_crtc_atomic_begin - Begins an atomic commit on a CRTC
 * @crtc: drm crtc
 *
 * Prepares to write registers associated with the atomic commit of a CRTC
 * by using vblank evasion to ensure that all register writes happen within
 * the same vblank period.
 */
void intel_crtc_atomic_begin(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_pipe_update_start(intel_crtc, &intel_crtc->atomic_vbl_count);
}

/**
 * intel_crtc_atomic_flush - Finishes an atomic commit on a CRTC
 * @crtc: drm crtc
 *
 * Concludes the writing of registers for an atomic commit of a CRTC.
 */
void intel_crtc_atomic_flush(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	intel_pipe_update_end(intel_crtc, intel_crtc->atomic_vbl_count);
}

static int intel_prepare_fb(struct drm_plane *plane,
			    struct drm_framebuffer *fb)
{
	struct drm_device *dev = plane->dev;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct drm_i915_gem_object *old_obj = intel_plane->obj;
	enum pipe pipe = intel_plane->pipe;
	unsigned front_bits = 0;
	int ret = 0;

	switch (plane->type) {
	case DRM_PLANE_TYPE_PRIMARY:
		front_bits = INTEL_FRONTBUFFER_PRIMARY(pipe);

		if (plane->crtc) {
			intel_crtc_wait_for_pending_flips(plane->crtc);
			if (intel_crtc_has_pending_flip(plane->crtc)) {
				DRM_ERROR("pipe is still busy with an old pageflip\n");
				return -EBUSY;
			}
		}

		break;
	case DRM_PLANE_TYPE_OVERLAY:
		front_bits = INTEL_FRONTBUFFER_SPRITE(pipe);
		break;
	case DRM_PLANE_TYPE_CURSOR:
		front_bits = INTEL_FRONTBUFFER_CURSOR(pipe);
		break;
	}

	mutex_lock(&dev->struct_mutex);

	/* Note that this will apply the VT-d workaround for scanouts,
	 * which is more restrictive than required for sprites. (The
	 * primary plane requires 256KiB alignment with 64 PTE padding,
	 * the sprite planes only require 128KiB alignment and 32 PTE
	 * padding.
	 */
	ret = intel_pin_and_fence_fb_obj(plane, fb, NULL);
	if (ret == 0)
		i915_gem_track_fb(old_obj, obj, front_bits);

	if (plane->type == DRM_PLANE_TYPE_CURSOR &&
	    INTEL_INFO(dev)->cursor_needs_physical) {
		int align = IS_I830(dev) ? 16 * 1024 : 256;

		ret = i915_gem_object_attach_phys(obj, align);
		if (ret)
			DRM_DEBUG_KMS("failed to attach phys object\n");
	}

	mutex_unlock(&dev->struct_mutex);

	return ret;
}

static void intel_cleanup_fb(struct drm_plane *plane,
			     struct drm_framebuffer *fb)
{
	struct drm_device *dev = plane->dev;
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);

	mutex_lock(&dev->struct_mutex);
	intel_unpin_fb_obj(obj);
	mutex_unlock(&dev->struct_mutex);
}

static int intel_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_crtc *intel_crtc = to_intel_crtc(state->crtc);
	struct intel_plane_state *intel_state = to_intel_plane_state(state);

	/* Disabling a plane is always okay */
	if (state->fb == NULL)
		return 0;

	/*
	 * The original src/dest coordinates are stored in state->base, but
	 * we want to keep another copy internal to our driver that we can
	 * clip/modify ourselves.
	 */
	intel_state->src.x1 = state->src_x;
	intel_state->src.y1 = state->src_y;
	intel_state->src.x2 = state->src_x + state->src_w;
	intel_state->src.y2 = state->src_y + state->src_h;
	intel_state->dst.x1 = state->crtc_x;
	intel_state->dst.y1 = state->crtc_y;
	intel_state->dst.x2 = state->crtc_x + state->crtc_w;
	intel_state->dst.y2 = state->crtc_y + state->crtc_h;

	/* Clip all planes to CRTC size, or 0x0 if CRTC is disabled */
	if (intel_crtc) {
		intel_state->clip.x1 = 0;
		intel_state->clip.y1 = 0;
		intel_state->clip.x2 =
			intel_crtc->active ? intel_crtc->config.pipe_src_w : 0;
		intel_state->clip.y2 =
			intel_crtc->active ? intel_crtc->config.pipe_src_h : 0;
	}

	return intel_plane->check_plane(plane, intel_state);
}

static void intel_plane_atomic_update(struct drm_plane *plane)
{
	struct intel_plane *intel_plane = to_intel_plane(plane);
	struct intel_plane_state *intel_state =
		to_intel_plane_state(plane->state);

	if (!plane->state->fb)
		intel_plane_disable(plane);
	else
		intel_plane->commit_plane(plane, intel_state);
}

const struct drm_plane_helper_funcs intel_plane_helper_funcs = {
	.prepare_fb = intel_prepare_fb,
	.cleanup_fb = intel_cleanup_fb,
	.atomic_check = intel_plane_atomic_check,
	.atomic_update = intel_plane_atomic_update,
};

