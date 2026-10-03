#ifndef IR_PREFAB_CANVAS_POSE_H
#define IR_PREFAB_CANVAS_POSE_H

// The owner → canvas pose channel for detached entity canvases. One writer
// shape for the per-frame propagation system and for the sites that stand a
// canvas up mid-frame: a canvas created after PROPAGATE_CANVAS_ROTATION has
// run would otherwise reach RENDER with the "not a detached canvas" sentinel
// and raster in camera space for one frame.

#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>

namespace IRPrefab::CanvasPose {

/// Rotation of a world-space orientation in a detached canvas's model frame:
/// the entity rotation applied first, then the camera basis removed. The
/// world canvas re-applies that basis to the composited canvas, so the two
/// camera factors cancel and only the entity rotation remains in camera space.
inline IRMath::vec4 canvasRotation(IRMath::vec4 cameraRotationInverse, IRMath::vec4 worldRotation) {
    return IRMath::quatMul(cameraRotationInverse, worldRotation);
}

/// Offset of a world-space point from a canvas owner, in that canvas's model
/// frame — the translation twin of `canvasRotation`.
inline IRMath::vec3 canvasOffset(
    IRMath::vec4 cameraRotationInverse, IRMath::vec3 worldPoint, IRMath::vec3 ownerWorldTranslation
) {
    return IRMath::rotateVectorByQuat(worldPoint - ownerWorldTranslation, cameraRotationInverse);
}

/// Stamp @p canvas with its owner's pose for this frame. @p mode must be one
/// of the canvas-owning rotation modes.
inline void write(
    IRComponents::C_CanvasLocalRotation &canvas,
    IRMath::vec4 cameraRotationInverse,
    const IRComponents::C_WorldTransform &ownerWorld,
    IRComponents::RotationMode mode,
    const IRComponents::C_EntityCanvas &entityCanvas
) {
    canvas.rotation_ = canvasRotation(cameraRotationInverse, ownerWorld.rotation_);
    canvas.reVoxelize_ = mode == IRComponents::RotationMode::DETACHED_REVOXELIZE;
    // The screen-space lighting passes iterate the canvas, not the owner, so
    // the placement they need travels here: whether the canvas is world-placed,
    // whether it casts, and the world cell it sits on. The cell rounding matches
    // the composite depth offset (pos3DtoDistance(roundVec3HalfUp(translation)))
    // so depth and receive stay on one convention.
    canvas.worldPlaced_ = !entityCanvas.screenLocked_;
    canvas.castsWorldShadow_ = !entityCanvas.screenLocked_ && entityCanvas.visible_;
    canvas.worldCellOffset_ = IRMath::vec3(IRMath::roundVec3HalfUp(ownerWorld.translation_));
    canvas.ownerWorldTranslation_ = ownerWorld.translation_;
}

} // namespace IRPrefab::CanvasPose

#endif /* IR_PREFAB_CANVAS_POSE_H */
