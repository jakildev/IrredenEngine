#ifndef IR_PREFAB_PICKING_H
#define IR_PREFAB_PICKING_H

// Editor-side voxel picking: convert the cursor into a world-space ray
// (in the engine's isometric projection) and find the first SDF shape
// or `C_VoxelSetNew` voxel it intersects. The picking inverse is the
// same one used by `IRRender::mouseWorldPos3DAtIsoDepth`, so the
// recovered world point matches what the voxel rasterizer wrote at any
// visualYaw.
//
// CPU-side vs GPU-readback picking — pick the right path:
//
//   - **CPU-side (`castVoxelRay`, this file)** — the default for click-
//     driven editor input. No frame lag: the call returns a hit on the
//     same frame as the click. Yields the sub-voxel surface intersection
//     point AND the face normal of the hit (`RayHit::faceNormal_`), so
//     downstream "place adjacent" math has everything it needs without a
//     second round-trip. Cost per click is proportional to visible
//     shapes × depth range PLUS visible `C_VoxelSetNew` entities × depth
//     range (each set is tested via an inverse-lookup on its grid, NOT
//     per-voxel — so cost is independent of voxel count per set). Tens
//     of voxel sets in a scene is fine; scenes with hundreds of
//     thousands of active voxels distributed across many sets should
//     prefer the GPU path below.
//
//   - **GPU readback (`IRRender::getEntityIdAtMouseTrixel`)** — the O(1)
//     GPU alternative. Reads the entity-id texture written by
//     `VOXEL_TO_TRIXEL_STAGE_2` and `SHAPES_TO_TRIXEL` (via a
//     persistent-mapped buffer) and returns the frontmost entity at the
//     cursor pixel in O(1). The trade-off is a one-frame lag (the buffer
//     holds the previous frame's render) and it yields the entity only
//     — no sub-voxel surface hit position, no face normal. Use when the
//     scene has many active voxels and per-click CPU cost dominates, or
//     when only the entity id matters (selection without placement).

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace IRPrefab::Picking {

struct RayHit {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    IRMath::vec3 worldHitPos_ = IRMath::vec3(0.0f);
    IRMath::ivec3 voxelPos_ = IRMath::ivec3(0);
    // Outward unit normal of the hit face along the dominant axis of
    // `worldHitPos_ - voxelCenter` (±1 on exactly one axis). Only
    // meaningful for voxel hits from `C_VoxelSetNew`; left at (0,0,0)
    // for shape hits (the cube-face convention doesn't carry meaning
    // for non-box SDF surfaces).
    IRMath::ivec3 faceNormal_ = IRMath::ivec3(0);
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

// Snapshot of one `C_VoxelSetNew` entity for the picking walk. Voxel
// hits are resolved by inverse-lookup against the set's local grid
// (O(1) per depth step per set) rather than SDF-testing every active
// voxel — the local-position layout is integer-offset from
// `worldOrigin_`, so the candidate voxel at any world point is a single
// `roundVec3HalfUp` away.
struct VoxelSetSnapshot {
    IREntity::EntityId entity_;
    // World position of the voxel at local index (0,0,0). All other
    // voxels in the set sit at integer offsets from this origin.
    IRMath::vec3 worldOrigin_;
    IRMath::ivec3 size_;
    std::span<const IRComponents::C_Voxel> voxels_;
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

            // forEachComponent iterates one component type; position is
            // fetched per-entity because the API has no multi-component
            // form. Safe: createEntity guarantees C_PositionGlobal3D on
            // every entity. Acceptable: this path fires on click frames
            // only. Reader migrates to C_WorldTransform.translation_ in
            // T-299 (render-side SQT reader pass).
            auto &gpos = IREntity::getComponent<IRComponents::C_PositionGlobal3D>(id);
            const IRMath::vec3 worldPos = gpos.pos_;
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

inline std::vector<VoxelSetSnapshot>
gatherVisibleVoxelSets(IRMath::CardinalIndex cardinalIndex, IREntity::EntityId excludeEntity) {
    std::vector<VoxelSetSnapshot> snapshot;
    IREntity::forEachComponent<IRComponents::C_VoxelSetNew>([&](IREntity::EntityId id,
                                                                IRComponents::C_VoxelSetNew &vs) {
        if (id == excludeEntity)
            return;
        // Headless / pre-canvas sets have no pool span yet — they
        // can't be picked until a future canvas-attach pass moves
        // staged data into the pool.
        if (vs.numVoxels_ <= 0)
            return;
        if (vs.globalPositions_.empty() || vs.voxels_.empty())
            return;
        if (vs.size_.x <= 0 || vs.size_.y <= 0 || vs.size_.z <= 0)
            return;

        // worldOrigin = global position of local voxel (0,0,0).
        // Subsequent voxels at local (x,y,z) sit at integer offsets
        // from this origin in world space (axis-aligned; the engine
        // currently translates voxel sets but does not rotate
        // them). UPDATE_VOXEL_SET_CHILDREN has already refreshed
        // globalPositions_ for this frame.
        const IRMath::vec3 worldOrigin = vs.globalPositions_[0].pos_;
        // Voxels at local [0, size-1] occupy world [-0.5, size-0.5],
        // so the inflated AABB center is `worldOrigin + (size-1)/2`
        // and the half-extent along each axis is `size/2`. Cardinal
        // Z rotation permutes axes; the sum of half-extents
        // (projected onto (1,1,1)) is rotation-invariant, so the
        // iso-depth bound mirrors the shape-gather formulation.
        const IRMath::vec3 bboxCenter =
            worldOrigin + (IRMath::vec3(vs.size_) - IRMath::vec3(1.0f)) * 0.5f;
        const IRMath::vec3 bboxHalf = IRMath::vec3(vs.size_) * 0.5f;
        const IRMath::vec3 rotatedCenter = IRMath::rotateCardinalZ(bboxCenter, cardinalIndex);
        const float center = rotatedCenter.x + rotatedCenter.y + rotatedCenter.z;
        const float halfRange = bboxHalf.x + bboxHalf.y + bboxHalf.z;

        snapshot.push_back(
            {id,
             worldOrigin,
             vs.size_,
             std::span<const IRComponents::C_Voxel>(vs.voxels_.data(), vs.voxels_.size()),
             center - halfRange - kPickingDepthMargin,
             center + halfRange + kPickingDepthMargin}
        );
    });
    return snapshot;
}

// Picks the hit-face outward normal for a unit-cube voxel. Returns the
// axis with the largest |delta| component, signed by that component —
// "place adjacent" math just adds this normal to `voxelPos_` to land
// on the cell on the entry side.
inline IRMath::ivec3 voxelHitFaceNormal(IRMath::vec3 delta) {
    const IRMath::vec3 absDelta = IRMath::abs(delta);
    IRMath::ivec3 normal(0);
    if (absDelta.x >= absDelta.y && absDelta.x >= absDelta.z) {
        normal.x = delta.x >= 0.0f ? 1 : -1;
    } else if (absDelta.y >= absDelta.z) {
        normal.y = delta.y >= 0.0f ? 1 : -1;
    } else {
        normal.z = delta.z >= 0.0f ? 1 : -1;
    }
    return normal;
}

} // namespace detail

// Casts a ray from the cursor into the scene and returns the first
// shape or voxel it hits, or std::nullopt if the ray misses
// everything. The caller passes the editor's highlight entity (or any
// entity to be skipped) so the highlight itself doesn't catch its own
// ray.
//
// Algorithm: walk along the canvas-frame iso depth axis in
// `kPickingDepthStep` increments over the union of all visible shape
// and voxel-set iso-depth ranges. At each step, evaluate (a) every
// shape's SDF at the recovered world point and (b) for each visible
// `C_VoxelSetNew`, look up the candidate voxel via inverse-grid
// indexing — the first surface hit (`SDF::evaluate <=
// kSurfaceThreshold` for shapes; inside-unit-cube for voxels) wins.
// The depth-range pre-filter keeps the per-step cost proportional to
// the count of shapes / sets whose bounding box overlaps that depth
// slice, not the total scene count.
inline std::optional<RayHit>
castVoxelRay(IREntity::EntityId excludeEntity = IREntity::kNullEntity) {
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
    const IRMath::CardinalIndex cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);

    auto shapes = detail::gatherVisibleShapes(cardinalIndex, excludeEntity);
    auto voxelSets = detail::gatherVisibleVoxelSets(cardinalIndex, excludeEntity);
    if (shapes.empty() && voxelSets.empty())
        return std::nullopt;

    float depthMin = std::numeric_limits<float>::infinity();
    float depthMax = -std::numeric_limits<float>::infinity();
    for (const auto &s : shapes) {
        depthMin = IRMath::min(depthMin, s.isoDepthMin_);
        depthMax = IRMath::max(depthMax, s.isoDepthMax_);
    }
    for (const auto &vs : voxelSets) {
        depthMin = IRMath::min(depthMin, vs.isoDepthMin_);
        depthMax = IRMath::max(depthMax, vs.isoDepthMax_);
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
                return RayHit{
                    s.entity_,
                    worldPoint,
                    IRMath::roundVec3HalfUp(worldPoint),
                    IRMath::ivec3(0)
                };
            }
        }

        for (const auto &vs : voxelSets) {
            if (d < vs.isoDepthMin_ || d > vs.isoDepthMax_)
                continue;
            const IRMath::ivec3 localInt = IRMath::roundVec3HalfUp(worldPoint - vs.worldOrigin_);
            if (localInt.x < 0 || localInt.x >= vs.size_.x || localInt.y < 0 ||
                localInt.y >= vs.size_.y || localInt.z < 0 || localInt.z >= vs.size_.z) {
                continue;
            }
            const IRMath::vec3 candidateCenter = vs.worldOrigin_ + IRMath::vec3(localInt);
            const IRMath::vec3 delta = worldPoint - candidateCenter;
            const std::size_t flatIdx =
                static_cast<std::size_t>(IRMath::index3DtoIndex1D(localInt, vs.size_));
            // Active = non-zero alpha (matches `C_Voxel::activate` /
            // `deactivate` and the GPU pipeline's per-voxel skip).
            if (vs.voxels_[flatIdx].color_.alpha_ == 0)
                continue;
            return RayHit{
                vs.entity_,
                worldPoint,
                IRMath::roundVec3HalfUp(candidateCenter),
                detail::voxelHitFaceNormal(delta)
            };
        }
    }

    return std::nullopt;
}

} // namespace IRPrefab::Picking

#endif /* IR_PREFAB_PICKING_H */
