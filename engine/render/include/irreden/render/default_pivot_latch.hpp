#ifndef DEFAULT_PIVOT_LATCH_H
#define DEFAULT_PIVOT_LATCH_H

#include <irreden/ir_math.hpp>

namespace IRRender {

// The camera pose one frame renders with — the whole input to the default
// pivot's latch-update policy.
struct DefaultPivotPose {
    float visualYaw_ = 0.0f;
    IRMath::vec2 cameraIso_ = IRMath::vec2(0.0f);
    IRMath::vec2 zoom_ = IRMath::vec2(0.0f);
};

// What DefaultPivotLatch::observeFrame decided for one frame. When derive_ is
// true exactly one of the two clauses below explains it — they are mutually
// exclusive by construction (rotationStart_ requires a yaw delta, viewMoved_
// requires a settled yaw).
struct DefaultPivotLatchDecision {
    bool derive_ = false;
    // The rotation-start edge: yaw is changing THIS frame, was settled the
    // previous one (so the depth attachment still on the GPU belongs to a
    // still view), and that previous frame's yaw was 0 within
    // DefaultPivotLatch::kYawSettleDelta (so the depth it reads pins the point
    // actually under the crosshair — see observeFrame).
    bool rotationStart_ = false;
    // Pan or zoom moved since the last derive and the camera has settled
    // again.
    bool viewMoved_ = false;
};

// Update policy for the depth-aware default pivot's latched iso depth: when
// may RenderManager pay a composite-depth readback and re-latch?
//
// Split out of RenderManager because the policy is the thing under contract
// (docs/design/camera-yaw-pivot.md §"The contract", latch policy) while the
// readback is the thing that needs a GPU. Nothing here touches the device, so
// the whole policy — including the rotation-start edge, which no
// pivot-verify.py block can observe because every block holds the camera fixed
// — is machine-gated headlessly by test/render/default_pivot_latch_test.cpp.
//
// NOT a dirty flag over caller-authored data: a derive costs a full GPU flush
// (single-pixel depth readback), and holding the depth WHILE yaw moves is the
// semantic requirement — re-deriving mid-rotation would chase the pivot across
// the very content it is pinning.
class DefaultPivotLatch {
  public:
    // Per-frame delta of ABSOLUTE visualYaw at or below which the camera counts
    // as not rotating, so the latch holds instead of re-deriving. Deliberately
    // local rather than IRPrefab::Camera::kResidualYawDeadband: that constant is
    // the one source for the *residual*-yaw predicate its four consumers share
    // (per-axis allocation gate, render path-select, the FrameData UBO, the
    // shadow bake), which is a different question, and borrowing it would
    // silently couple this settle threshold to a value tuned for those.
    // 1e-4 rad/frame is ~0.34 deg/s at 60 fps.
    //
    // Doubles as the rotation-start edge's yaw-0 tolerance (`|yaw| <= delta`):
    // the same "indistinguishable from zero" that decides whether yaw moved
    // decides whether the pre-rotation frame rendered in the world frame.
    static constexpr float kYawSettleDelta = 1e-4f;

    // Stamp `pose` as what this frame is about to render and decide whether the
    // latched depth may be re-derived now.
    //
    // Call EXACTLY ONCE per frame and in EVERY pivot mode: the pose stamps
    // describe what the frame renders, which is true whatever the pivot is, and
    // freezing them behind the mode gate let the first frame back on the default
    // pivot match a pose several frames stale and consume a depth attachment for
    // a different view. `pivotOwnsDepth` is that gate — false when the mode is
    // not CAMERA_CENTER or an explicit focus overrides the default, in which
    // case no readback may be paid but the pose is still observed.
    DefaultPivotLatchDecision observeFrame(const DefaultPivotPose &pose, bool pivotOwnsDepth) {
        // Settle predicate: the per-frame change in ABSOLUTE yaw, against this
        // class's own kYawSettleDelta. No residual and no computeYawSplit are
        // involved — see the constant for why it isn't the shared
        // Camera::kResidualYawDeadband.
        const bool yawSettled = IRMath::abs(pose.visualYaw_ - m_lastYaw) <= kYawSettleDelta;
        // The depth attachment a derive reads was written by the PREVIOUS frame
        // (beginFrame runs ahead of the RENDER pipeline), so it only describes
        // the current view once the previous frame rendered this same pan/zoom.
        // Deriving on the frame a pan lands would read the pre-pan image and
        // latch a depth for a view that no longer exists.
        const bool viewMatchesLastRender =
            pose.cameraIso_ == m_renderedCameraIso && pose.zoom_ == m_renderedZoom;
        const bool wasYawSettled = m_yawSettled;
        const bool wasYawZero = IRMath::abs(m_lastYaw) <= kYawSettleDelta;

        m_lastYaw = pose.visualYaw_;
        m_renderedCameraIso = pose.cameraIso_;
        m_renderedZoom = pose.zoom_;
        m_yawSettled = yawSettled;

        if (!pivotOwnsDepth || !viewMatchesLastRender) {
            return {};
        }

        DefaultPivotLatchDecision decision;
        if (!yawSettled) {
            // Rotation-start re-derive, gesture-start ONLY: the edge is "was
            // settled, is not now", so a continuous rotation derives once at its
            // first yaw-delta frame and never again while yaw keeps moving. A
            // drag that pauses for a frame and resumes re-arms the edge — the
            // paused frame's attachment is a still view.
            //
            // Yaw-0 ONLY, on top of that. The focus is
            // isoPixelToPos3D(viewCenterIso, isoDepth), an expression with no
            // yaw term, so a depth read from a frame rendered at non-zero yaw
            // pins a point that is NOT the one under the crosshair: the view
            // shifts, which changes what the next rotation start reads, and
            // successive derives walk the pivot into the background. Only the
            // world frame (yaw 0) is a fixed point of that map, so the edge
            // fires from there and the latch holds everywhere else — the
            // non-zero-yaw derive needs a yaw-aware readback-to-focus map, which
            // this policy does not have.
            decision.rotationStart_ = wasYawSettled && wasYawZero;
            decision.derive_ = decision.rotationStart_;
            return decision;
        }

        // Pan/zoom-scoped derive. A genuinely still camera does ZERO readbacks,
        // but the cost lands on every motion-stop frame of a real drag, not
        // once at startup.
        decision.viewMoved_ =
            !m_hasIsoDepth || pose.cameraIso_ != m_derivedCameraIso || pose.zoom_ != m_derivedZoom;
        decision.derive_ = decision.viewMoved_;
        return decision;
    }

    // Record the depth the derive `observeFrame` admitted actually read. Keyed
    // on the pose observeFrame just stamped, so a caller cannot key a derive to
    // a pose the policy did not decide against.
    void noteDerived(float isoDepth) {
        m_isoDepth = isoDepth;
        m_hasIsoDepth = true;
        m_derivedCameraIso = m_renderedCameraIso;
        m_derivedZoom = m_renderedZoom;
    }

    // Latched iso depth of the surface under the crosshair. 0 — before the
    // first derive, whenever the center pixel reads background, and for a
    // creation whose frame never reaches beginFrame — is the exact fallback
    // point, so the fallback is the same expression rather than a structurally
    // different branch.
    float isoDepth() const {
        return m_isoDepth;
    }
    bool hasIsoDepth() const {
        return m_hasIsoDepth;
    }

  private:
    float m_isoDepth = 0.0f;
    bool m_hasIsoDepth = false;

    // Camera state the PREVIOUS frame rendered with — the depth attachment a
    // derive reads belongs to that frame. Stamped every frame in every pivot
    // mode, so returning to CAMERA_CENTER cannot inherit a stale pose stamp.
    float m_lastYaw = 0.0f;
    IRMath::vec2 m_renderedCameraIso = IRMath::vec2(0.0f);
    // A live zoom is never 0, so this initialiser is the first-frame sentinel
    // that makes the pose check fail by construction before anything has
    // rendered — do not "tidy" it to a plausible default.
    IRMath::vec2 m_renderedZoom = IRMath::vec2(0.0f);
    // Was the previous frame's yaw settled? The rotation-start edge is this
    // going true -> false, which is why the flag has to persist across frames
    // rather than being recomputed from m_lastYaw alone.
    bool m_yawSettled = true;

    // Camera state the latched iso depth was derived from.
    IRMath::vec2 m_derivedCameraIso = IRMath::vec2(0.0f);
    IRMath::vec2 m_derivedZoom = IRMath::vec2(0.0f);
};

} // namespace IRRender

#endif /* DEFAULT_PIVOT_LATCH_H */
