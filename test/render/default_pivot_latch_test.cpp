#include <gtest/gtest.h>

#include <irreden/render/default_pivot_latch.hpp>

// ---------------------------------------------------------------------------
// The default pivot's latch-UPDATE policy.
//
// `docs/design/camera-yaw-pivot.md` §"The contract" says when the depth-aware
// default pivot may re-latch: while the camera is settled and pan/zoom moved
// since the last derive, AND on the rotation-start edge — the first frame yaw
// changes, whose previous frame was still (so it left a valid depth
// attachment) and rendered at yaw 0 (so the depth it left pins the point under
// the crosshair). A rotation starting from non-zero yaw holds the latch.
//
// Nothing in the tree could see that. Every `scripts/pivot-verify.py` block —
// including P4's `cursor-latch` — holds the camera pan/zoom FIXED across its
// shots and sweeps yaw from a single derive, which is the one regime where the
// pan/zoom-only and amended policies agree; the harness even flags a block
// that moves the view as MISCONFIGURED. So the guard the ruling requires
// ("verification must move the camera between derives") is this file:
// `DefaultPivotLatch` is the policy with the GPU readback lifted out, so a
// frame sequence that pans, then rotates, then rotates again runs headlessly
// in the normal suite.
//
// The one-frame lag is modelled explicitly. `beginFrame` runs ahead of the
// RENDER pipeline, so a derive at frame N consumes the attachment frame N-1
// rendered — `LatchDriver::step` hands the derive the PREVIOUS frame's depth,
// which is what makes post-rotate staleness observable at all.
// ---------------------------------------------------------------------------

namespace {

using IRMath::vec2;
using IRRender::DefaultPivotLatch;
using IRRender::DefaultPivotLatchDecision;
using IRRender::DefaultPivotPose;

constexpr vec2 kZoom = vec2(4.0f);
constexpr vec2 kZoomedIn = vec2(8.0f);
// Comfortably above kYawSettleDelta: one frame of a real rotation, not a
// residual.
constexpr float kYawStep = 0.05f;
constexpr float kYawSettleDelta = DefaultPivotLatch::kYawSettleDelta;

// Iso depths standing for distinct content under the crosshair. Each is a
// distinguishable value, so an assertion on the latched depth names WHICH
// frame's attachment was consumed rather than merely that something changed.
constexpr float kDepthStart = 5.0f;
constexpr float kDepthAfterPan = 17.0f;
constexpr float kDepthAfterFirstRotation = -3.25f;

class LatchDriver {
  public:
    // Advance one frame. The camera renders `pose`, and the content under the
    // crosshair AT THAT POSE has iso depth `depthUnderCrosshair`. A derive
    // admitted this frame consumes the previous frame's attachment.
    DefaultPivotLatchDecision step(const DefaultPivotPose &pose, float depthUnderCrosshair) {
        const DefaultPivotLatchDecision decision = m_latch.observeFrame(pose, m_pivotOwnsDepth);
        if (decision.derive_) {
            m_latch.noteDerived(m_attachmentDepth);
            ++m_derives;
            m_rotationStartDerives += decision.rotationStart_ ? 1 : 0;
        }
        m_attachmentDepth = depthUnderCrosshair;
        return decision;
    }

    // Hold the camera still for `frames` frames, rendering the same content.
    void hold(const DefaultPivotPose &pose, float depthUnderCrosshair, int frames) {
        for (int i = 0; i < frames; ++i) {
            step(pose, depthUnderCrosshair);
        }
    }

    void setPivotOwnsDepth(bool owns) {
        m_pivotOwnsDepth = owns;
    }
    float isoDepth() const {
        return m_latch.isoDepth();
    }
    bool hasIsoDepth() const {
        return m_latch.hasIsoDepth();
    }
    int derives() const {
        return m_derives;
    }
    int rotationStartDerives() const {
        return m_rotationStartDerives;
    }

  private:
    DefaultPivotLatch m_latch;
    bool m_pivotOwnsDepth = true;
    float m_attachmentDepth = 0.0f;
    int m_derives = 0;
    int m_rotationStartDerives = 0;
};

DefaultPivotPose pose(float yaw, vec2 cameraIso, vec2 zoom = kZoom) {
    return DefaultPivotPose{yaw, cameraIso, zoom};
}

// ---------------------------------------------------------------------------
// The pan/zoom clause, which the rotation-start edge sits beside: a settled
// camera derives once per pan/zoom, one frame late, and a still camera pays
// nothing.
// ---------------------------------------------------------------------------

TEST(DefaultPivotLatch, FirstFrameCannotDeriveAndASettledCameraDerivesExactlyOnce) {
    LatchDriver driver;
    const DefaultPivotPose still = pose(0.0f, vec2(0.0f));

    // Frame 1 has no previous render to read: the zoom sentinel (0,0) makes the
    // pose check fail by construction.
    EXPECT_FALSE(driver.step(still, kDepthStart).derive_);
    EXPECT_FALSE(driver.hasIsoDepth());

    // Frame 2's attachment belongs to frame 1, which rendered this same pose.
    const DefaultPivotLatchDecision second = driver.step(still, kDepthStart);
    EXPECT_TRUE(second.derive_);
    EXPECT_TRUE(second.viewMoved_);
    EXPECT_FALSE(second.rotationStart_);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);

    // A genuinely still camera does ZERO further readbacks.
    driver.hold(still, kDepthStart, 30);
    EXPECT_EQ(driver.derives(), 1);
}

TEST(DefaultPivotLatch, PanDefersItsDeriveToTheFirstStillFrameAfterIt) {
    LatchDriver driver;
    const DefaultPivotPose before = pose(0.0f, vec2(0.0f));
    const DefaultPivotPose after = pose(0.0f, vec2(64.0f, -12.0f));
    driver.hold(before, kDepthStart, 4);
    ASSERT_EQ(driver.derives(), 1);

    // The frame the pan lands: the attachment is the PRE-pan image, so
    // deriving here would latch a depth for a view that no longer exists.
    EXPECT_FALSE(driver.step(after, kDepthAfterPan).derive_);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);

    // The first still frame after it derives, and reads the panned content.
    EXPECT_TRUE(driver.step(after, kDepthAfterPan).derive_);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
    EXPECT_EQ(driver.rotationStartDerives(), 0);
}

TEST(DefaultPivotLatch, ZoomIsKeyedExactlyLikePan) {
    LatchDriver driver;
    driver.hold(pose(0.0f, vec2(0.0f)), kDepthStart, 4);
    ASSERT_EQ(driver.derives(), 1);

    const DefaultPivotPose zoomed = pose(0.0f, vec2(0.0f), kZoomedIn);
    EXPECT_FALSE(driver.step(zoomed, kDepthAfterPan).derive_);
    EXPECT_TRUE(driver.step(zoomed, kDepthAfterPan).derive_);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
}

// ---------------------------------------------------------------------------
// The rotation-start edge, where it fires: a rotation starting from yaw 0.
// ---------------------------------------------------------------------------

TEST(DefaultPivotLatch, RotationStartRederivesAfterAPanThatNeverSettled) {
    // The criterion-4 sequence in its sharpest form: pan, then rotate with NO
    // still frame between them. The pan/zoom clause cannot fire (the pan
    // frame's attachment is pre-pan, and the rotation frames are not settled),
    // so under the pan/zoom-only policy the entire rotation pivots about the
    // PRE-PAN depth.
    LatchDriver driver;
    const DefaultPivotPose before = pose(0.0f, vec2(0.0f));
    driver.hold(before, kDepthStart, 4);
    ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthStart);

    const DefaultPivotPose panned = pose(0.0f, vec2(64.0f, -12.0f));
    ASSERT_FALSE(driver.step(panned, kDepthAfterPan).derive_);

    // Yaw starts moving on the very next frame. Its previous frame was still
    // and at yaw 0, so the attachment is the panned view and the derive is
    // sound.
    const DefaultPivotLatchDecision start =
        driver.step(pose(kYawStep, panned.cameraIso_), kDepthAfterPan);
    EXPECT_TRUE(start.derive_);
    EXPECT_TRUE(start.rotationStart_);
    EXPECT_FALSE(start.viewMoved_);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
}

TEST(DefaultPivotLatch, RotationStartRederivesAfterAPanThatSettled) {
    // The criterion as literally worded — pan, settle, rotate. Here the
    // pan/zoom clause has already refreshed the depth, so the value is
    // unchanged; the assertion that carries the rotation-start clause is that
    // the derive FIRED, and fired through that clause.
    LatchDriver driver;
    driver.hold(pose(0.0f, vec2(0.0f)), kDepthStart, 3);
    const DefaultPivotPose panned = pose(0.0f, vec2(-31.0f, 8.5f));
    driver.hold(panned, kDepthAfterPan, 3);
    ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
    const int derivesBeforeRotation = driver.derives();

    const DefaultPivotLatchDecision start =
        driver.step(pose(kYawStep, panned.cameraIso_), kDepthAfterPan);
    EXPECT_TRUE(start.rotationStart_);
    EXPECT_EQ(driver.derives(), derivesBeforeRotation + 1);
}

TEST(DefaultPivotLatch, ContinuousRotationDerivesOnceAtItsStartAndNeverPerFrame) {
    // Gesture-start only. A per-frame derive would pay a full GPU flush every
    // frame of every rotation AND chase the pivot across the content it is
    // pinning.
    LatchDriver driver;
    const vec2 cameraIso = vec2(12.0f, 3.0f);
    driver.hold(pose(0.0f, cameraIso), kDepthStart, 3);
    const int before = driver.derives();

    float yaw = 0.0f;
    for (int frame = 0; frame < 24; ++frame) {
        yaw += kYawStep;
        driver.step(pose(yaw, cameraIso), kDepthAfterFirstRotation);
    }
    EXPECT_EQ(driver.derives(), before + 1);
    EXPECT_EQ(driver.rotationStartDerives(), 1);

    // Settling out of the rotation is not a pan or a zoom, so it derives
    // nothing either — the depth latched at the start pins the whole gesture.
    driver.hold(pose(yaw, cameraIso), kDepthAfterFirstRotation, 10);
    EXPECT_EQ(driver.derives(), before + 1);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
}

TEST(DefaultPivotLatch, RotationStartFromYawWithinTheSettleToleranceDerives) {
    // The yaw-0 gate is a tolerance, not an exact-zero compare: a pre-rotation
    // yaw the settle predicate itself cannot tell from 0 counts as 0.
    LatchDriver driver;
    const vec2 cameraIso = vec2(3.0f, -9.0f);
    driver.hold(pose(0.0f, cameraIso), kDepthStart, 3);
    // Drifting to half the tolerance is under the settle delta, so the camera
    // is still "settled" and this costs nothing.
    const float nearZero = 0.5f * kYawSettleDelta;
    driver.hold(pose(nearZero, cameraIso), kDepthAfterPan, 3);
    const int before = driver.derives();
    ASSERT_EQ(driver.rotationStartDerives(), 0);

    const DefaultPivotLatchDecision start =
        driver.step(pose(nearZero + kYawStep, cameraIso), kDepthAfterPan);
    EXPECT_TRUE(start.derive_);
    EXPECT_TRUE(start.rotationStart_);
    EXPECT_EQ(driver.derives(), before + 1);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
}

// ---------------------------------------------------------------------------
// The rotation-start edge, where it holds: a rotation starting from non-zero
// yaw. The focus expression has no yaw term, so a depth read off a frame
// rendered at non-zero yaw would pin a point other than the one under the
// crosshair and walk the pivot on every gesture; the latch holds instead, and
// the pre-rotation depth pins the gesture as it did under the pan/zoom-only
// policy.
// ---------------------------------------------------------------------------

TEST(DefaultPivotLatch, RotationStartFromYawOutsideTheSettleToleranceHolds) {
    // The boundary's other side: a pre-rotation yaw just past the tolerance
    // is non-zero, and the edge does not fire.
    LatchDriver driver;
    const vec2 cameraIso = vec2(3.0f, -9.0f);
    driver.hold(pose(0.0f, cameraIso), kDepthStart, 3);
    // Jumping past the tolerance in one frame is itself a rotation start from
    // yaw 0, which re-reads the still yaw-0 attachment — the same depth.
    const float justOffZero = 2.0f * kYawSettleDelta;
    driver.hold(pose(justOffZero, cameraIso), kDepthAfterPan, 3);
    ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
    const int before = driver.derives();

    const DefaultPivotLatchDecision start =
        driver.step(pose(justOffZero + kYawStep, cameraIso), kDepthAfterPan);
    EXPECT_FALSE(start.derive_);
    EXPECT_FALSE(start.rotationStart_);
    EXPECT_EQ(driver.derives(), before);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
}

TEST(DefaultPivotLatch, SecondRotationFromNonZeroYawHoldsTheLatch) {
    // A rotation changes what sits under the crosshair but changes neither
    // cameraIso nor zoom, so the SECOND rotation and every one after it pivot
    // about the depth the first one started from until the user pans or zooms.
    // That post-rotate staleness is the accepted residual of the yaw-0-scoped
    // edge (docs/design/camera-yaw-pivot.md §"The contract"): the alternative,
    // re-deriving here, reads a non-zero-yaw frame and walks the pivot.
    LatchDriver driver;
    const vec2 cameraIso = vec2(-7.0f, 21.0f);
    driver.hold(pose(0.0f, cameraIso), kDepthStart, 3);
    ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthStart);

    float yaw = 0.0f;
    for (int frame = 0; frame < 6; ++frame) {
        yaw += kYawStep;
        driver.step(pose(yaw, cameraIso), kDepthStart);
    }
    // The first rotation pinned the pre-rotation content, as it must.
    ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthStart);

    // It settles at a new yaw, where DIFFERENT content sits under the crosshair.
    driver.hold(pose(yaw, cameraIso), kDepthAfterFirstRotation, 4);
    const int before = driver.derives();

    const DefaultPivotLatchDecision secondStart =
        driver.step(pose(yaw + kYawStep, cameraIso), kDepthAfterFirstRotation);
    EXPECT_FALSE(secondStart.derive_);
    EXPECT_FALSE(secondStart.rotationStart_);
    EXPECT_EQ(driver.derives(), before);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
}

TEST(DefaultPivotLatch, APausedDragReArmsTheEdgeOnlyWhenItPausesAtYawZero) {
    // The edge is "settled last frame, not settled now", so a drag that stops
    // for a frame and resumes is a new rotation start — but the yaw-0 gate
    // applies to it like any other. Paused at non-zero yaw, resuming holds;
    // paused back at yaw 0, resuming derives. Neither is a per-frame derive: a
    // rotation with no still frame in it derives at most once.
    LatchDriver driver;
    const vec2 cameraIso = vec2(0.0f);
    driver.hold(pose(0.0f, cameraIso), kDepthStart, 3);

    float yaw = 0.0f;
    for (int frame = 0; frame < 4; ++frame) {
        yaw += kYawStep;
        driver.step(pose(yaw, cameraIso), kDepthAfterFirstRotation);
    }
    ASSERT_EQ(driver.rotationStartDerives(), 1);

    // One paused frame at non-zero yaw: settled, and pan/zoom unchanged since
    // the derive, so the pause itself costs nothing.
    const int beforePause = driver.derives();
    EXPECT_FALSE(driver.step(pose(yaw, cameraIso), kDepthAfterFirstRotation).derive_);
    EXPECT_EQ(driver.derives(), beforePause);

    // Resuming from non-zero yaw holds.
    EXPECT_FALSE(driver.step(pose(yaw + kYawStep, cameraIso), kDepthAfterFirstRotation).derive_);
    EXPECT_EQ(driver.rotationStartDerives(), 1);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);

    // Dragging back to yaw 0 and pausing there re-arms the edge: the paused
    // frame is a still yaw-0 view, so the derive on resume is sound and reads
    // the content now under the crosshair.
    yaw += kYawStep;
    for (int frame = 0; frame < 5; ++frame) {
        yaw -= kYawStep;
        driver.step(pose(yaw, cameraIso), kDepthAfterPan);
    }
    ASSERT_LE(IRMath::abs(yaw), kYawSettleDelta);
    EXPECT_FALSE(driver.step(pose(yaw, cameraIso), kDepthAfterPan).derive_);
    EXPECT_TRUE(driver.step(pose(yaw + kYawStep, cameraIso), kDepthAfterPan).derive_);
    EXPECT_EQ(driver.rotationStartDerives(), 2);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
}

TEST(DefaultPivotLatch, AResidualYawWobbleUnderTheSettleDeltaIsNotARotationStart) {
    LatchDriver driver;
    const vec2 cameraIso = vec2(5.0f, 5.0f);
    driver.hold(pose(0.0f, cameraIso), kDepthStart, 3);
    const int before = driver.derives();

    // An order of magnitude under kYawSettleDelta per frame.
    float yaw = 0.0f;
    for (int frame = 0; frame < 20; ++frame) {
        yaw += (frame % 2 == 0) ? 0.1f * kYawSettleDelta : -0.1f * kYawSettleDelta;
        driver.step(pose(yaw, cameraIso), kDepthAfterFirstRotation);
    }
    EXPECT_EQ(driver.derives(), before);
    EXPECT_EQ(driver.rotationStartDerives(), 0);
}

// ---------------------------------------------------------------------------
// The mode gate: observe every frame, derive only when the default pivot owns
// the depth.
// ---------------------------------------------------------------------------

TEST(DefaultPivotLatch, ANonDefaultPivotPaysNoReadbackButStillObservesThePose) {
    LatchDriver driver;
    driver.hold(pose(0.0f, vec2(0.0f)), kDepthStart, 3);
    ASSERT_EQ(driver.derives(), 1);

    // ORIGIN mode / an explicit focus: the camera roams for a while.
    driver.setPivotOwnsDepth(false);
    driver.hold(pose(0.0f, vec2(100.0f, 100.0f)), kDepthAfterPan, 5);
    EXPECT_EQ(driver.derives(), 1);

    // Back on the default pivot the very frame the camera moves again. The pose
    // stamps kept running, so this frame's attachment is the PREVIOUS pose's and
    // the derive is correctly refused — the failure mode of stamping behind the
    // mode gate was matching a pose several frames stale.
    driver.setPivotOwnsDepth(true);
    EXPECT_FALSE(driver.step(pose(0.0f, vec2(140.0f, 90.0f)), kDepthAfterFirstRotation).derive_);
    EXPECT_TRUE(driver.step(pose(0.0f, vec2(140.0f, 90.0f)), kDepthAfterFirstRotation).derive_);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterFirstRotation);
}

TEST(DefaultPivotLatch, ARotationStartUnderANonDefaultPivotDerivesNothing) {
    LatchDriver driver;
    const vec2 cameraIso = vec2(0.0f);
    driver.hold(pose(0.0f, cameraIso), kDepthStart, 3);
    driver.setPivotOwnsDepth(false);
    const int before = driver.derives();

    driver.step(pose(kYawStep, cameraIso), kDepthAfterPan);
    EXPECT_EQ(driver.derives(), before);
    EXPECT_EQ(driver.rotationStartDerives(), 0);
}

} // namespace
