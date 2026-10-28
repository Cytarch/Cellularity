#pragma once

// The interactive camera for the Windows host.
//
// This is separate from the host's message loop and its frame submission so the
// camera contract can be tested without a window, a driver or a session. The
// projection in particular earned its own unit: an earlier version composed it from
// the viewport's own world matrix times a hand-calibrated surface matrix, which
// produced a plausible-looking frame in which the dish was quietly an ellipse. That
// kind of error does not fail, it just looks slightly wrong, so it needs an assertion
// rather than an eye.

#include <array>

class NativeViewport;

namespace cellularity::host::win32 {

// The camera radius, in the same world units the demo fixture uses: the base-cell
// shaders express nucleus and membrane widths in production world units, so the camera
// has to be in that scale for those fixed-size details to stay visible.
//
// It is larger than the dish on purpose, and the camera bounds are why. The viewport
// floors its zoom at one over this radius, so the opening view spans exactly this
// radius either side of the camera, and the bounds let the camera move by whatever is
// left once the dish is accounted for. At the dish's own radius nothing is left: the
// view height equals the dish diameter, the bound evaluates to zero, and a vertical
// drag does nothing in a landscape window while a horizontal one moves. Half a unit
// spans the opening view exactly, so any radius above a quarter leaves slack on both
// axes; half a unit shows the whole dish with room around it and still lets the wheel
// zoom in past the dish.
inline constexpr float kCameraRadius = 0.5F;

// Matches the demo background so the interactive view and the fixture frame are
// visually the same scene.
inline constexpr std::array<float, 3> kBackgroundColor{0.73F, 0.70F, 0.95F};

// Projection matrix for the current camera: the world-to-normalised transform that
// puts the visible world extent exactly across the surface and centres it on the
// camera.
//
// This is built from the visible extent rather than taken from the viewport's own
// world matrix. That matrix normalises each axis by its own extent while taking a
// single scale from the shorter side, so it cannot express a view that keeps the
// world's shape, and it multiplies the surface matrix on the wrong side for a surface
// matrix to correct it. Deriving the matrix from the extent instead makes the two axes
// agree by construction at any window shape, and it reproduces the fixture's own
// projection exactly at the fixture's camera, because the fixture maps the same extent
// the same way.
[[nodiscard]] std::array<float, 16> projectionMatrixFor(
        const NativeViewport& viewport);

} // namespace cellularity::host::win32
