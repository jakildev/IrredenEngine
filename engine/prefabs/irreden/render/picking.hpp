#ifndef IR_PREFAB_PICKING_H
#define IR_PREFAB_PICKING_H

// Editor-side voxel picking: convert the cursor into a world-space ray
// (in the engine's isometric projection) and find the first SDF shape
// it intersects. The picking inverse is the same one used by
// `IRRender::mouseWorldPos3DAtIsoDepth`, so the recovered world point
// matches what the voxel rasterizer wrote at any visualYaw.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <optional>
#include <vector>

namespace IRPrefab::Picking {

struct RayHit {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    IRMath::vec3 worldHitPos_ = IRMath::vec3(0.0f);
    IRMath::ivec3 voxelPos_ = IRMath::ivec3(0);
};

// Step size along the canvas-frame iso depth axis. One iso-depth unit
// moves the recovered 3D point by (1/3, 1/3, 1/3) in the rotated canvas
// frame, so 0.5 ≈ 0.17 voxel — sub-voxel granularity that avoids
// missing the surface of any 1-voxel-thick shape.
inline constexpr float kPickingDepthStep = 0.5f;

// Margin added past the union of shape iso-depth bounds when walking
// the ray. Guards against half-voxel rounding at the entry/exit
// surface; a hit one step shy of the bounding box is still caught.
inline constexpr float kPickingDepthMargin = 4.0f;

namespace detail {

struct ShapeSnapshot {
    IREntity::EntityId entity_;
    IRMath::vec3 worldPos_;
    IRRender::ShapeType type_;
    IRMath::vec4 params_;
    float boundingRadius_;
    float isoDepthMin_;
    float isoDepthMax_;
};

inline std::vector<ShapeSnapshot>
gatherVisibleShapes(IRMath::CardinalIndex cardinalIndex, IREntity::EntityId excludeEntity) {
    std::vector<ShapeSnapshot> snapshot;
    IREntity::forEachComponent<IRComponents::C_ShapeDescriptor>(
        [&](IREntity::EntityId id, IRComponents::C_ShapeDescriptor &sd) {
            if (id == excludeEntity)
                return;
            if (!(sd.flags_ & IRRender::SHAPE_FLAG_VISIBLE))
                return;

            // forEachComponent iterates one component type; positions are
            // fetched per-entity because the API has no multi-component
            // form. Safe: createEntity guarantees both components on every
            // entity. Acceptable: this path fires on click frames only.
            auto &gpos = IREntity::getComponent<IRComponents::C_PositionGlobal3D>(id);
            auto &opos = IREntity::getComponent<IRComponents::C_PositionOffset3D>(id);
            const IRMath::vec3 worldPos = gpos.pos_ + opos.pos_;
            const IRMath::vec3 rotatedPos = IRMath::rotateCardinalZ(worldPos, cardinalIndex);
            const IRMath::vec3 boundHalf = IRMath::SDF::boundingHalf(sd.shapeType_, sd.params_);

            // Iso depth in the rotated canvas frame = sum components of
            // the rotated world position. The shape's iso-depth bounds
            // span its bounding-box projection onto the (1,1,1) axis.
            const float center = rotatedPos.x + rotatedPos.y + rotatedPos.z;
            const float halfRange = boundHalf.x + boundHalf.y + boundHalf.z;

            snapshot.push_back(
                {id,
                 worldPos,
                 sd.shapeType_,
                 sd.params_,
                 IRMath::SDF::boundingRadius(sd.shapeType_, sd.params_),
                 center - halfRange - kPickingDepthMargin,
                 center + halfRange + kPickingDepthMargin}
            );
        }
    );
    return snapshot;
}

} // namespace detail

// Casts a ray from the cursor into the scene and returns the first
// shape it hits, or std::nullopt if the ray misses everything. The
// caller passes the editor's highlight entity (or any entity to be
// skipped) so the highlight itself doesn't catch its own ray.
//
// Algorithm: walk along the canvas-frame iso depth axis in
// `kPickingDepthStep` increments over the union of all visible shape
// iso-depth ranges. At each step, evaluate every shape's SDF at the
// recovered world point; the first surface (`SDF::evaluate <=
// kSurfaceThreshold`) wins. The depth-range pre-filter keeps the
// per-step cost proportional to the number of shapes whose bounding
// box overlaps that depth slice, not the total scene shape count.
inline std::optional<RayHit>
castVoxelRay(IREntity::EntityId excludeEntity = IREntity::kNullEntity) {
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
    const IRMath::CardinalIndex cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);

    auto shapes = detail::gatherVisibleShapes(cardinalIndex, excludeEntity);
    if (shapes.empty())
        return std::nullopt;

    float depthMin = shapes[0].isoDepthMin_;
    float depthMax = shapes[0].isoDepthMax_;
    for (const auto &s : shapes) {
        depthMin = IRMath::min(depthMin, s.isoDepthMin_);
        depthMax = IRMath::max(depthMax, s.isoDepthMax_);
    }

    for (float d = depthMin; d <= depthMax; d += kPickingDepthStep) {
        const IRMath::vec3 worldPoint = IRRender::mouseWorldPos3DAtIsoDepth(d);
        for (const auto &s : shapes) {
            if (d < s.isoDepthMin_ || d > s.isoDepthMax_)
                continue;
            const IRMath::vec3 localPos = worldPoint - s.worldPos_;
            // Cheap bounding-sphere reject before the full SDF eval.
            if (IRMath::dot(localPos, localPos) > s.boundingRadius_ * s.boundingRadius_)
                continue;
            const IRMath::vec4 effective = IRMath::SDF::effectiveParams(s.type_, s.params_);
            const float dist = IRMath::SDF::evaluate(localPos, s.type_, effective);
            if (dist <= IRMath::SDF::kSurfaceThreshold) {
                return RayHit{s.entity_, worldPoint, IRMath::roundVec3HalfUp(worldPoint)};
            }
        }
    }

    return std::nullopt;
}

} // namespace IRPrefab::Picking

#endif /* IR_PREFAB_PICKING_H */
