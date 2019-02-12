/*
 * Copyright © 2019 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * DOC: big joiner support
 *
 * The functions here enable use of the "big joiner" which was introduced with
 * gen11 hardware.  The big joiner allows the post-DSC output of two pipes
 * to be combined as input to a single transcoder+DDI.  This ganging of two
 * CRTCs allows us to support modes that exceed the clock or resolution
 * capabilities of a single pipe.
 *
 * Use of the big joiner must remain transparent to userspace.  When userspace
 * requests a large mode on a single CRTC, the driver will transparently
 * program the registers for a second "slave" CRTC; none of this
 * behind-the-scenes programming should be reflected in any way via the slave
 * CRTC's properties.
 *
 * The hardware limits which CRTCs may be used as master or slave for a big
 * joiner configuration.  If userspace requests a large mode that can only
 * be satisfied via the big joiner, but the potential slave CRTC is already
 * in use driving a different display, the configuration cannot be supported
 * and the atomic transaction should be rejected.  Similarly, if we successfully
 * setup a big joiner configuration, but a subsequent atomic request from
 * userspace starts trying to directly use the CRTC that i915 is using as a
 * big joiner slave, that request will have to be rejected (current platforms
 * only have a single potential slave CRTC, so there's no opportunity to
 * migrate our slave responsibilities to a different unused CRTC).
 */

#include "intel_drv.h"

/**
 * intel_bigjoiner_possible - determines whether bigjoiner can be used
 * @crtc_state: state for master CRTC that userspace wishes to update
 *
 * Returns %true if hardware supports big joiner usage, %false otherwise.
 */
bool
intel_bigjoiner_possible(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	enum pipe pipe = to_intel_crtc(crtc_state->base.crtc)->pipe;

	/*
	 * Current hardware is pretty simple; the only possible setup is
	 * pipes B(master) + C(slave).
	 *
	 * For simplicity, we'll only allow big-joiner modes when userspace
	 * requests them on pipe B (the master).  We could potentially allow
	 * userspace to make these requests on pipe C as well (assuming pipe B
	 * is inactive), but that would require more internal logic shuffling
	 * so let's just keep things simple for now.  We can revisit this
	 * decision in the future once we're sure the basic logic and
	 * functionality is working as expected.
	 */
	return INTEL_GEN(dev_priv) >= 11 && pipe == PIPE_B;
}

/**
 * intel_bigjoiner_master - determine master crtc for given slave
 * @slave: big joiner slave CRTC
 *
 * Returns a pointer to @slave's big joiner master crtc partner.
 */
struct intel_crtc *
intel_bigjoiner_master(struct intel_crtc *slave)
{
	struct drm_i915_private *dev_priv = to_i915(slave->base.dev);

	/*
	 * If we start allowing userspace to request big-joiner modes on
	 * CRTC C, we'd need to handle that here.  But for now, master B +
	 * slave C is * the only valid big joiner configuration.
	 */
	return slave->pipe == PIPE_C ?
		dev_priv->pipe_to_crtc_mapping[PIPE_B] : NULL;
}

/**
 * intel_bigjoiner_slave - determine slave crtc for given master
 * @master: big joiner master CRTC
 *
 * Returns a pointer to @master's big joiner slave crtc partner.
 */
struct intel_crtc *
intel_bigjoiner_slave(struct intel_crtc *master)
{
	struct drm_i915_private *dev_priv = to_i915(master->base.dev);

	/*
	 * If we start allowing userspace to request big-joiner modes on
	 * CRTC C, we'd need to handle that here.  But for now, master B +
	 * slave C is * the only valid big joiner configuration.
	 */
	return master->pipe == PIPE_B ?
		dev_priv->pipe_to_crtc_mapping[PIPE_C] : NULL;
}

/**
 * i915_adjust_bigjoiner_planes - adjust plane rectangles for big joiner
 * @master_state: CRTC state for gang master
 * @slave_state: CRTC state for gang slave
 *
 * When using a big joiner mode serviced by two CRTC's, userspace has requested
 * a single set of plane configurations based on the uapi mode.  We need to
 * grab corresponding planes on the slave CRTC and adjust the coordinates and
 * offsets of the planes on both CRTC's to display the proper subset of content.
 * The master CRTC will display the left half of the uapi mode and the
 * slave CRTC to display the right half of the uapi mode.
 *
 * This function needs to be called before intel_plane_atomic_check so that we
 * can divide up the planes before the regular CRTC clipping happens on the
 * userspace-provided source/dest rectangles.
 *
 * Note that this function only updates the driver-internal plane rectangles;
 * it does not change any of the other plane state.  In fact it's very
 * important that we *not* touch any state fields that would be exposed through
 * the uapi since userspace should not know that we're using extra planes/pipes
 * behind its back.
 *
 * Returns 0 on success, negative errno on failure.
 */
int i915_adjust_bigjoiner_planes(struct intel_crtc_state *master_state,
				 struct intel_crtc_state *slave_state)
{
	struct intel_atomic_state *state =
		to_intel_atomic_state(master_state->base.state);
	struct intel_plane *plane;
	struct intel_plane_state *plane_state;
	struct intel_plane_state *master_plane_states[I915_MAX_PLANES] = { 0 };
	struct intel_plane_state *slave_plane_states[I915_MAX_PLANES] = { 0 };
	struct drm_rect master_area = { 0 }, slave_area = { 0 };
	int i;
	u16 id_mask = 0;

	WARN_ON(master_state->bigjoiner_mode != I915_BIGJOINER_MASTER);
	WARN_ON(slave_state->bigjoiner_mode != I915_BIGJOINER_SLAVE);

	/*
	 * Define viewports of the original userspace mode each CRTC is
	 * responsible for.
	 */
	master_area.x2 = master_state->pipe_src_w;
	master_area.y2 = master_state->pipe_src_h;
	slave_area = master_area;
	slave_area.x1 += master_state->pipe_src_w;
	slave_area.x2 += master_state->pipe_src_h;

	/* Figure out which planes on the slave CRTC we need to grab */
	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		if (plane->pipe != to_intel_crtc(master_state->base.crtc)->pipe)
			continue;

		id_mask |= BIT(plane->id);
		master_plane_states[plane->id] = plane_state;
	}

	/* Grab all the same planes on the slave CRTC and copy the rects */
	for_each_intel_plane_on_crtc_mask(state->base.dev,
					  to_intel_crtc(slave_state->base.crtc),
					  id_mask, plane) {
		struct intel_plane_state *ps =
			intel_atomic_get_plane_state(state, plane);

		if (IS_ERR(ps))
			return PTR_ERR(ps);

		ps->base.src = master_plane_states[plane->id]->base.src;
		ps->base.dst = master_plane_states[plane->id]->base.dst;

		slave_plane_states[plane->id] = ps;
	}

	/* Clip/translate viewports for both CRTC's */
	for_each_planeid_masked(state->base.dev, id_mask, i) {
		master_plane_states[i]->base.visible =
			drm_rect_clip_scaled(&master_plane_states[i]->base.src,
					     &master_plane_states[i]->base.dst,
					     &master_area);
		slave_plane_states[i]->base.visible =
			drm_rect_clip_scaled(&slave_plane_states[i]->base.src,
					     &slave_plane_states[i]->base.dst,
					     &slave_area);

		/*
		 * We need to translate plane destination coordinates on slave
		 * CRTC so that they fall within the CRTC's viewport rather
		 * than inside the larger uapi mode.
		 */
		slave_plane_states[i]->base.dst.x1 -= master_area.x2;
		slave_plane_states[i]->base.dst.x2 -= master_area.x2;
	}

	return 0;
}

