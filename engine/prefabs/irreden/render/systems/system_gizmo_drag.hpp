#ifndef SYSTEM_GIZMO_DRAG_H
#define SYSTEM_GIZMO_DRAG_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>

namespace IRSystem {

// Editor gizmo drag — drives the interaction state machine over the
// per-frame hover flag GIZMO_HOVER just wrote. One drag is active at a
// time; the dragged handle's anchor entity (typically the gizmo group)
// gets its C_LocalTransform / accumulated rotation / accumulated scale
// updated until the mouse button releases.
//
// Pipeline: INPUT, after GIZMO_HOVER. Mutates anchor
// `C_LocalTransform::translation_` (translate) and
// `C_LocalTransform::rotation_` (rotate, #1610 — FK pose editing reads
// this through PROPAGATE_TRANSFORM + the skeletal skinning substrate);
// scale still only accumulates on the system params — render-side
// application is a follow-up once a canonical scale component lands.
//
// Drag math (translate):
//   - At press, capture (a) the anchor's local position, (b) the
//     cursor's world point projected onto a fixed iso-depth plane
//     through the anchor.
//   - Each frame, recompute the cursor world point at THAT SAME plane
//     and project the world delta onto the handle's unit axis.
//   - anchor.translation_ = startPos + axisUnit * dot(worldDelta, axisUnit).
//   Fixing the plane prevents the gizmo from running away from the
//   cursor as its iso depth changes.
//
// Drag math (rotate):
//   - Use screen-space angle of cursor around the anchor's iso pixel.
//     Phase 3 MVP: same screen-space angle for all three rings; later
//     refinement can project the ring plane into screen and use a
//     true axis-perpendicular angle.
//   - Shift held → angle snaps to `kRotateSnapStep` (π/12 = 15°).
//   - anchor.rotation_ = pressRotation ∘ quatAxisAngle(ringAxis, delta):
//     post-multiplying rotates about the anchor's LOCAL axis — the rings
//     are children of the anchor, so the grabbed ring's color always
//     names the local axis the user sees. Composed fresh from the
//     press-time rotation each frame (not incrementally), so a long
//     drag accumulates no quat drift.
//
// Drag math (scale):
//   - SCALE_CENTER: cursor screen-distance from press point → uniform
//     scale delta.
//   - SCALE_STICK: world axis projection (same as translate) → single-
//     axis scale delta.
template <> struct System<GIZMO_DRAG> {
    // Active drag — kNullEntity when no handle is being dragged.
    IREntity::EntityId dragHandle_ = IREntity::kNullEntity;
    IREntity::EntityId dragAnchor_ = IREntity::kNullEntity;
    IRComponents::GizmoKind dragKind_ = IRComponents::GizmoKind::TRANSLATE_ARROW;
    IRComponents::GizmoAxis dragAxis_ = IRComponents::GizmoAxis::NONE;

    // Press-time captures.
    IRMath::vec3 dragStartAnchorPos_ = IRMath::vec3(0.0f);
    IRMath::vec4 dragStartAnchorRot_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    IRMath::vec3 dragStartCursorWorld_ = IRMath::vec3(0.0f);
    float dragPlaneIsoDepth_ = 0.0f;
    IRMath::vec2 dragStartCursorScreen_ = IRMath::vec2(0.0f);
    IRMath::vec2 dragAnchorScreenIso_ = IRMath::vec2(0.0f);
    float dragStartRotateAngle_ = 0.0f;

    // Per-drag readouts for the rotate / scale gestures. applyDrag()
    // OVERWRITES each frame with a fresh delta from THAT drag's press
    // point — successive drags do NOT numerically stack here. Storage
    // is keyed by anchor so beginDrag() can reset to identity when the
    // gizmo target changes, keeping stale values from a previous
    // gizmo's drag from appearing as the new gizmo's current state.
    // endTick() reads these on the press→release transition to log the
    // final per-drag value. Wiring rotate / scale to an accumulating
    // render-side component is a follow-up — the canonical channel is
    // the quat on C_LocalTransform plus a future C_Scale.
    IREntity::EntityId accumOwner_ = IREntity::kNullEntity;
    float accumRotateAngle_ = 0.0f;
    float accumScaleUniform_ = 1.0f;
    IRMath::vec3 accumScalePerAxis_ = IRMath::vec3(1.0f);

    // Per-frame input state.
    bool mouseLeftPressedThisFrame_ = false;
    bool mouseLeftReleasedThisFrame_ = false;
    bool mouseLeftDown_ = false;
    bool shiftHeld_ = false;
    IRMath::vec2 mouseScreen_ = IRMath::vec2(0.0f);
    IRMath::vec2 mouseCanvasIso_ = IRMath::vec2(0.0f);

    // 15 degrees in radians for shift-snap rotate.
    static constexpr float kRotateSnapStep = IRMath::kPi / 12.0f;
    // Pixel reference scale for SCALE_CENTER uniform sensitivity.
    static constexpr float kScaleCenterRefPixels = 200.0f;
    // World-unit reference scale for SCALE_STICK per-axis sensitivity.
    static constexpr float kScaleStickRefWorld = 8.0f;

    // Track previous drag handle across frames so the release transition
    // (any → kNullEntity) fires a one-shot final-state log; useful for
    // verifying ROTATE_RING / SCALE_* whose state lives on the system
    // params and isn't yet wired to a render-visible rotation / scale
    // component on the anchor.
    IREntity::EntityId prevDragHandle_ = IREntity::kNullEntity;

    void beginTick() {
        mouseLeftPressedThisFrame_ = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::PRESSED
        );
        mouseLeftReleasedThisFrame_ = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::RELEASED
        );
        const bool held = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::HELD
        );
        mouseLeftDown_ = held || mouseLeftPressedThisFrame_;
        shiftHeld_ = IRInput::checkKeyMouseButton(
                         IRInput::KeyMouseButtons::kKeyButtonLeftShift,
                         IRInput::ButtonStatuses::HELD
                     ) ||
                     IRInput::checkKeyMouseButton(
                         IRInput::KeyMouseButtons::kKeyButtonRightShift,
                         IRInput::ButtonStatuses::HELD
                     );
        mouseScreen_ = IRInput::getMousePositionScreen();
        mouseCanvasIso_ = IRRender::mousePosition2DIsoWorldRender();

        if (dragHandle_ != IREntity::kNullEntity && !mouseLeftDown_) {
            // Release on previous frame's HELD slipping to RELEASED+up.
            dragHandle_ = IREntity::kNullEntity;
        }
    }

    void tick(IREntity::EntityId id, IRComponents::C_GizmoHandle &handle) {
        // 1) Drag-start: this handle is hovered + mouse just pressed + no other drag.
        if (dragHandle_ == IREntity::kNullEntity && mouseLeftPressedThisFrame_ && handle.hover_ &&
            handle.anchorEntity_ != IREntity::kNullEntity) {
            beginDrag(id, handle);
        }

        // 2) Drag-update: this handle is the active one, mouse still down.
        if (dragHandle_ == id && mouseLeftDown_) {
            applyDrag();
        }
    }

    void endTick() {
        if (prevDragHandle_ != IREntity::kNullEntity && dragHandle_ == IREntity::kNullEntity) {
            // Drag released this frame. Log final state per kind so the
            // ROTATE_RING / SCALE_* gestures are reviewable in headless
            // smoke runs (no render-visible rotation/scale yet).
            switch (dragKind_) {
            case IRComponents::GizmoKind::ROTATE_RING:
                IRE_LOG_INFO(
                    "GIZMO_DRAG ROTATE_RING axis={} release: angle={:.4f} rad ({})",
                    static_cast<int>(dragAxis_),
                    accumRotateAngle_,
                    shiftHeld_ ? "shift-snapped" : "free"
                );
                break;
            case IRComponents::GizmoKind::SCALE_CENTER:
                IRE_LOG_INFO("GIZMO_DRAG SCALE_CENTER release: uniform={:.4f}", accumScaleUniform_);
                break;
            case IRComponents::GizmoKind::SCALE_STICK:
                IRE_LOG_INFO(
                    "GIZMO_DRAG SCALE_STICK axis={} release: perAxis=({:.4f},{:.4f},{:.4f})",
                    static_cast<int>(dragAxis_),
                    accumScalePerAxis_.x,
                    accumScalePerAxis_.y,
                    accumScalePerAxis_.z
                );
                break;
            default:
                break;
            }
        }
        prevDragHandle_ = dragHandle_;
    }

    void beginDrag(IREntity::EntityId id, const IRComponents::C_GizmoHandle &handle) {
        dragHandle_ = id;
        dragAnchor_ = handle.anchorEntity_;
        dragKind_ = handle.kind_;
        dragAxis_ = handle.axis_;

        auto &anchorLocal = IREntity::getComponent<IRComponents::C_LocalTransform>(dragAnchor_);
        auto &anchorWorldTransform =
            IREntity::getComponent<IRComponents::C_WorldTransform>(dragAnchor_);
        const IRMath::vec3 anchorWorld = anchorWorldTransform.translation_;

        dragStartAnchorPos_ = anchorLocal.translation_;
        dragStartAnchorRot_ = anchorLocal.rotation_;
        dragPlaneIsoDepth_ = canvasIsoDepthOfAnchor(anchorWorld);
        dragStartCursorWorld_ = IRRender::mouseWorldPos3DAtIsoDepth(dragPlaneIsoDepth_);
        dragStartCursorScreen_ = mouseScreen_;
        dragAnchorScreenIso_ = anchorIsoPosition(anchorWorld);

        // Reset per-anchor accumulators when the anchor changes so the
        // rotate / scale displays don't carry over between different
        // gizmos.
        if (accumOwner_ != dragAnchor_) {
            accumOwner_ = dragAnchor_;
            accumRotateAngle_ = 0.0f;
            accumScaleUniform_ = 1.0f;
            accumScalePerAxis_ = IRMath::vec3(1.0f);
        }

        dragStartRotateAngle_ = cursorCanvasAngle(mouseCanvasIso_);
    }

    void applyDrag() {
        auto &anchorLocal = IREntity::getComponent<IRComponents::C_LocalTransform>(dragAnchor_);

        switch (dragKind_) {
        case IRComponents::GizmoKind::TRANSLATE_ARROW: {
            const IRMath::vec3 axisUnit = axisToUnit(dragAxis_);
            const IRMath::vec3 cursorWorld =
                IRRender::mouseWorldPos3DAtIsoDepth(dragPlaneIsoDepth_);
            const IRMath::vec3 worldDelta = cursorWorld - dragStartCursorWorld_;
            const float along = IRMath::dot(worldDelta, axisUnit);
            anchorLocal.translation_ = dragStartAnchorPos_ + axisUnit * along;
            break;
        }
        case IRComponents::GizmoKind::ROTATE_RING: {
            const float currentAngle = cursorCanvasAngle(mouseCanvasIso_);
            float delta = IRMath::wrapAnglePi(currentAngle - dragStartRotateAngle_);
            if (shiftHeld_) {
                delta = IRMath::round(delta / kRotateSnapStep) * kRotateSnapStep;
            }
            accumRotateAngle_ = delta;
            // Post-multiply = rotation about the anchor's LOCAL ring axis;
            // composed fresh from the press-time rotation so the drag
            // accumulates no quat drift (see the header drag-math note).
            anchorLocal.rotation_ = IRMath::quatMul(
                dragStartAnchorRot_,
                IRMath::quatAxisAngle(axisToUnit(dragAxis_), delta)
            );
            break;
        }
        case IRComponents::GizmoKind::SCALE_STICK: {
            const IRMath::vec3 axisUnit = axisToUnit(dragAxis_);
            const IRMath::vec3 cursorWorld =
                IRRender::mouseWorldPos3DAtIsoDepth(dragPlaneIsoDepth_);
            const IRMath::vec3 worldDelta = cursorWorld - dragStartCursorWorld_;
            const float along = IRMath::dot(worldDelta, axisUnit);
            const float factor = 1.0f + along / kScaleStickRefWorld;
            IRMath::vec3 perAxis = IRMath::vec3(1.0f);
            switch (dragAxis_) {
            case IRComponents::GizmoAxis::X:
                perAxis.x = factor;
                break;
            case IRComponents::GizmoAxis::Y:
                perAxis.y = factor;
                break;
            case IRComponents::GizmoAxis::Z:
                perAxis.z = factor;
                break;
            default:
                break;
            }
            accumScalePerAxis_ = perAxis;
            break;
        }
        case IRComponents::GizmoKind::SCALE_CENTER: {
            const IRMath::vec2 screenDelta = mouseScreen_ - dragStartCursorScreen_;
            const float signedMag = screenDelta.x + screenDelta.y; // diagonal-positive convention
            accumScaleUniform_ = 1.0f + signedMag / kScaleCenterRefPixels;
            break;
        }
        default:
            break;
        }
    }

    static SystemId create() {
        return registerSystem<GIZMO_DRAG, IRComponents::C_GizmoHandle>("GizmoDrag");
    }

  private:
    static IRMath::vec3 axisToUnit(IRComponents::GizmoAxis axis) {
        switch (axis) {
        case IRComponents::GizmoAxis::X:
            return IRMath::vec3(1.0f, 0.0f, 0.0f);
        case IRComponents::GizmoAxis::Y:
            return IRMath::vec3(0.0f, 1.0f, 0.0f);
        case IRComponents::GizmoAxis::Z:
            return IRMath::vec3(0.0f, 0.0f, 1.0f);
        case IRComponents::GizmoAxis::NONE:
        default:
            return IRMath::vec3(0.0f);
        }
    }

    static float canvasIsoDepthOfAnchor(IRMath::vec3 worldPos) {
        const IRMath::CardinalIndex idx =
            IRMath::rasterYawCardinalIndex(IRPrefab::Camera::getRasterYaw());
        const IRMath::vec3 rotated = IRMath::rotateCardinalZ(worldPos, idx);
        return static_cast<float>(IRMath::pos3DtoDistance(rotated));
    }

    static IRMath::vec2 anchorIsoPosition(IRMath::vec3 worldPos) {
        const IRMath::CardinalIndex idx =
            IRMath::rasterYawCardinalIndex(IRPrefab::Camera::getRasterYaw());
        const IRMath::vec3 rotated = IRMath::rotateCardinalZ(worldPos, idx);
        return IRMath::pos3DtoPos2DIso(rotated);
    }

    // Canvas-iso-space angle of the cursor measured from the anchor's
    // iso position, in radians. Both terms live in the rasterYaw-
    // rotated canvas iso frame (mousePosition2DIsoWorldRender ↔
    // pos3DtoPos2DIso(rotateCardinalZ(worldPos))) so the relative
    // vector is camera-pan invariant. IRMath::wrapAnglePi normalizes
    // deltas across the ±π discontinuity.
    float cursorCanvasAngle(IRMath::vec2 cursorCanvas) const {
        const IRMath::vec2 delta = cursorCanvas - dragAnchorScreenIso_;
        return IRMath::atan2(delta.y, delta.x);
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GIZMO_DRAG_H */
