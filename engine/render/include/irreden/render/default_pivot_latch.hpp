#ifndef DEFAULT_PIVOT_LATCH_H
#define DEFAULT_PIVOT_LATCH_H

#include <irreden/ir_math.hpp>

namespace IRRender {

// The pose one main-framebuffer composite drew with — everything needed to
// turn a depth sample off that frame's depth attachment back into the world
// point it came from. Stamped at the composite, not at beginFrame: a camera
// system that mutates yaw inside RENDER ahead of geometry, and an
// auto-screenshot shot applied at the RENDER tail, both make the beginFrame
// observation a different pose from the one the attachment was drawn with.
struct DefaultPivotSourceFrame {
    float visualYaw_ = 0.0f;
    // `IRPrefab::Camera::computeYawSplit`'s residual for visualYaw_: exactly 0
    // at a settled cardinal, where the frame took the cardinal store rather
    // than the per-axis one.
    float residualYaw_ = 0.0f;
    // The raw camera (`C_Position2DIso`) and the pivot-corrected offset the
    // composite placed content with (`getEffectiveCameraIso`).
    IRMath::vec2 cameraIso_ = IRMath::vec2(0.0f);
    IRMath::vec2 effectiveCameraIso_ = IRMath::vec2(0.0f);
    // Iso coordinate of the main canvas center with no camera applied: a world
    // point W sits under the crosshair when `P_yaw(W) + effectiveCameraIso ==
    // canvasCenterIso`.
    IRMath::vec2 canvasCenterIso_ = IRMath::vec2(0.0f);
    // The composite stores depth in shared framebuffer units (world iso depth
    // × effective subdivisions). The divisor is zoom-dependent in
    // SubdivisionMode::FULL, and the frame a gesture acquires from can be at a
    // different zoom than the frame that reads it back.
    int effectiveSubdivisions_ = 1;
};

// Update policy and state of the depth-aware default pivot: when
// RenderManager may pay a composite-depth readback, and what the sample it
// reads turns into.
//
// Split out of RenderManager because the policy is the thing under contract
// (docs/design/camera-yaw-pivot.md §"Latch policy") while the readback is the
// thing that needs a GPU. Nothing here touches the device, so the whole policy
// and the recovery arithmetic are machine-gated headlessly by
// test/render/default_pivot_latch_test.cpp.
//
// The latch is ROTATION-scoped. It acquires once per rotation gesture — the
// frame yaw starts moving, at any yaw — from the surface under the crosshair
// in the frame before, and holds that anchor for the gesture. A pan or zoom
// never re-latches: between gestures the anchor rides the camera so the pan
// identity `IRMath::cameraMoveRelativeToYaw` pre-compensates holds, and
// nothing about the view jumps when the camera stops.
//
// State is an un-yawed iso DEPTH and a view offset in iso units. The focus is
// `isoPixelToPos3D(viewCenterIso − viewOffsetIso, isoDepth)` — derived live
// from the camera, so a pan transports it — and the offset is also added to the
// camera by every CAMERA_CENTER branch of `getEffectiveCameraIso`. With both
// terms the anchor stays at the canvas center for any offset, and an
// acquisition at non-zero yaw can re-anchor onto a different point of the same
// crosshair ray without moving the view. Offset zero is the pre-acquisition
// state and stays bit-exactly zero until a gesture acquires at non-zero yaw.
class DefaultPivotLatch {
  public:
    // Per-frame delta of ABSOLUTE visualYaw at or below which the camera counts
    // as not rotating. Deliberately local rather than
    // IRPrefab::Camera::kResidualYawDeadband: that constant is the one source
    // for the *residual*-yaw predicate its four consumers share (per-axis
    // allocation gate, render path-select, the FrameData UBO, the shadow bake),
    // which is a different question, and borrowing it would silently couple
    // this settle threshold to a value tuned for those.
    // 1e-4 rad/frame is ~0.34 deg/s at 60 fps.
    //
    // Doubles as the yaw-0 tolerance of an acquisition (`|yaw| <= delta`):
    // a source frame the settle predicate cannot tell from yaw 0 latches its
    // depth directly and leaves the view offset untouched.
    static constexpr float kYawSettleDelta = 1e-4f;

    // How far behind the visible surface the cardinal store keys a fragment,
    // in yawed iso-depth units. At residual yaw 0 each face is keyed on the
    // lower-corner lattice `[p, p + 1]` of view space rather than on the
    // authored cube `[p - 1/2, p + 1/2]`: a (1/2, 1/2, 1/2) shift along the view
    // axis, invisible on screen, that puts every cardinal key 1.5 units deeper
    // than the surface the pixel shows. The per-axis (non-cardinal) store keys
    // without it. Removing it at a cardinal source makes the acquired point the
    // surface itself, to within one micro-face.
    static constexpr float kCardinalStoreLatticeDepth = 1.5f;

    // Record the pose the main composite is drawing this frame with, or — when
    // the default pivot does not own the depth (ORIGIN mode, or an explicit
    // focus) — that this frame's attachment is unusable as a source. A frame
    // that never reaches the composite leaves no stamp at all.
    void stampSourceFrame(const DefaultPivotSourceFrame &frame, bool pivotOwnsDepth) {
        m_pendingSource = frame;
        m_hasPendingSource = pivotOwnsDepth;
    }

    // Observe the yaw this frame starts with and decide whether a readback may
    // be paid now. Call EXACTLY ONCE per frame, in EVERY pivot mode, ahead of
    // the RENDER pipeline: the previous frame's stamp becomes the source a
    // derive this frame reads, and the settle state has to track every frame
    // or the first frame back on the default pivot would compare against a
    // stale yaw.
    //
    // True on the gesture-start edge — yaw settled last frame, moving this one —
    // when the default pivot owns the depth and the previous frame left a stamp.
    // A continuous rotation derives once, at its first yaw-delta frame; a drag
    // that pauses for a frame and resumes is a new gesture and acquires again,
    // at whatever yaw it paused. A settled camera, and any pan or zoom, derives
    // nothing.
    bool observeFrame(float visualYaw, bool pivotOwnsDepth) {
        m_source = m_pendingSource;
        m_hasSource = m_hasPendingSource;
        m_hasPendingSource = false;

        const bool yawSettled =
            !m_hasObservedFrame || IRMath::abs(visualYaw - m_lastYaw) <= kYawSettleDelta;
        const bool gestureStart = m_yawSettled && !yawSettled;
        m_hasObservedFrame = true;
        m_lastYaw = visualYaw;
        m_yawSettled = yawSettled;
        return gestureStart && pivotOwnsDepth && m_hasSource;
    }

    // The frame a derive admitted by observeFrame reads its depth from.
    const DefaultPivotSourceFrame &sourceFrame() const {
        return m_source;
    }

    // Acquire the surface a derive read: @p framebufferIsoDepth is the decoded
    // composite depth at the canvas center of the source frame, in framebuffer
    // units. A background or foreground-tier sample is NOT passed here — the
    // latch holds instead, so a crosshair over nothing keeps the previous anchor
    // rather than jumping to the depth-0 point.
    //
    // The world point is recovered in the frame the source was drawn in —
    // its yaw and its effective camera — so it projects to the pixel it was
    // read from, and the new state leaves the effective camera of the source
    // pose unchanged: acquisition never moves the view. A cardinal source's
    // sample is first moved off the store's lattice onto the visible surface
    // (kCardinalStoreLatticeDepth).
    void acquire(float framebufferIsoDepth) {
        const DefaultPivotSourceFrame &source = m_source;
        float yawedIsoDepth =
            framebufferIsoDepth / static_cast<float>(IRMath::max(1, source.effectiveSubdivisions_));
        if (source.residualYaw_ == 0.0f) {
            yawedIsoDepth -= kCardinalStoreLatticeDepth;
        }
        m_hasAcquired = true;
        if (IRMath::abs(source.visualYaw_) <= kYawSettleDelta) {
            // At yaw 0 the yawed depth IS the un-yawed depth and the focus
            // expression already puts it under the crosshair. Recomputing the
            // offset would round-trip it through isoPixelToPos3D and
            // pos3DtoPos2DIso, which is not a float identity — and yaw-0
            // frames depend on the offset alone, so it must not drift.
            m_isoDepth = yawedIsoDepth;
            return;
        }
        const IRMath::vec3 world = IRMath::isoPixelToPos3DYawed(
            source.canvasCenterIso_ - source.effectiveCameraIso_,
            yawedIsoDepth,
            source.visualYaw_
        );
        m_isoDepth = world.x + world.y + world.z;
        m_viewOffsetIso =
            source.canvasCenterIso_ - source.cameraIso_ - IRMath::pos3DtoPos2DIso(world);
    }

    // The anchor under the view center @p viewCenterIso (`canvasCenterIso −
    // cameraIso` for the live camera).
    IRMath::vec3 focus(IRMath::vec2 viewCenterIso) const {
        return IRMath::isoPixelToPos3D(viewCenterIso - m_viewOffsetIso, m_isoDepth);
    }

    // Un-yawed iso depth of the anchor. 0 — before the first acquisition, and
    // for a creation whose frame never reaches beginFrame — is the exact
    // fallback point, so the fallback is the same expression rather than a
    // structurally different branch.
    float isoDepth() const {
        return m_isoDepth;
    }
    IRMath::vec2 viewOffsetIso() const {
        return m_viewOffsetIso;
    }
    bool hasAcquired() const {
        return m_hasAcquired;
    }

  private:
    float m_isoDepth = 0.0f;
    IRMath::vec2 m_viewOffsetIso = IRMath::vec2(0.0f);
    bool m_hasAcquired = false;

    // The stamp the composite left this frame, and the one the previous frame
    // left — the source a derive in this frame's beginFrame reads.
    DefaultPivotSourceFrame m_pendingSource;
    bool m_hasPendingSource = false;
    DefaultPivotSourceFrame m_source;
    bool m_hasSource = false;

    float m_lastYaw = 0.0f;
    // The first observed frame has no previous yaw, so it is settled by
    // definition — a creation that starts at non-zero yaw is not a gesture.
    bool m_hasObservedFrame = false;
    // Was the previous frame's yaw settled? The gesture-start edge is this
    // going true -> false, which is why the flag has to persist across frames
    // rather than being recomputed from m_lastYaw alone.
    bool m_yawSettled = true;
};

} // namespace IRRender

#endif /* DEFAULT_PIVOT_LATCH_H */
