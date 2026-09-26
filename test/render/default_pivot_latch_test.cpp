#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/default_pivot_latch.hpp>
#include <irreden/render/render_manager.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>
#include <optional>

// ---------------------------------------------------------------------------
// The default pivot's latch policy and acquisition arithmetic.
//
// `docs/design/camera-yaw-pivot.md` §"Latch policy": the depth-aware default
// pivot acquires ONCE per rotation gesture — the first frame yaw changes, at
// any yaw — from the surface under the crosshair in the frame before, recovered
// in the frame that image was drawn in, and holds it for the gesture. A pan or
// zoom never re-latches; between gestures the anchor rides the camera.
// Acquisition never moves the view.
//
// No `scripts/pivot-verify.py` block can see most of that: each shot there is a
// one-frame pose snap, so a pan that lands without a rotation, a drag that
// mutates yaw inside RENDER, and the exact effective camera across an
// acquisition are all invisible to it. `DefaultPivotLatch` is the policy with
// the GPU readback lifted out, so this file drives it frame by frame.
//
// The one-frame lag is modelled explicitly. `beginFrame` runs ahead of the
// RENDER pipeline, so a derive at frame N consumes the attachment frame N-1
// rendered, described by the pose frame N-1's composite stamped —
// `LatchDriver::step` keeps both. So is the store's key: a frame drawn at a
// cardinal stores the visible surface kCardinalStoreLatticeDepth deeper.
// ---------------------------------------------------------------------------

namespace {

using IRMath::vec2;
using IRMath::vec3;
using IRRender::DefaultPivotLatch;
using IRRender::DefaultPivotSourceFrame;
// Per-texel store of a 3×3 block's winners; nullopt names neither store.
using TexelSubjects = std::array<std::optional<bool>, 9>;

// One frame of a real rotation, comfortably above kYawSettleDelta.
constexpr float kYawStep = 0.05f;
constexpr float kYawSettleDelta = DefaultPivotLatch::kYawSettleDelta;
constexpr float kPi = IRMath::kPi;

// An arbitrary non-trivial canvas center; nothing may depend on it.
constexpr vec2 kCanvasCenterIso = vec2(311.0f, -47.0f);

// Yawed iso depths standing for distinct content under the crosshair. Each is
// distinguishable, so an assertion on the latched depth names WHICH frame's
// attachment was consumed rather than merely that something changed.
constexpr float kDepthStart = 5.0f;
constexpr float kDepthAfterPan = 17.0f;
constexpr float kDepthAfterFirstRotation = -3.25f;

// What the crosshair sees in one rendered frame: a visible surface at a yawed
// depth (world units), or background.
using Surface = std::optional<float>;
constexpr Surface kBackground = std::nullopt;

class LatchDriver {
  public:
    // Advance one frame. beginFrame observes `beginYaw`; a camera system may
    // then change yaw inside RENDER, so the frame is drawn at `renderYaw`; the
    // composite stamps that pose and leaves `surface` in the depth attachment.
    // A derive admitted this frame consumes the PREVIOUS frame's attachment.
    bool stepWithInFrameYaw(
        float beginYaw, float renderYaw, vec2 cameraIso, Surface surface, int effSub = 1
    ) {
        const bool derive = m_latch.observeFrame(beginYaw, m_pivotOwnsDepth);
        if (derive) {
            ++m_derives;
            if (m_attachment.has_value()) {
                m_latch.acquire(*m_attachment);
            }
        }
        const float residualYaw = IRPrefab::Camera::computeYawSplit(renderYaw).second;
        m_latch.stampSourceFrame(
            DefaultPivotSourceFrame{
                renderYaw,
                residualYaw,
                cameraIso,
                effectiveCameraIso(renderYaw, cameraIso),
                kCanvasCenterIso,
                effSub
            },
            m_pivotOwnsDepth
        );
        if (!surface.has_value()) {
            m_attachment = std::nullopt;
            return derive;
        }
        const float storeKey = residualYaw == 0.0f
                                   ? *surface + DefaultPivotLatch::kCardinalStoreLatticeDepth
                                   : *surface;
        m_attachment = storeKey * static_cast<float>(effSub);
        return derive;
    }

    bool step(float yaw, vec2 cameraIso, Surface surface) {
        return stepWithInFrameYaw(yaw, yaw, cameraIso, surface);
    }

    void hold(float yaw, vec2 cameraIso, Surface surface, int frames) {
        for (int i = 0; i < frames; ++i) {
            step(yaw, cameraIso, surface);
        }
    }

    // `IRRender::getEffectiveCameraIso`'s default CAMERA_CENTER branch over the
    // latch's current state.
    vec2 effectiveCameraIso(float yaw, vec2 cameraIso) const {
        const vec2 pivotCameraIso = cameraIso + m_latch.viewOffsetIso();
        return IRMath::cameraYawPivotOffset(pivotCameraIso, focus(cameraIso), yaw);
    }
    vec3 focus(vec2 cameraIso) const {
        return m_latch.focus(kCanvasCenterIso - cameraIso);
    }

    void setPivotOwnsDepth(bool owns) {
        m_pivotOwnsDepth = owns;
    }
    const DefaultPivotLatch &latch() const {
        return m_latch;
    }
    float isoDepth() const {
        return m_latch.isoDepth();
    }
    vec2 viewOffsetIso() const {
        return m_latch.viewOffsetIso();
    }
    int derives() const {
        return m_derives;
    }

  private:
    DefaultPivotLatch m_latch;
    bool m_pivotOwnsDepth = true;
    std::optional<float> m_attachment;
    int m_derives = 0;
};

// Settle at `yaw` over `surface`, then start a gesture from there that reads
// `surface`. Returns the effective camera the settled frame was drawn with.
vec2 settleThenStartGesture(
    LatchDriver &driver, float yaw, vec2 cameraIso, Surface surface, int settleFrames = 3
) {
    driver.hold(yaw, cameraIso, surface, settleFrames);
    const vec2 before = driver.effectiveCameraIso(yaw, cameraIso);
    EXPECT_TRUE(driver.step(yaw + kYawStep, cameraIso, surface));
    return before;
}

// ---------------------------------------------------------------------------
// When the latch derives: once per rotation gesture, never on a still camera,
// a pan or a zoom.
// ---------------------------------------------------------------------------

TEST(DefaultPivotLatch, AStillCameraNeverDerivesAndHoldsTheInitialState) {
    LatchDriver driver;
    driver.hold(0.0f, vec2(0.0f), kDepthStart, 30);
    EXPECT_EQ(driver.derives(), 0);
    EXPECT_FALSE(driver.latch().hasAcquired());
    EXPECT_FLOAT_EQ(driver.isoDepth(), 0.0f);
    EXPECT_EQ(driver.viewOffsetIso(), vec2(0.0f));
}

TEST(DefaultPivotLatch, TheFirstObservedFrameIsNotAGestureAtAnyYaw) {
    // A creation that starts at non-zero yaw has no previous frame to compare
    // against, and no source to read.
    LatchDriver driver;
    EXPECT_FALSE(driver.step(0.39f, vec2(0.0f), kDepthStart));
    driver.hold(0.39f, vec2(0.0f), kDepthStart, 5);
    EXPECT_EQ(driver.derives(), 0);
}

TEST(DefaultPivotLatch, PanNeverDerivesAndCarriesTheAnchorWithTheCamera) {
    // Nothing re-latches after a pan, so the view never jumps when the camera
    // stops. The anchor rides the pan instead — the focus moves by exactly the
    // un-projected camera delta.
    LatchDriver driver;
    const vec2 before = vec2(0.0f);
    const vec3 anchor = [&] {
        settleThenStartGesture(driver, 0.0f, before, kDepthStart);
        driver.hold(kYawStep, before, kDepthStart, 3);
        return driver.focus(before);
    }();
    ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
    const int derivesBeforePan = driver.derives();

    const vec2 after = vec2(64.0f, -12.0f);
    driver.hold(kYawStep, after, kDepthAfterPan, 10);
    EXPECT_EQ(driver.derives(), derivesBeforePan);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
    const vec3 carried = anchor + IRMath::isoPixelToPos3D(before - after, 0.0f);
    const vec3 focus = driver.focus(after);
    EXPECT_NEAR(focus.x, carried.x, 1e-4f);
    EXPECT_NEAR(focus.y, carried.y, 1e-4f);
    EXPECT_NEAR(focus.z, carried.z, 1e-4f);
}

TEST(DefaultPivotLatch, ZoomNeverDerives) {
    // Zoom reaches the latch only through the source stamp's subdivisions;
    // a zoom with no rotation is not a gesture.
    LatchDriver driver;
    driver.hold(0.0f, vec2(0.0f), kDepthStart, 4);
    driver.stepWithInFrameYaw(0.0f, 0.0f, vec2(0.0f), kDepthAfterPan, 2);
    driver.hold(0.0f, vec2(0.0f), kDepthAfterPan, 4);
    EXPECT_EQ(driver.derives(), 0);
    EXPECT_FLOAT_EQ(driver.isoDepth(), 0.0f);
}

TEST(DefaultPivotLatch, RotationStartAcquiresTheSurfaceAPanBroughtUnderTheCrosshair) {
    // Pan, then rotate with NO still frame between them: the gesture reads the
    // panned frame, whose stamp carries the panned camera.
    LatchDriver driver;
    driver.hold(0.0f, vec2(0.0f), kDepthStart, 4);
    const vec2 panned = vec2(64.0f, -12.0f);
    ASSERT_FALSE(driver.step(0.0f, panned, kDepthAfterPan));

    EXPECT_TRUE(driver.step(kYawStep, panned, kDepthAfterPan));
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
    EXPECT_EQ(driver.viewOffsetIso(), vec2(0.0f));
}

TEST(DefaultPivotLatch, ContinuousRotationDerivesOnceAtItsStartAndNeverPerFrame) {
    // Gesture-start only. A per-frame derive would pay a full GPU flush every
    // frame of every rotation AND chase the pivot across the content it is
    // pinning.
    LatchDriver driver;
    const vec2 cameraIso = vec2(12.0f, 3.0f);
    driver.hold(0.0f, cameraIso, kDepthStart, 3);

    float yaw = 0.0f;
    for (int frame = 0; frame < 24; ++frame) {
        yaw += kYawStep;
        driver.step(yaw, cameraIso, kDepthAfterFirstRotation);
    }
    EXPECT_EQ(driver.derives(), 1);

    // Settling out of the rotation derives nothing either — the anchor
    // acquired at the start pins the whole gesture.
    driver.hold(yaw, cameraIso, kDepthAfterFirstRotation, 10);
    EXPECT_EQ(driver.derives(), 1);
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
}

TEST(DefaultPivotLatch, RotationStartFromNonZeroYawDerives) {
    // A gesture starting from any yaw acquires: the recovery carries the
    // source yaw, so a non-zero-yaw source pins the point under the crosshair.
    LatchDriver driver;
    const vec2 cameraIso = vec2(3.0f, -9.0f);
    const float offZero = 2.0f * kYawSettleDelta;
    driver.hold(offZero, cameraIso, kDepthAfterPan, 3);
    ASSERT_EQ(driver.derives(), 0);

    EXPECT_TRUE(driver.step(offZero + kYawStep, cameraIso, kDepthAfterPan));
    EXPECT_EQ(driver.derives(), 1);
    EXPECT_TRUE(driver.latch().hasAcquired());
}

TEST(DefaultPivotLatch, SecondRotationFromNonZeroYawReacquires) {
    // A rotation changes what sits under the crosshair; the next gesture
    // acquires that surface rather than pivoting about the first one's anchor.
    LatchDriver driver;
    const vec2 cameraIso = vec2(-7.0f, 21.0f);
    driver.hold(0.0f, cameraIso, kDepthStart, 3);

    float yaw = 0.0f;
    for (int frame = 0; frame < 6; ++frame) {
        yaw += kYawStep;
        driver.step(yaw, cameraIso, kDepthStart);
    }
    ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
    const vec2 viewBefore = driver.effectiveCameraIso(yaw, cameraIso);

    // It settles at a new yaw, where DIFFERENT content sits under the
    // crosshair.
    driver.hold(yaw, cameraIso, kDepthAfterFirstRotation, 4);
    EXPECT_TRUE(driver.step(yaw + kYawStep, cameraIso, kDepthAfterFirstRotation));
    EXPECT_EQ(driver.derives(), 2);
    EXPECT_NE(driver.isoDepth(), kDepthStart);
    // ... and re-anchoring did not move the view the settled frame showed.
    const vec2 viewAfter = driver.effectiveCameraIso(yaw, cameraIso);
    EXPECT_NEAR(viewAfter.x, viewBefore.x, 1e-4f);
    EXPECT_NEAR(viewAfter.y, viewBefore.y, 1e-4f);
}

TEST(DefaultPivotLatch, APausedDragReArmsTheEdgeAtAnyYaw) {
    // The edge is "settled last frame, not settled now", so a drag that stops
    // for a frame and resumes is a new gesture, at whatever yaw it paused.
    // A rotation with no still frame in it derives at most once.
    LatchDriver driver;
    const vec2 cameraIso = vec2(0.0f);
    driver.hold(0.0f, cameraIso, kDepthStart, 3);

    float yaw = 0.0f;
    for (int frame = 0; frame < 4; ++frame) {
        yaw += kYawStep;
        driver.step(yaw, cameraIso, kDepthAfterFirstRotation);
    }
    ASSERT_EQ(driver.derives(), 1);

    // One paused frame costs nothing by itself.
    EXPECT_FALSE(driver.step(yaw, cameraIso, kDepthAfterFirstRotation));
    EXPECT_EQ(driver.derives(), 1);

    // Resuming acquires from the paused frame.
    EXPECT_TRUE(driver.step(yaw + kYawStep, cameraIso, kDepthAfterFirstRotation));
    EXPECT_EQ(driver.derives(), 2);
}

TEST(DefaultPivotLatch, AResidualYawWobbleUnderTheSettleDeltaIsNotARotationStart) {
    LatchDriver driver;
    const vec2 cameraIso = vec2(5.0f, 5.0f);
    driver.hold(0.0f, cameraIso, kDepthStart, 3);

    // An order of magnitude under kYawSettleDelta per frame.
    float yaw = 0.0f;
    for (int frame = 0; frame < 20; ++frame) {
        yaw += (frame % 2 == 0) ? 0.1f * kYawSettleDelta : -0.1f * kYawSettleDelta;
        driver.step(yaw, cameraIso, kDepthAfterFirstRotation);
    }
    EXPECT_EQ(driver.derives(), 0);
}

TEST(DefaultPivotLatch, ADragThatMutatesYawInsideRenderDerivesOnItsFirstYawDeltaFrame) {
    // The shape of a Ctrl+middle drag with no explicit focus — including a
    // cursor-pivot click that missed every surface: CAMERA_MOUSE_ROTATE sets
    // yaw inside RENDER, ahead of geometry, so the frame it first moves in is
    // drawn at the new yaw while beginFrame still saw the old one.
    LatchDriver driver;
    const vec2 cameraIso = vec2(-20.0f, 14.0f);
    const float startYaw = 0.6f;
    driver.hold(startYaw, cameraIso, kDepthStart, 3);

    // Frame N: beginFrame sees the settled yaw; the drag moves it mid-frame.
    EXPECT_FALSE(
        driver.stepWithInFrameYaw(startYaw, startYaw + kYawStep, cameraIso, kDepthAfterPan)
    );
    const vec2 drawnWith = driver.effectiveCameraIso(startYaw + kYawStep, cameraIso);

    // Frame N+1 is the first yaw-delta frame beginFrame observes: it acquires
    // from frame N, drawn at the new yaw — and so leaves frame N's view where
    // it was.
    EXPECT_TRUE(driver.stepWithInFrameYaw(
        startYaw + kYawStep,
        startYaw + 2.0f * kYawStep,
        cameraIso,
        kDepthAfterPan
    ));
    EXPECT_EQ(driver.derives(), 1);
    const vec2 after = driver.effectiveCameraIso(startYaw + kYawStep, cameraIso);
    EXPECT_NEAR(after.x, drawnWith.x, 1e-4f);
    EXPECT_NEAR(after.y, drawnWith.y, 1e-4f);
}

// ---------------------------------------------------------------------------
// What an acquisition turns the sample into.
// ---------------------------------------------------------------------------

// Every yaw the acquisition must be displacement-free at.
const float kAcquisitionYaws[] = {
    0.0f, IRMath::kPi / 8.0f, IRMath::kQuarterPi, -IRMath::kQuarterPi, IRMath::kHalfPi, kPi
};

TEST(DefaultPivotLatch, AcquisitionLeavesTheEffectiveCameraUnchangedAtEveryYaw) {
    // The ruling's first requirement: acquisition must not move the view. A
    // gesture re-anchors onto a surface at a depth well away from the current
    // anchor, and the effective camera of the settled frame — the one on
    // screen — must survive it.
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    for (const float yaw : kAcquisitionYaws) {
        LatchDriver driver;
        // First gesture, from yaw 0, anchors on kDepthStart.
        settleThenStartGesture(driver, 0.0f, cameraIso, kDepthStart);
        ASSERT_FLOAT_EQ(driver.isoDepth(), kDepthStart);
        // Settle at `yaw`, where content 12 units nearer sits under the
        // crosshair, and start a second gesture there.
        driver.hold(yaw, cameraIso, kDepthStart - 12.0f, 3);
        const vec2 before = settleThenStartGesture(driver, yaw, cameraIso, kDepthStart - 12.0f);
        const vec2 after = driver.effectiveCameraIso(yaw, cameraIso);
        EXPECT_NEAR(after.x, before.x, 1e-4f) << "yaw=" << yaw;
        EXPECT_NEAR(after.y, before.y, 1e-4f) << "yaw=" << yaw;

        // The acquired anchor sits at the canvas center at that yaw, i.e. it
        // IS the point under the crosshair.
        const vec3 anchor = driver.focus(cameraIso);
        const vec2 onScreen = IRMath::pos3DtoPos2DIsoYawed(anchor, yaw) + after;
        EXPECT_NEAR(onScreen.x, kCanvasCenterIso.x, 1e-3f) << "yaw=" << yaw;
        EXPECT_NEAR(onScreen.y, kCanvasCenterIso.y, 1e-3f) << "yaw=" << yaw;
    }
}

TEST(DefaultPivotLatch, RecoveringWithoutTheSourceYawWouldMoveTheView) {
    // Positive-fire control for the arm above: the same acquisition computed
    // with `isoPixelToPos3D` and no yaw term moves the effective camera by more
    // than one iso unit at every non-zero yaw, so the arm above can fail.
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    for (const float yaw : kAcquisitionYaws) {
        if (yaw == 0.0f) {
            continue;
        }
        LatchDriver driver;
        settleThenStartGesture(driver, 0.0f, cameraIso, kDepthStart);
        driver.hold(yaw, cameraIso, kDepthStart - 12.0f, 3);
        const vec2 source = driver.effectiveCameraIso(yaw, cameraIso);

        const vec3 unYawed =
            IRMath::isoPixelToPos3D(kCanvasCenterIso - source, kDepthStart - 12.0f);
        const float depth = unYawed.x + unYawed.y + unYawed.z;
        const vec2 offset = kCanvasCenterIso - cameraIso - IRMath::pos3DtoPos2DIso(unYawed);
        const vec3 focus = IRMath::isoPixelToPos3D(kCanvasCenterIso - cameraIso - offset, depth);
        const vec2 moved = IRMath::cameraYawPivotOffset(cameraIso + offset, focus, yaw);
        EXPECT_GT(IRMath::length(moved - source), 1.0f) << "yaw=" << yaw;
    }
}

TEST(DefaultPivotLatch, ABackgroundSampleHoldsTheDepthAndTheOffset) {
    // A crosshair over nothing keeps the previous anchor rather than jumping
    // to the depth-0 point.
    LatchDriver driver;
    const vec2 cameraIso = vec2(9.0f, 2.0f);
    const float yaw = IRMath::kQuarterPi;
    settleThenStartGesture(driver, yaw, cameraIso, kDepthStart);
    ASSERT_TRUE(driver.latch().hasAcquired());
    const float depth = driver.isoDepth();
    const vec2 offset = driver.viewOffsetIso();
    ASSERT_NE(offset, vec2(0.0f));

    driver.hold(yaw + kYawStep, cameraIso, kBackground, 3);
    const vec2 before = settleThenStartGesture(driver, yaw + kYawStep, cameraIso, kBackground);
    EXPECT_EQ(driver.isoDepth(), depth);
    EXPECT_EQ(driver.viewOffsetIso(), offset);
    EXPECT_EQ(driver.effectiveCameraIso(yaw + kYawStep, cameraIso), before);
}

TEST(DefaultPivotLatch, YawZeroAcquisitionsLeaveTheViewOffsetBitExact) {
    // yaw-0 frames depend on the offset alone and are byte-compared by the
    // reference suites, so an acquisition from a yaw-0 frame must not
    // round-trip it through the recovery.
    LatchDriver driver;
    const vec2 cameraIsos[] = {vec2(0.0f), vec2(64.0f, -12.0f), vec2(-3.3f, 7.1f)};
    for (const vec2 cameraIso : cameraIsos) {
        settleThenStartGesture(driver, 0.0f, cameraIso, kDepthAfterPan);
        driver.hold(0.0f, cameraIso, kDepthAfterPan, 2);
        EXPECT_EQ(driver.viewOffsetIso(), vec2(0.0f));
        EXPECT_EQ(driver.isoDepth(), kDepthAfterPan);
    }

    // With an offset already latched, a yaw-0 acquisition keeps it as-is.
    settleThenStartGesture(driver, IRMath::kHalfPi, vec2(0.0f), kDepthStart);
    const vec2 offset = driver.viewOffsetIso();
    ASSERT_NE(offset, vec2(0.0f));
    settleThenStartGesture(driver, 0.0f, vec2(0.0f), kDepthAfterFirstRotation);
    EXPECT_EQ(driver.viewOffsetIso(), offset);
    EXPECT_EQ(driver.isoDepth(), kDepthAfterFirstRotation);
}

// The anchor a latch holds after acquiring @p framebufferIsoDepth from a
// source frame drawn at @p yaw with effective camera @p effectiveCameraIso,
// sampled off a fragment of the voxel store or, with @p voxelStoreWinner false,
// of the SDF shape store.
vec3 acquiredAnchor(
    float yaw,
    vec2 cameraIso,
    vec2 effectiveCameraIso,
    float framebufferIsoDepth,
    bool voxelStoreWinner = true
) {
    constexpr int kEffSub = 4;
    DefaultPivotLatch latch;
    latch.stampSourceFrame(
        DefaultPivotSourceFrame{
            yaw,
            IRPrefab::Camera::computeYawSplit(yaw).second,
            cameraIso,
            effectiveCameraIso,
            kCanvasCenterIso,
            kEffSub
        },
        true
    );
    latch.observeFrame(yaw, true);
    latch.acquire(framebufferIsoDepth * static_cast<float>(kEffSub), voxelStoreWinner);
    return latch.focus(kCanvasCenterIso - cameraIso);
}

TEST(DefaultPivotLatch, ACardinalSourceLatchesTheVisibleSurfaceNotTheStoreKey) {
    // The cardinal store keys a fragment kCardinalStoreLatticeDepth behind the
    // surface it shows. At every cardinal the latch lands on the surface: the
    // point on the source frame's crosshair ray at the key minus the lattice.
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    const float cardinals[] = {0.0f, IRMath::kHalfPi, kPi, -IRMath::kHalfPi};
    for (const float yaw : cardinals) {
        // A never-acquired latch has no view offset, so a yaw-0 frame is drawn
        // at the raw camera.
        const vec2 effectiveCameraIso = yaw == 0.0f ? cameraIso : vec2(61.5f, -9.25f);
        const vec3 anchor = acquiredAnchor(yaw, cameraIso, effectiveCameraIso, kDepthAfterPan);
        const vec3 surface = IRMath::isoPixelToPos3DYawed(
            kCanvasCenterIso - effectiveCameraIso,
            kDepthAfterPan - DefaultPivotLatch::kCardinalStoreLatticeDepth,
            yaw
        );
        EXPECT_NEAR(anchor.x, surface.x, 1e-3f) << "yaw=" << yaw;
        EXPECT_NEAR(anchor.y, surface.y, 1e-3f) << "yaw=" << yaw;
        EXPECT_NEAR(anchor.z, surface.z, 1e-3f) << "yaw=" << yaw;
    }
}

TEST(DefaultPivotLatch, ANonCardinalSourceKeepsTheStoreKey) {
    // The per-axis store keys a non-cardinal frame with no lattice shift, so
    // the latch takes its sample as-is there. Positive fire for the arm above:
    // a subtraction applied at every yaw would move this anchor by the lattice.
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    const vec2 effectiveCameraIso = vec2(61.5f, -9.25f);
    const float yaws[] = {IRMath::kPi / 8.0f, IRMath::kQuarterPi, 2.0f * kPi / 3.0f};
    for (const float yaw : yaws) {
        const vec3 anchor = acquiredAnchor(yaw, cameraIso, effectiveCameraIso, kDepthAfterPan);
        const vec3 key = IRMath::isoPixelToPos3DYawed(
            kCanvasCenterIso - effectiveCameraIso,
            kDepthAfterPan,
            yaw
        );
        EXPECT_NEAR(anchor.x, key.x, 1e-3f) << "yaw=" << yaw;
        EXPECT_NEAR(anchor.y, key.y, 1e-3f) << "yaw=" << yaw;
        EXPECT_NEAR(anchor.z, key.z, 1e-3f) << "yaw=" << yaw;
    }
}

TEST(DefaultPivotLatch, ACardinalSdfWinnerLatchesItsKeyAsItStands) {
    // The SDF shape store keys a cardinal fragment on the surface, with no
    // lattice, so an SDF winner's sample is the surface already. Positive fire
    // for the subject branch: the voxel-store subtraction applied here would
    // put the anchor the lattice depth in front of the surface.
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    const float cardinals[] = {0.0f, IRMath::kHalfPi, kPi, -IRMath::kHalfPi};
    for (const float yaw : cardinals) {
        const vec2 effectiveCameraIso = yaw == 0.0f ? cameraIso : vec2(61.5f, -9.25f);
        const vec3 anchor =
            acquiredAnchor(yaw, cameraIso, effectiveCameraIso, kDepthAfterPan, false);
        const vec3 key = IRMath::isoPixelToPos3DYawed(
            kCanvasCenterIso - effectiveCameraIso,
            kDepthAfterPan,
            yaw
        );
        EXPECT_NEAR(anchor.x, key.x, 1e-3f) << "yaw=" << yaw;
        EXPECT_NEAR(anchor.y, key.y, 1e-3f) << "yaw=" << yaw;
        EXPECT_NEAR(anchor.z, key.z, 1e-3f) << "yaw=" << yaw;
    }
}

TEST(DefaultPivotLatch, TheCrosshairTexelIsTheCanvasCenterUpToTheCameraFraction) {
    // The main canvas is placed at the whole-texel part of the effective
    // camera and the gather shifts it by the rest, so on each axis the crosshair
    // displays the canvas center texel where the camera term is on the texel
    // lattice and the texel one lower where it is not. Measured on macOS/Metal
    // against the canvas depth at that texel: (24, 0) at effSub 4 reads
    // (321, 361) and (3.0645, -27.041) reads (320, 360) on a 642 x 722 canvas.
    // OpenGL displays (320, 361) at the fractional camera; the block search
    // covers that.
    const IRMath::ivec2 canvasSize = IRMath::ivec2(642, 722);
    const IRMath::ivec2 centerTexel = IRMath::trixelOriginOffsetX1(canvasSize);
    const IRMath::ivec2 lowerTexel = IRMath::trixelOriginOffsetZ1(canvasSize);
    struct Case {
        vec2 effectiveCameraIso_;
        int effectiveSubdivisions_;
        IRMath::ivec2 texel_;
    };
    const Case cases[] = {
        {vec2(24.0f, 0.0f), 4, centerTexel},
        {vec2(-7.25f, 13.5f), 4, centerTexel},
        {vec2(3.0645046f, -27.041023f), 4, lowerTexel},
        {vec2(-24.01165f, -6.0203533f), 8, lowerTexel},
    };
    for (const Case &c : cases) {
        DefaultPivotSourceFrame frame;
        frame.effectiveCameraIso_ = c.effectiveCameraIso_;
        frame.effectiveSubdivisions_ = c.effectiveSubdivisions_;
        const IRMath::ivec2 texel = IRRender::defaultPivotCrosshairCanvasTexel(frame, canvasSize);
        EXPECT_EQ(texel, c.texel_)
            << "camera=(" << c.effectiveCameraIso_.x << ", " << c.effectiveCameraIso_.y << ")";
    }
}

TEST(DefaultPivotLatch, TheSubjectIsReadOffTheBlockTexelsHoldingTheSampledKey) {
    // Stored canvas distances around the crosshair estimate (index 4), one key
    // per row, the sample holding the key of the estimate's +y row (a pi/2
    // cardinal source with a fractional camera, measured on Windows/OpenGL).
    // That row is an SDF winner and the estimate a voxel one, so the row
    // decides.
    const std::array<int, 9> glBlock = {-662, -662, -662, -670, -670, -670, -678, -678, -678};
    const TexelSubjects voxelBelowSdf = {true, true, true, true, true, true, false, false, false};
    EXPECT_EQ(
        IRRender::defaultPivotSampledSubjectIsVoxelStore(glBlock, voxelBelowSdf, -678),
        std::optional<bool>(false)
    );

    // Each texel decides when it alone holds the key, whichever subject it is.
    for (std::size_t i = 0; i < 9; ++i) {
        std::array<int, 9> block{};
        block.fill(1000);
        block[i] = 2;
        for (const bool voxel : {true, false}) {
            TexelSubjects subjects{};
            subjects.fill(!voxel);
            subjects[i] = voxel;
            SCOPED_TRACE(testing::Message() << "texel " << i << " voxel=" << voxel);
            EXPECT_EQ(
                IRRender::defaultPivotSampledSubjectIsVoxelStore(block, subjects, 2),
                std::optional<bool>(voxel)
            );
        }
    }

    // Every texel holding the key agrees: the subject is established.
    const std::array<int, 9> flatBlock = {186, 186, 186, 186, 186, 186, 186, 186, 186};
    TexelSubjects allSdf{};
    allSdf.fill(false);
    EXPECT_EQ(
        IRRender::defaultPivotSampledSubjectIsVoxelStore(flatBlock, allSdf, 186),
        std::optional<bool>(false)
    );
}

TEST(DefaultPivotLatch, KeyMatchingTexelsThatDisagreeOnSubjectHold) {
    // A voxel/SDF seam at the crosshair with a bit-equal key on both sides: the
    // sample cannot say which store it came from, so no subject is established,
    // wherever in the block the disagreeing texel sits.
    const std::array<int, 9> flatBlock = {186, 186, 186, 186, 186, 186, 186, 186, 186};
    for (std::size_t i = 0; i < 9; ++i) {
        if (i == 4) {
            continue;
        }
        TexelSubjects subjects{};
        subjects.fill(true);
        subjects[i] = false;
        SCOPED_TRACE(testing::Message() << "texel " << i);
        EXPECT_EQ(
            IRRender::defaultPivotSampledSubjectIsVoxelStore(flatBlock, subjects, 186),
            std::nullopt
        );
    }
    // Texels that do not hold the key take no part, whatever their subject.
    std::array<int, 9> block = flatBlock;
    block[7] = 190;
    TexelSubjects subjects{};
    subjects.fill(true);
    subjects[7] = false;
    EXPECT_EQ(
        IRRender::defaultPivotSampledSubjectIsVoxelStore(block, subjects, 186),
        std::optional<bool>(true)
    );
}

TEST(DefaultPivotLatch, ABlockWithNoTexelHoldingTheSampledKeyHolds) {
    // The estimate's own subject is no evidence for a key it does not hold.
    const std::array<int, 9> glBlock = {-662, -662, -662, -670, -670, -670, -678, -678, -678};
    TexelSubjects allVoxel{};
    allVoxel.fill(true);
    EXPECT_EQ(
        IRRender::defaultPivotSampledSubjectIsVoxelStore(glBlock, allVoxel, 10),
        std::nullopt
    );
}

TEST(DefaultPivotLatch, AKeyMatchingTexelNamingNoKnownStoreHolds) {
    // A texel holding the key whose entity is neither store's — dead since the
    // frame was drawn, or a producer the classifier does not know — leaves the
    // sample's store unproven, even when every other matching texel agrees.
    const std::array<int, 9> flatBlock = {186, 186, 186, 186, 186, 186, 186, 186, 186};
    for (std::size_t i = 0; i < 9; ++i) {
        for (const bool voxel : {true, false}) {
            TexelSubjects subjects{};
            subjects.fill(voxel);
            subjects[i] = std::nullopt;
            SCOPED_TRACE(testing::Message() << "texel " << i << " voxel=" << voxel);
            EXPECT_EQ(
                IRRender::defaultPivotSampledSubjectIsVoxelStore(flatBlock, subjects, 186),
                std::nullopt
            );
        }
    }
    // One that does not hold the key takes no part.
    std::array<int, 9> block = flatBlock;
    block[2] = 190;
    TexelSubjects subjects{};
    subjects.fill(false);
    subjects[2] = std::nullopt;
    EXPECT_EQ(
        IRRender::defaultPivotSampledSubjectIsVoxelStore(block, subjects, 186),
        std::optional<bool>(false)
    );
}

// A latch that acquired kDepthStart off a voxel winner at yaw 0, then acquires
// kDepthAfterPan from a source frame at @p yaw whose winner's subject is
// @p voxelStoreWinner.
DefaultPivotLatch latchReacquiredFrom(float yaw, std::optional<bool> voxelStoreWinner) {
    const vec2 cameraIso = vec2(64.0f, -12.0f);
    DefaultPivotLatch latch;
    const auto acquireFrom = [&](float sourceYaw, float depth, std::optional<bool> subject) {
        latch.stampSourceFrame(
            DefaultPivotSourceFrame{
                sourceYaw,
                IRPrefab::Camera::computeYawSplit(sourceYaw).second,
                cameraIso,
                sourceYaw == 0.0f ? cameraIso : vec2(61.5f, -9.25f),
                kCanvasCenterIso,
                1
            },
            true
        );
        latch.observeFrame(sourceYaw, true);
        latch.acquire(depth, subject);
    };
    acquireFrom(0.0f, kDepthStart, true);
    acquireFrom(yaw, kDepthAfterPan, voxelStoreWinner);
    return latch;
}

TEST(DefaultPivotLatch, ACardinalSourceWithAnUnestablishedSubjectHoldsItsAnchor) {
    // The classifier's nullopt at a cardinal source — no key match, disagreeing
    // texels, or a block past the canvas edge — keeps the previous anchor, as a
    // background sample does, rather than branch on an unproven subject.
    const float held = kDepthStart - DefaultPivotLatch::kCardinalStoreLatticeDepth;
    const float cardinals[] = {0.0f, IRMath::kHalfPi, kPi, -IRMath::kHalfPi};
    for (const float yaw : cardinals) {
        const DefaultPivotLatch latch = latchReacquiredFrom(yaw, std::nullopt);
        EXPECT_EQ(latch.isoDepth(), held) << "yaw=" << yaw;
        EXPECT_EQ(latch.viewOffsetIso(), vec2(0.0f)) << "yaw=" << yaw;

        // Positive fire: an established subject re-acquires from the same frame.
        for (const bool voxel : {true, false}) {
            EXPECT_NE(latchReacquiredFrom(yaw, voxel).isoDepth(), held)
                << "yaw=" << yaw << " voxel=" << voxel;
        }
    }

    // A non-cardinal source never reads the subject, so nullopt acquires.
    const DefaultPivotLatch perAxis = latchReacquiredFrom(IRMath::kQuarterPi, std::nullopt);
    EXPECT_NE(perAxis.isoDepth(), held);
    EXPECT_NE(perAxis.viewOffsetIso(), vec2(0.0f));
}

// The per-texel store read (`RenderManager::texelSubjectIsVoxelStore`) against a
// live EntityManager. No RenderManager: the read touches only the ECS.
class DefaultPivotTexelSubject : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

TEST_F(DefaultPivotTexelSubject, OnlyALiveStoreEntityEstablishesItsStore) {
    const IREntity::EntityId sdf = IREntity::createEntity(IRComponents::C_ShapeDescriptor{});
    const IREntity::EntityId voxel = IREntity::createEntity(IRComponents::C_VoxelSetNew{});
    const IREntity::EntityId neither = IREntity::createEntity();
    EXPECT_EQ(IRRender::RenderManager::texelSubjectIsVoxelStore(sdf), std::optional<bool>(false));
    EXPECT_EQ(IRRender::RenderManager::texelSubjectIsVoxelStore(voxel), std::optional<bool>(true));
    EXPECT_EQ(IRRender::RenderManager::texelSubjectIsVoxelStore(neither), std::nullopt);
    EXPECT_EQ(
        IRRender::RenderManager::texelSubjectIsVoxelStore(IREntity::kNullEntity),
        std::nullopt
    );
}

TEST_F(DefaultPivotTexelSubject, AnEntityInBothStoresHolds) {
    // Either store can write the id of an entity carrying both components, so
    // the id does not establish which one won the texel.
    const IREntity::EntityId both =
        IREntity::createEntity(IRComponents::C_ShapeDescriptor{}, IRComponents::C_VoxelSetNew{});
    const std::optional<bool> subject = IRRender::RenderManager::texelSubjectIsVoxelStore(both);
    EXPECT_EQ(subject, std::nullopt);

    TexelSubjects subjects{};
    subjects.fill(subject);
    const std::array<int, 9> flatBlock = {186, 186, 186, 186, 186, 186, 186, 186, 186};
    const std::optional<bool> classified =
        IRRender::defaultPivotSampledSubjectIsVoxelStore(flatBlock, subjects, 186);
    EXPECT_EQ(classified, std::nullopt);

    const float held = kDepthStart - DefaultPivotLatch::kCardinalStoreLatticeDepth;
    for (const float yaw : {0.0f, IRMath::kHalfPi, kPi, -IRMath::kHalfPi}) {
        const DefaultPivotLatch latch = latchReacquiredFrom(yaw, classified);
        EXPECT_EQ(latch.isoDepth(), held) << "yaw=" << yaw;
        EXPECT_EQ(latch.viewOffsetIso(), vec2(0.0f)) << "yaw=" << yaw;
    }
}

TEST_F(DefaultPivotTexelSubject, APriorFrameSdfWinnerDestroyedBeforeAcquisitionHolds) {
    // The canvas still holds the frame the SDF entity won, key and id at every
    // texel of the block; UPDATE destroys the entity before beginFrame reads
    // them back. Its dead id must not read as the voxel store — that would
    // subtract the voxel lattice from an SDF surface key.
    const IREntity::EntityId sdf = IREntity::createEntity(IRComponents::C_ShapeDescriptor{});
    const std::array<int, 9> flatBlock = {186, 186, 186, 186, 186, 186, 186, 186, 186};
    const auto classify = [&] {
        TexelSubjects subjects{};
        for (auto &subject : subjects) {
            subject = IRRender::RenderManager::texelSubjectIsVoxelStore(sdf);
        }
        return IRRender::defaultPivotSampledSubjectIsVoxelStore(flatBlock, subjects, 186);
    };
    ASSERT_EQ(classify(), std::optional<bool>(false));

    IREntity::destroyEntity(sdf);
    m_entityManager.destroyMarkedEntities();
    const std::optional<bool> afterDestroy = classify();
    EXPECT_EQ(afterDestroy, std::nullopt);

    const float held = kDepthStart - DefaultPivotLatch::kCardinalStoreLatticeDepth;
    for (const float yaw : {0.0f, IRMath::kHalfPi, kPi, -IRMath::kHalfPi}) {
        const DefaultPivotLatch latch = latchReacquiredFrom(yaw, afterDestroy);
        EXPECT_EQ(latch.isoDepth(), held) << "yaw=" << yaw;
        EXPECT_EQ(latch.viewOffsetIso(), vec2(0.0f)) << "yaw=" << yaw;
    }
}

TEST(DefaultPivotLatch, AStampIsDecodedWithItsOwnSubdivisions) {
    // The composite stores depth × effective subdivisions, and the divisor is
    // zoom-dependent. A shot that changes zoom AND yaw in one snap acquires
    // from a frame at the old zoom while the live divisor is already the new
    // one; the stamped divisor is the right one.
    LatchDriver driver;
    const vec2 cameraIso = vec2(0.0f);
    driver.hold(0.0f, cameraIso, kDepthStart, 3);
    driver.stepWithInFrameYaw(0.0f, 0.0f, cameraIso, kDepthAfterPan, 2);
    EXPECT_TRUE(driver.stepWithInFrameYaw(kYawStep, kYawStep, cameraIso, kDepthAfterPan, 4));
    EXPECT_FLOAT_EQ(driver.isoDepth(), kDepthAfterPan);
}

// ---------------------------------------------------------------------------
// The mode gate: observe every frame, derive only when the default pivot owns
// the depth, and never source from a frame it did not own.
// ---------------------------------------------------------------------------

TEST(DefaultPivotLatch, ANonDefaultPivotPaysNoReadback) {
    LatchDriver driver;
    const vec2 cameraIso = vec2(0.0f);
    driver.hold(0.0f, cameraIso, kDepthStart, 3);
    driver.setPivotOwnsDepth(false);

    driver.step(kYawStep, cameraIso, kDepthAfterPan);
    driver.hold(kYawStep, vec2(100.0f, 100.0f), kDepthAfterPan, 5);
    driver.step(2.0f * kYawStep, vec2(100.0f, 100.0f), kDepthAfterPan);
    EXPECT_EQ(driver.derives(), 0);
    EXPECT_FALSE(driver.latch().hasAcquired());
}

TEST(DefaultPivotLatch, AFrameRenderedUnderANonDefaultPivotIsNotASource) {
    // The explicit-focus drag ends and the very next frame starts a default
    // gesture: the attachment it would read was drawn under the explicit
    // pivot, so the latch holds rather than acquire from it.
    LatchDriver driver;
    const vec2 cameraIso = vec2(0.0f);
    driver.hold(0.0f, cameraIso, kDepthStart, 3);
    driver.setPivotOwnsDepth(false);
    driver.hold(0.0f, cameraIso, kDepthAfterPan, 3);

    driver.setPivotOwnsDepth(true);
    EXPECT_FALSE(driver.step(kYawStep, cameraIso, kDepthAfterPan));
    EXPECT_FALSE(driver.latch().hasAcquired());

    // One owned still frame later, the next gesture acquires normally.
    driver.step(kYawStep, cameraIso, kDepthAfterPan);
    EXPECT_TRUE(driver.step(2.0f * kYawStep, cameraIso, kDepthAfterPan));
    EXPECT_TRUE(driver.latch().hasAcquired());
}

TEST(DefaultPivotLatch, AFrameThatNeverReachedTheCompositeIsNotASource) {
    // A creation with no TRIXEL_TO_FRAMEBUFFER never stamps: the edge fires
    // but there is nothing to read.
    DefaultPivotLatch latch;
    EXPECT_FALSE(latch.observeFrame(0.0f, true));
    EXPECT_FALSE(latch.observeFrame(0.0f, true));
    EXPECT_FALSE(latch.observeFrame(kYawStep, true));
    EXPECT_FALSE(latch.hasAcquired());
}

} // namespace
