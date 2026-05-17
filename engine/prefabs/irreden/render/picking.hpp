#ifndef IR_PREFAB_PICKING_H
#define IR_PREFAB_PICKING_H

// Editor-side voxel picking: convert the cursor into a world-space ray
// (in the engine's isometric projection) and find the first surface it
// intersects. The picking inverse is the same one used by
// `IRRender::mouseWorldPos3DAtIsoDepth`, so the recovered world point
// matches what the voxel rasterizer wrote at any visualYaw.
//
// Two paths exist for "which entity is under the cursor":
//
//   1. CPU-side `castVoxelRay` (this header) — **default for click-frame
//      picking.** Walks the cursor's iso-depth axis, SDF-tests every
//      `C_ShapeDescriptor` plus every active voxel of every `C_VoxelSetNew`
//      whose bounding box overlaps the current depth slice. Returns the
//      first surface intersection with sub-voxel world position, the
//      integer voxel coordinate, the owning entity, and (for voxel-set
//      hits) the face normal needed for place-adjacent computation.
//      Cost is `O(visibleSurfaces × depthSteps)` per click and fires
//      only on click frames — never per-frame.
//
//   2. GPU `IRRender::getEntityIdAtMouseTrixel()` — **documented fallback.**
//      Reads the entity-id texture populated by `VOXEL_TO_TRIXEL_STAGE_2`
//      and `SHAPES_TO_TRIXEL` (via a persistent-mapped buffer) and returns
//      the frontmost entity id at the cursor pixel in `O(1)`. Trade-offs:
//      one-frame lag (the buffer holds the previous frame's render), and
//      it yields the entity id only — no sub-voxel hit position and no
//      face normal. Reach for this when the CPU path's per-click cost
//      becomes a bottleneck (e.g., scenes with hundreds of thousands of
//      active voxels) and the lag is acceptable.
//
// The CPU path is preferred because click responsiveness matters in
// editor workflows; the GPU path stays available for perf-critical
// creations to swap in without rewriting consumer logic.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
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
    // Axis-aligned unit vector pointing out of the face the ray entered.
    // Populated only for `C_VoxelSetNew` hits (voxels are unit cubes with
    // an unambiguous face). Stays at the default zero for `C_ShapeDescriptor`
    // hits, where smooth SDFs make a per-face normal ill-defined.
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
            // only. APPLY_POSITION_OFFSET has already folded any modifier-
            // driven offset into globalPos for this frame.
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

struct VoxelSetSnapshot {
    IREntity::EntityId entity_;
    // World position of grid coord (0,0,0)'s voxel center for this set.
    // Equals `entityGlobalPos + positions_[0].pos_` — the same compose
    // `updateAsChild` uses to drive the per-voxel global pool span,
    // minus the per-voxel `positionOffsets_` (squash/stretch animation
    // offset). Picking treats the rest pose; the editor's place/erase
    // operates on integer grid coords, not the deformed pose.
    IRMath::vec3 originWorld_;
    IRMath::ivec3 size_;
    float isoDepthMin_;
    float isoDepthMax_;
    std::span<const IRComponents::C_Voxel> voxels_;
};

inline std::vector<VoxelSetSnapshot>
gatherVisibleVoxelSets(IRMath::CardinalIndex cardinalIndex, IREntity::EntityId excludeEntity) {
    std::vector<VoxelSetSnapshot> snapshot;
    IREntity::forEachComponent<IRComponents::C_VoxelSetNew>([&](IREntity::EntityId id,
                                                                IRComponents::C_VoxelSetNew &vs) {
        if (id == excludeEntity)
            return;
        // Skip headless-staged sets — they have no pool span and no
        // rendered footprint, so they can't be picked. A future
        // canvas-attach pass moves staged voxels into the pool; once
        // that lands the set will pass this guard naturally.
        if (vs.numVoxels_ <= 0 || vs.voxels_.empty() || vs.positions_.empty())
            return;
        if (vs.size_.x <= 0 || vs.size_.y <= 0 || vs.size_.z <= 0)
            return;

        // Same per-entity getComponent rationale as gatherVisibleShapes:
        // click-frame path, not a per-frame tick.
        auto &gpos = IREntity::getComponent<IRComponents::C_PositionGlobal3D>(id);
        const IRMath::vec3 originWorld = gpos.pos_ + vs.positions_[0].pos_;

        // Grid AABB half-extents are `size * 0.5` (voxels are unit
        // cubes centered on integer coords; the grid spans
        // `[origin - 0.5, origin + (size - 1) + 0.5]`). The iso-depth
        // sum `bx + by + bz` is invariant under `rotateCardinalZ`
        // because cardinal rotations permute axes but preserve their
        // magnitudes — same trick `gatherVisibleShapes` uses.
        const IRMath::vec3 gridCenterWorld =
            originWorld + IRMath::vec3(vs.size_ - IRMath::ivec3(1)) * 0.5f;
        const IRMath::vec3 rotatedCenter = IRMath::rotateCardinalZ(gridCenterWorld, cardinalIndex);
        const float center = rotatedCenter.x + rotatedCenter.y + rotatedCenter.z;
        const IRMath::vec3 boundHalf = IRMath::vec3(vs.size_) * 0.5f;
        const float halfRange = boundHalf.x + boundHalf.y + boundHalf.z;

        snapshot.push_back(
            {id,
             originWorld,
             vs.size_,
             center - halfRange - kPickingDepthMargin,
             center + halfRange + kPickingDepthMargin,
             std::span<const IRComponents::C_Voxel>(vs.voxels_)}
        );
    });
    return snapshot;
}

// Face normal for a voxel hit: ±1 along the dominant axis of the
// `worldHitPos − voxelCenter` vector. Voxels are unit cubes centered on
// integer coords, so `(worldHitPos − voxelCenter)` lies inside
// `[-0.5, 0.5]³`; the component of largest magnitude tells us which face
// the ray crossed to enter. Ties (corner hits) fall through to the next
// axis in x→y→z order — corner hits are rare in practice, and any of
// the three faces is a valid place-adjacent direction.
inline IRMath::ivec3
voxelFaceNormal(const IRMath::vec3 &worldHitPos, const IRMath::ivec3 &voxelWorldCoord) {
    const IRMath::vec3 delta = worldHitPos - IRMath::vec3(voxelWorldCoord);
    const float ax = IRMath::abs(delta.x);
    const float ay = IRMath::abs(delta.y);
    const float az = IRMath::abs(delta.z);
    IRMath::ivec3 normal(0);
    if (ax >= ay && ax >= az) {
        normal.x = (delta.x >= 0.0f) ? 1 : -1;
    } else if (ay >= az) {
        normal.y = (delta.y >= 0.0f) ? 1 : -1;
    } else {
        normal.z = (delta.z >= 0.0f) ? 1 : -1;
    }
    return normal;
}

} // namespace detail

// Casts a ray from the cursor into the scene and returns the first
// shape or voxel it hits, or std::nullopt if the ray misses everything.
// The caller passes the editor's highlight entity (or any entity to be
// skipped) so the highlight itself doesn't catch its own ray.
//
// Algorithm: walk along the canvas-frame iso depth axis in
// `kPickingDepthStep` increments over the union of all visible
// surfaces' iso-depth ranges (`C_ShapeDescriptor` + `C_VoxelSetNew`).
// At each step:
//   - Evaluate every shape's SDF at the recovered world point; the
//     first surface (`SDF::evaluate <= kSurfaceThreshold`) wins.
//   - Check every voxel set whose AABB overlaps the depth slice: round
//     the world point to an integer voxel coord, bounds-check against
//     the set's grid extent, and consume the hit if the indexed voxel
//     is active (`color_.alpha_ > 0`). Returns the per-set entity ID
//     plus the face normal derived from the entry direction so the
//     caller can compute the place-adjacent target world coord.
// The depth-range pre-filter keeps the per-step cost proportional to
// the number of surfaces whose bounding box overlaps that depth slice,
// not the total scene surface count. Shapes and voxel sets are tested
// in the same depth-step pass so the lowest-iso-depth hit wins
// regardless of which surface kind produced it.
inline std::optional<RayHit>
castVoxelRay(IREntity::EntityId excludeEntity = IREntity::kNullEntity) {
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
    const IRMath::CardinalIndex cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);

    auto shapes = detail::gatherVisibleShapes(cardinalIndex, excludeEntity);
    auto voxelSets = detail::gatherVisibleVoxelSets(cardinalIndex, excludeEntity);
    if (shapes.empty() && voxelSets.empty())
        return std::nullopt;

    // Sentinel-driven union of every visible surface's iso-depth range.
    // The empty-check above guarantees at least one source loop runs, so
    // the infinities are always overwritten before the walk begins.
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
                return RayHit{s.entity_, worldPoint, IRMath::roundVec3HalfUp(worldPoint)};
            }
        }
        for (const auto &vs : voxelSets) {
            if (d < vs.isoDepthMin_ || d > vs.isoDepthMax_)
                continue;
            const IRMath::ivec3 gridCoord = IRMath::roundVec3HalfUp(worldPoint - vs.originWorld_);
            if (gridCoord.x < 0 || gridCoord.x >= vs.size_.x || gridCoord.y < 0 ||
                gridCoord.y >= vs.size_.y || gridCoord.z < 0 || gridCoord.z >= vs.size_.z) {
                continue;
            }
            const int flatIdx = IRMath::index3DtoIndex1D(gridCoord, vs.size_);
            if (vs.voxels_[flatIdx].color_.alpha_ == 0)
                continue;
            const IRMath::ivec3 voxelWorldCoord = IRMath::roundVec3HalfUp(worldPoint);
            return RayHit{
                vs.entity_,
                worldPoint,
                voxelWorldCoord,
                detail::voxelFaceNormal(worldPoint, voxelWorldCoord)
            };
        }
    }

    return std::nullopt;
}

} // namespace IRPrefab::Picking

#endif /* IR_PREFAB_PICKING_H */
