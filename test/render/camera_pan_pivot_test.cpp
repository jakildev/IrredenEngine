#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

// ---------------------------------------------------------------------------
// The CAMERA_CENTER pan invariant (#2547).
//
// `IRMath::cameraMoveRelativeToYaw` pre-compensates a pan so dragging moves
// content parallel to the drag on screen at every yaw. Its derivation assumes
// `getEffectiveCameraIso`'s focus is re-derived from the LIVE `cameraIso` every
// frame — `d effCam / d cameraIso == P(R_z(-yaw) * Pinv(delta))`, exactly what
// the helper inverts. The depth-aware default pivot latches only the iso DEPTH
// and keeps deriving the point live, which preserves that derivative because
// `isoPixelToPos3D`'s depth parameter shifts along (1,1,1) and projects to
// (0,0).
//
// Nothing in the tree guarded that premise: `pivot-verify.py` and
// `jitter_probe --stationary` hold the camera fixed while sweeping yaw, which
// is the one regime where a frozen focus and a live focus are
// indistinguishable. These tests are the guard. They are pure math over the
// IRMath helpers plus the focus expression `RenderManager` uses — no GPU, no
// RenderManager — so they run headless in the normal suite.
// ---------------------------------------------------------------------------

namespace {

using IRMath::vec2;
using IRMath::vec3;

constexpr float kTolerance = 1e-4f;

// An arbitrary non-trivial canvas center; the invariant must not depend on it.
constexpr vec2 kCanvasCenterIso = vec2(311.0f, -47.0f);

// The default-pivot focus expression, mirroring
// `RenderManager::getDefaultRotationPivotFocus`: the point under the viewport
// center at the latched iso depth, with the POINT derived from the live camera.
vec3 liveFocus(const vec2 cameraIso, const float isoDepth) {
    return IRMath::isoPixelToPos3D(kCanvasCenterIso - cameraIso, isoDepth);
}

// `IRRender::getEffectiveCameraIso`'s CAMERA_CENTER branch.
vec2 effectiveCameraIso(const vec2 cameraIso, const vec3 focusWorld, const float visualYaw) {
    return IRMath::cameraYawPivotOffset(cameraIso, focusWorld, visualYaw);
}

// On-screen shift of a fixed world point when the camera iso moves by `delta`:
// content sits at `pos3DtoPos2DIsoYawed(W, yaw) + effCam`, and the first term
// does not depend on the camera, so the shift IS the change in effCam.
vec2 screenShiftUnderLiveFocus(
    const vec2 cameraIso, const vec2 delta, const float isoDepth, const float visualYaw
) {
    const vec2 moved = cameraIso + delta;
    return effectiveCameraIso(moved, liveFocus(moved, isoDepth), visualYaw) -
           effectiveCameraIso(cameraIso, liveFocus(cameraIso, isoDepth), visualYaw);
}

// Yaws to sweep. 2pi/3 is excluded deliberately: the iso projection is
// geometrically degenerate there (det = 1 + 2*cos(yaw) == 0) and
// `cameraMoveRelativeToYaw` documents that it returns its input unchanged, so
// the identity is not expected to hold.
const float kYaws[] = {
    0.0f,
    IRMath::kQuarterPi,
    IRMath::kHalfPi,
    IRMath::kPi,
    -IRMath::kQuarterPi,
    -IRMath::kHalfPi,
    // A yaw that is neither a cardinal nor a quadrant bisector, so the 2x2
    // solve is exercised away from its symmetric cases.
    -IRMath::kPi / 3.0f,
};

// Latched depths: the background/legacy 0, a positive surface depth, and a
// fractional negative one (a raised surface, and not on the integer lattice).
const float kIsoDepths[] = {0.0f, 5.0f, -3.25f};

// ---------------------------------------------------------------------------
// The mechanism: the focus's iso projection is depth-independent.
// ---------------------------------------------------------------------------

TEST(CameraPanPivot, FocusProjectionIsDepthIndependent) {
    const vec2 cameraIso = vec2(-18.5f, 7.25f);
    for (const float isoDepth : kIsoDepths) {
        const vec2 projected = IRMath::pos3DtoPos2DIso(liveFocus(cameraIso, isoDepth));
        // A world point W lands at screen center when P(W) + cameraIso ==
        // canvasCenterIso, so this is what makes any latched depth admissible.
        EXPECT_NEAR(projected.x, (kCanvasCenterIso - cameraIso).x, kTolerance)
            << "isoDepth=" << isoDepth;
        EXPECT_NEAR(projected.y, (kCanvasCenterIso - cameraIso).y, kTolerance)
            << "isoDepth=" << isoDepth;
    }
}

// ---------------------------------------------------------------------------
// The invariant itself: a pre-compensated drag shifts content by exactly the
// requested iso delta, at every yaw and every latched depth.
// ---------------------------------------------------------------------------

TEST(CameraPanPivot, PreCompensatedDragShiftsContentByTheDragAtEveryYawAndDepth) {
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    const vec2 isoDeltas[] = {vec2(10.0f, 0.0f), vec2(0.0f, 10.0f), vec2(-7.5f, 3.25f)};

    for (const float visualYaw : kYaws) {
        for (const float isoDepth : kIsoDepths) {
            for (const vec2 isoDelta : isoDeltas) {
                const vec2 delta = IRMath::cameraMoveRelativeToYaw(isoDelta, visualYaw);
                const vec2 shift = screenShiftUnderLiveFocus(cameraIso, delta, isoDepth, visualYaw);
                EXPECT_NEAR(shift.x, isoDelta.x, kTolerance)
                    << "yaw=" << visualYaw << " isoDepth=" << isoDepth << " isoDelta=("
                    << isoDelta.x << "," << isoDelta.y << ")";
                EXPECT_NEAR(shift.y, isoDelta.y, kTolerance)
                    << "yaw=" << visualYaw << " isoDepth=" << isoDepth << " isoDelta=("
                    << isoDelta.x << "," << isoDelta.y << ")";
            }
        }
    }
}

TEST(CameraPanPivot, PanIdentityIsIndependentOfTheStartingCameraPosition) {
    const vec2 isoDelta = vec2(10.0f, 0.0f);
    const vec2 starts[] = {vec2(0.0f), vec2(64.0f, -12.0f), vec2(-1024.5f, 903.75f)};

    for (const vec2 cameraIso : starts) {
        const vec2 delta = IRMath::cameraMoveRelativeToYaw(isoDelta, IRMath::kHalfPi);
        const vec2 shift = screenShiftUnderLiveFocus(cameraIso, delta, 5.0f, IRMath::kHalfPi);
        EXPECT_NEAR(shift.x, isoDelta.x, kTolerance) << "cameraIso.x=" << cameraIso.x;
        EXPECT_NEAR(shift.y, isoDelta.y, kTolerance) << "cameraIso.x=" << cameraIso.x;
    }
}

// ---------------------------------------------------------------------------
// Negative control — the regression this guards against.
//
// A test that only asserts the live-focus identity would still pass if the
// focus were frozen and the helper silently changed to match. Pinning the
// broken behavior's numbers documents WHICH transform is being relied on: with
// the focus latched as a world point, `d effCam / d cameraIso` is the identity,
// so the pre-compensation passes through undivided.
// ---------------------------------------------------------------------------

TEST(CameraPanPivot, WorldPointLatchBreaksThePanIdentity) {
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    const vec2 isoDelta = vec2(10.0f, 0.0f);
    const float visualYaw = IRMath::kHalfPi;
    const float isoDepth = 5.0f;

    const vec2 delta = IRMath::cameraMoveRelativeToYaw(isoDelta, visualYaw);
    // The focus is derived once and then HELD while the camera moves — the
    // world-point latch.
    const vec3 frozenFocus = liveFocus(cameraIso, isoDepth);
    const vec2 shift = effectiveCameraIso(cameraIso + delta, frozenFocus, visualYaw) -
                       effectiveCameraIso(cameraIso, frozenFocus, visualYaw);

    // At yaw pi/2 the helper's solution for a (10, 0) drag is (20, 30); with a
    // frozen focus that reaches the screen unchanged, i.e. content travels
    // ~3.2x the drag at ~56 degrees off the drag axis, then pops back by the
    // difference the moment the focus re-derives.
    EXPECT_NEAR(shift.x, delta.x, kTolerance);
    EXPECT_NEAR(shift.y, delta.y, kTolerance);
    EXPECT_NEAR(shift.x, 20.0f, kTolerance);
    EXPECT_NEAR(shift.y, 30.0f, kTolerance);
    EXPECT_GT(IRMath::length(shift - isoDelta), 1.0f);
}

// ---------------------------------------------------------------------------
// Cardinal fast path: at yaw 0 the effective camera is the raw camera iso at
// every latched depth, so a depth-aware pivot cannot perturb the byte-identical
// un-yawed path (#2547 acceptance criterion 4).
// ---------------------------------------------------------------------------

TEST(CameraPanPivot, YawZeroReturnsRawCameraIsoAtEveryLatchedDepth) {
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    for (const float isoDepth : kIsoDepths) {
        const vec2 effCam = effectiveCameraIso(cameraIso, liveFocus(cameraIso, isoDepth), 0.0f);
        EXPECT_NEAR(effCam.x, cameraIso.x, kTolerance) << "isoDepth=" << isoDepth;
        EXPECT_NEAR(effCam.y, cameraIso.y, kTolerance) << "isoDepth=" << isoDepth;
    }
}

} // namespace
