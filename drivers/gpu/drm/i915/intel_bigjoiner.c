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
