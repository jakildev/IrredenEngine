#ifndef IR_PREFAB_PICKING_H
#define IR_PREFAB_PICKING_H

// Editor-side voxel picking: convert the cursor into a world-space ray
// (in the engine's isometric projection) and find the first SDF shape
// or `C_VoxelSetNew` voxel it intersects. The inverse is raster-yaw only,
// so the recovered world point matches what the voxel rasterizer wrote at
// any cardinal yaw.
//
// Two cursor rays, chosen per call (`RayCastOptions::cursorRay_`):
//
//   - **ISO_LATTICE** (default) — the cursor snaps to its integer iso
//     pixel (`IRRender::mouseWorldPos3DAtIsoDepth`) and the ray is sampled
//     in `kPickingDepthStep` increments. Resolves the iso column; a
//     voxel's -x and -y faces share a column at cardinal yaw, so it cannot
//     tell them apart.
//   - **SCREEN_PIXEL** — the ray passes through the cursor's exact
//     position (`IRRender::mouseWorldPos3DAtIsoDepthExact`) and voxel sets
//     are traversed cell by cell (`castGridRay`), so the hit and its face
//     are the ones drawn under the cursor. Use it when the face matters.
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
//     `VOXEL_TO_TRIXEL_STAGE_1` and `SHAPES_TO_TRIXEL` (via a
//     persistent-mapped buffer) and returns the frontmost entity at the
//     cursor pixel in O(1). The trade-off is a one-frame lag (the buffer
//     holds the previous frame's render) and it yields the entity only
//     — no sub-voxel surface hit position, no face normal. Use when the
//     scene has many active voxels and per-click CPU cost dominates, or
//     when only the entity id matters (selection without placement).

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace IRPrefab::Picking {

// Picks the hit-face outward normal for a unit-cube voxel. Returns the
// axis with the largest |delta| component, signed by that component —
// "place adjacent" math just adds this normal to `voxelPos_` to land
// on the cell on the entry side.
//
// The tie-break order is contractual, not incidental: equal magnitudes
// resolve x → y → z, so a hit whose entry offset is parallel to the
// (1,1,1) march axis (the ray passing exactly through the voxel centre)
// always reports the x face. `test/render/picking_face_normal_test.cpp`
// pins it. Only the ISO_LATTICE ray derives its normal this way; it is
// wrong for a sample taken off a lattice ray, where the dominant axis of
// the offset need not be the face the ray entered through.
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

struct RayHit {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    IRMath::vec3 worldHitPos_ = IRMath::vec3(0.0f);
    IRMath::ivec3 voxelPos_ = IRMath::ivec3(0);
    // Outward unit normal of the hit face (±1 on exactly one axis). Only
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

enum class CursorRay {
    ISO_LATTICE,
    SCREEN_PIXEL,
};

struct RayCastOptions {
    // Skipped outright — the caller's own highlight or indicator.
    IREntity::EntityId excludeEntity_ = IREntity::kNullEntity;
    CursorRay cursorRay_ = CursorRay::ISO_LATTICE;
    // SDF shapes take part in the cast. A shape hit carries no face normal,
    // so a caller that needs one turns shapes off rather than have a shape
    // in front of a voxel swallow the hit.
    bool shapes_ = true;
    // When set, only entities it returns true for take part; everything
    // else is passed through as if absent, so the nearest accepted surface
    // wins even when a rejected one is drawn in front of it.
    bool (*accepts_)(IREntity::EntityId) = nullptr;
};

struct GridRayHit {
    IRMath::ivec3 cell_ = IRMath::ivec3(0);
    // Outward normal of the face the ray entered `cell_` through.
    IRMath::ivec3 faceNormal_ = IRMath::ivec3(0);
    // Ray parameter of that entry point: `origin + vec3(direction) * rayT_`.
    float rayT_ = 0.0f;
};

// First occupied cell an infinite line meets in a grid of unit cells, and the
// face it enters through. Cell `c` spans `[c - 0.5, c + 0.5)` on each axis, for
// `c` in `[0, size)`; `origin` is any point on the line in that frame and
// `direction` its per-axis step, each component exactly +1 or -1 (every iso
// view ray, at any cardinal yaw). The walk is exact: it visits every cell the
// line crosses in order, so no cell is skipped however short the chord through
// it. Crossings that tie resolve x, then y, then z.
template <typename Occupied>
std::optional<GridRayHit>
castGridRay(IRMath::vec3 origin, IRMath::ivec3 direction, IRMath::ivec3 size, Occupied &&occupied) {
    IR_ASSERT(
        IRMath::abs(direction.x) == 1 && IRMath::abs(direction.y) == 1 &&
            IRMath::abs(direction.z) == 1,
        "castGridRay direction components must each be exactly +1 or -1"
    );
    if (size.x <= 0 || size.y <= 0 || size.z <= 0)
        return std::nullopt;

    // Shift so cell c spans [c, c + 1).
    const IRMath::vec3 start = origin + IRMath::vec3(0.5f);
    float tEnter = -std::numeric_limits<float>::infinity();
    float tExit = std::numeric_limits<float>::infinity();
    int axis = 0;
    for (int a = 0; a < 3; ++a) {
        const float extent = static_cast<float>(size[a]);
        const float tNear = direction[a] > 0 ? -start[a] : start[a] - extent;
        const float tFar = tNear + extent;
        if (tNear > tEnter) {
            tEnter = tNear;
            axis = a;
        }
        tExit = IRMath::min(tExit, tFar);
    }
    if (tEnter >= tExit)
        return std::nullopt;

    IRMath::ivec3 cell(0);
    IRMath::vec3 tNext(0.0f);
    for (int a = 0; a < 3; ++a) {
        const float at = start[a] + static_cast<float>(direction[a]) * tEnter;
        cell[a] = IRMath::clamp(static_cast<int>(IRMath::floor(at)), 0, size[a] - 1);
        if (a == axis)
            cell[a] = direction[a] > 0 ? 0 : size[a] - 1;
        tNext[a] = direction[a] > 0 ? static_cast<float>(cell[a] + 1) - start[a]
                                    : start[a] - static_cast<float>(cell[a]);
    }

    float t = tEnter;
    while (true) {
        if (occupied(cell)) {
            IRMath::ivec3 normal(0);
            normal[axis] = -direction[axis];
            return GridRayHit{cell, normal, t};
        }
        axis = 0;
        if (tNext.y < tNext[axis])
            axis = 1;
        if (tNext.z < tNext[axis])
            axis = 2;
        t = tNext[axis];
        cell[axis] += direction[axis];
        if (cell[axis] < 0 || cell[axis] >= size[axis])
            return std::nullopt;
        tNext[axis] += 1.0f;
    }
}

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

inline bool takesPart(IREntity::EntityId id, const RayCastOptions &options) {
    if (id == options.excludeEntity_)
        return false;
    return options.accepts_ == nullptr || options.accepts_(id);
}

inline std::vector<ShapeSnapshot>
gatherVisibleShapes(IRMath::CardinalIndex cardinalIndex, const RayCastOptions &options) {
    std::vector<ShapeSnapshot> snapshot;
    if (!options.shapes_)
        return snapshot;
    IREntity::forEachComponent<IRComponents::C_ShapeDescriptor>(
        [&](IREntity::EntityId id, IRComponents::C_ShapeDescriptor &sd) {
            if (!takesPart(id, options))
                return;
            if (!(sd.flags_ & IRRender::SHAPE_FLAG_VISIBLE))
                return;

            // forEachComponent iterates one component type; position is
            // fetched per-entity because the API has no multi-component
            // form. Safe: createEntity guarantees C_WorldTransform on
            // every entity. Acceptable: this path fires on click frames
            // only.
            auto &xform = IREntity::getComponent<IRComponents::C_WorldTransform>(id);
            const IRMath::vec3 worldPos = xform.translation_;
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
gatherVisibleVoxelSets(IRMath::CardinalIndex cardinalIndex, const RayCastOptions &options) {
    std::vector<VoxelSetSnapshot> snapshot;
    IREntity::forEachComponent<IRComponents::C_VoxelSetNew>([&](IREntity::EntityId id,
                                                                IRComponents::C_VoxelSetNew &vs) {
        if (!takesPart(id, options))
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

// Active = non-zero alpha (matches `C_Voxel::activate` / `deactivate` and
// the GPU pipeline's per-voxel skip).
inline bool voxelActive(const VoxelSetSnapshot &vs, IRMath::ivec3 local) {
    const std::size_t flatIdx = static_cast<std::size_t>(IRMath::index3DtoIndex1D(local, vs.size_));
    return vs.voxels_[flatIdx].color_.alpha_ != 0;
}

inline bool shapeSurfaceAt(const ShapeSnapshot &s, IRMath::vec3 worldPoint) {
    const IRMath::vec3 localPos = worldPoint - s.worldPos_;
    // Cheap bounding-sphere reject before the full SDF eval.
    if (IRMath::dot(localPos, localPos) > s.boundingRadius_ * s.boundingRadius_)
        return false;
    const IRMath::vec4 effective = IRMath::SDF::effectiveParams(s.type_, s.params_);
    return IRMath::SDF::evaluate(localPos, s.type_, effective) <= IRMath::SDF::kSurfaceThreshold;
}

inline RayHit shapeHit(const ShapeSnapshot &s, IRMath::vec3 worldPoint) {
    return RayHit{s.entity_, worldPoint, IRMath::roundVec3HalfUp(worldPoint), IRMath::ivec3(0)};
}

// Walk along the canvas-frame iso depth axis in `kPickingDepthStep`
// increments over the union of all shape and voxel-set iso-depth ranges. At
// each step, evaluate every shape's SDF at the recovered world point, then
// look up each voxel set's candidate voxel by inverse-grid indexing; the
// first surface hit wins. The depth-range pre-filter keeps the per-step cost
// proportional to the count of shapes / sets whose bounding box overlaps that
// depth slice, not the total scene count.
inline std::optional<RayHit> castIsoLatticeRay(
    const std::vector<ShapeSnapshot> &shapes, const std::vector<VoxelSetSnapshot> &voxelSets
) {
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
            if (shapeSurfaceAt(s, worldPoint))
                return shapeHit(s, worldPoint);
        }

        for (const auto &vs : voxelSets) {
            if (d < vs.isoDepthMin_ || d > vs.isoDepthMax_)
                continue;
            const IRMath::ivec3 localInt = IRMath::roundVec3HalfUp(worldPoint - vs.worldOrigin_);
            if (localInt.x < 0 || localInt.x >= vs.size_.x || localInt.y < 0 ||
                localInt.y >= vs.size_.y || localInt.z < 0 || localInt.z >= vs.size_.z) {
                continue;
            }
            if (!voxelActive(vs, localInt))
                continue;
            const IRMath::vec3 candidateCenter = vs.worldOrigin_ + IRMath::vec3(localInt);
            return RayHit{
                vs.entity_,
                worldPoint,
                IRMath::roundVec3HalfUp(candidateCenter),
                voxelHitFaceNormal(worldPoint - candidateCenter)
            };
        }
    }

    return std::nullopt;
}

// The ray through the cursor's exact position. Voxel sets are traversed cell
// by cell, so the nearest hit and its entry face are exact; shapes have no
// closed-form entry, so they are sampled along the same ray and win only when
// a sample lands on one in front of the nearest voxel.
inline std::optional<RayHit> castScreenPixelRay(
    const std::vector<ShapeSnapshot> &shapes,
    const std::vector<VoxelSetSnapshot> &voxelSets,
    IRMath::CardinalIndex cardinalIndex
) {
    // The canvas-frame depth axis is (1,1,1); lifted to the world frame it
    // keeps unit magnitude on every axis, and canvas depth advances 3 per
    // unit of ray parameter.
    const IRMath::vec3 rayOrigin = IRRender::mouseWorldPos3DAtIsoDepthExact(0.0f);
    const IRMath::ivec3 rayDirection =
        IRMath::roundVec3HalfUp(IRMath::rotateCardinalZInv(IRMath::vec3(1.0f), cardinalIndex));
    const IRMath::vec3 rayStep(rayDirection);

    std::optional<RayHit> nearest;
    float nearestDepth = std::numeric_limits<float>::infinity();
    for (const auto &vs : voxelSets) {
        const std::optional<GridRayHit> hit = castGridRay(
            rayOrigin - vs.worldOrigin_,
            rayDirection,
            vs.size_,
            [&vs](IRMath::ivec3 local) { return voxelActive(vs, local); }
        );
        if (!hit || hit->rayT_ * 3.0f >= nearestDepth)
            continue;
        nearestDepth = hit->rayT_ * 3.0f;
        nearest = RayHit{
            vs.entity_,
            rayOrigin + rayStep * hit->rayT_,
            IRMath::roundVec3HalfUp(vs.worldOrigin_ + IRMath::vec3(hit->cell_)),
            hit->faceNormal_
        };
    }

    float depthMin = std::numeric_limits<float>::infinity();
    float depthMax = -std::numeric_limits<float>::infinity();
    for (const auto &s : shapes) {
        depthMin = IRMath::min(depthMin, s.isoDepthMin_);
        depthMax = IRMath::max(depthMax, s.isoDepthMax_);
    }
    depthMax = IRMath::min(depthMax, nearestDepth);
    for (float d = depthMin; d <= depthMax; d += kPickingDepthStep) {
        const IRMath::vec3 worldPoint = rayOrigin + rayStep * (d / 3.0f);
        for (const auto &s : shapes) {
            if (d < s.isoDepthMin_ || d > s.isoDepthMax_)
                continue;
            if (shapeSurfaceAt(s, worldPoint))
                return shapeHit(s, worldPoint);
        }
    }

    return nearest;
}

} // namespace detail

// Casts a ray from the cursor into the scene and returns the first shape or
// voxel it hits, or std::nullopt if the ray misses everything. `options`
// picks the cursor ray and which entities take part.
inline std::optional<RayHit> castVoxelRay(const RayCastOptions &options) {
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
    const IRMath::CardinalIndex cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);

    const auto shapes = detail::gatherVisibleShapes(cardinalIndex, options);
    const auto voxelSets = detail::gatherVisibleVoxelSets(cardinalIndex, options);
    if (shapes.empty() && voxelSets.empty())
        return std::nullopt;

    if (options.cursorRay_ == CursorRay::SCREEN_PIXEL)
        return detail::castScreenPixelRay(shapes, voxelSets, cardinalIndex);
    return detail::castIsoLatticeRay(shapes, voxelSets);
}

// The ISO_LATTICE cast over every visible shape and voxel set. The caller
// passes the editor's highlight entity (or any entity to be skipped) so the
// highlight itself doesn't catch its own ray.
inline std::optional<RayHit>
castVoxelRay(IREntity::EntityId excludeEntity = IREntity::kNullEntity) {
    return castVoxelRay(RayCastOptions{.excludeEntity_ = excludeEntity});
}

} // namespace IRPrefab::Picking

#endif /* IR_PREFAB_PICKING_H */
