#ifndef IR_PREFAB_FOG_OF_WAR_H
#define IR_PREFAB_FOG_OF_WAR_H

// Driver-side API for the render pipeline's FOG_TO_TRIXEL pass. All
// operations apply to the active canvas's `C_CanvasFogOfWar` component
// and silently no-op when no canvas is active or the active canvas
// does not own one — this lets scripts and init code run before the
// canvas is fully wired without crashing.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstdint>

namespace IRPrefab::Fog {

/// CPU mirror of the shader's analytic reveal curve. Screen-space antialiasing
/// remains a pixel concern; gameplay uses the authored world-space edge.
inline float
evalVisionReveal(const IRComponents::FrameDataFogObservers &observers, IRMath::vec3 worldPosition) {
    float reveal = 0.0f;
    for (int i = 0; i < observers.visionCircleCount_; ++i) {
        const IRMath::vec4 circle = observers.visionCircles_[i];
        const IRMath::vec4 height = observers.visionCircleHeights_[i];
        const IRMath::vec2 delta = IRMath::vec2(worldPosition) - IRMath::vec2(circle);
        const float edge = IRMath::max(circle.w, 0.0f);
        const float keepRadius = circle.z + edge;
        if (IRMath::dot(delta, delta) > keepRadius * keepRadius) {
            continue;
        }

        const float dzUp = IRMath::max(height.x - worldPosition.z, 0.0f);
        const float dzDown = IRMath::max(worldPosition.z - height.x, 0.0f);
        const float distanceEffective = IRMath::length(delta) +
                                        height.y * IRMath::max(dzUp - height.w, 0.0f) +
                                        height.z * IRMath::max(dzDown - height.w, 0.0f);
        if (edge <= 0.0f) {
            reveal = IRMath::max(reveal, distanceEffective <= circle.z ? 1.0f : 0.0f);
            continue;
        }
        const float t =
            IRMath::clamp((distanceEffective - (circle.z - edge)) / (2.0f * edge), 0.0f, 1.0f);
        reveal = IRMath::max(reveal, 1.0f - t * t * (3.0f - 2.0f * t));
    }
    return reveal;
}

namespace detail {

inline IRComponents::C_CanvasFogOfWar *activeFogComponent() {
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    if (canvas == IREntity::kNullEntity)
        return nullptr;
    auto opt = IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(canvas);
    if (!opt.has_value())
        return nullptr;
    return *opt;
}

} // namespace detail

/// Set a single fog cell at world-space voxel column @p (worldX, worldY).
/// State values: 0 = unexplored, 128 = explored, 255 = visible.
/// Out-of-range writes are silently dropped.
inline void setCell(int worldX, int worldY, std::uint8_t state) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->setCell(worldX, worldY, state);
    }
}

/// Read the fog state at @p (worldX, worldY). Returns
/// `kFogStateUnexplored` if the active canvas has no fog component or
/// the coordinate is out of range.
inline std::uint8_t getCell(int worldX, int worldY) {
    if (auto *fog = detail::activeFogComponent()) {
        return fog->getCell(worldX, worldY);
    }
    return IRComponents::kFogStateUnexplored;
}

/// Mark every cell within @p radius (Euclidean distance, since #1994) of
/// @p (cx, cy) as visible. See `C_CanvasFogOfWar::revealRadius` for the v1
/// contract around the cells that are NOT downgraded.
inline void revealRadius(int cx, int cy, int radius) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->revealRadius(cx, cy, radius);
    }
}

/// Replace the live vision set with a single analytic disc centered at the
/// (fractional) world column @p (cx, cy), @p radius world units. This is the
/// SMOOTH, render-resolution reveal: evaluated per pixel in the fog shader, so
/// the edge is crisp at game resolution, tracks sub-voxel observer motion
/// without grid quantization, and reveals partial voxels at the boundary —
/// distinct from the voxel-grid `revealRadius`. @p edge is the edge softness
/// in world units (default reads as antialiasing). @p observerZ + @p zCostUp
/// + @p zCostDown + @p freeBand (#2260, generalized by #2557) add an
/// asymmetric, penalty-free-banded height penalty — see
/// `C_CanvasFogOfWar::addVisionCircle` for the exact effective-distance
/// formula. @p zCostDown < 0 (the default) mirrors @p zCostUp; all-defaults
/// (@p zCostUp 0, @p freeBand 0) is the plain 2D disc. For multiple sources,
/// call `clearVisionCircles` then `addVisionCircle` per source. Combines
/// (with the grid and other circles) via max.
inline void setVisionCircle(
    float cx,
    float cy,
    float radius,
    float edge = IRComponents::kFogVisionEdgeDefault,
    float observerZ = 0.0f,
    float zCostUp = 0.0f,
    float zCostDown = IRComponents::kFogVisionZCostMirrorUp,
    float freeBand = 0.0f
) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearVisionCircles();
        fog->addVisionCircle(cx, cy, radius, edge, observerZ, zCostUp, zCostDown, freeBand);
    }
}

/// Append one analytic vision disc to the live set (up to
/// `kMaxFogVisionCircles`). See `setVisionCircle` for disc semantics (including
/// the @p observerZ / @p zCostUp / @p zCostDown / @p freeBand height penalty,
/// #2260/#2557); use this after `clearVisionCircles` to drive several vision
/// sources in one frame.
inline void addVisionCircle(
    float cx,
    float cy,
    float radius,
    float edge = IRComponents::kFogVisionEdgeDefault,
    float observerZ = 0.0f,
    float zCostUp = 0.0f,
    float zCostDown = IRComponents::kFogVisionZCostMirrorUp,
    float freeBand = 0.0f
) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->addVisionCircle(cx, cy, radius, edge, observerZ, zCostUp, zCostDown, freeBand);
    }
}

/// Drop every live analytic vision disc → grid-only fog.
inline void clearVisionCircles() {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearVisionCircles();
    }
}

/// Reset every cell to `kFogStateUnexplored`.
inline void clear() {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearAll();
    }
}

/// Attach both components FOG_TO_TRIXEL's archetype requires to @p canvas:
/// C_TrixelCanvasRenderBehavior (added only if absent, preserving any prior
/// customized behavior component) and a fresh C_CanvasFogOfWar. FOG_TO_TRIXEL
/// silently no-ops on a canvas missing either, so co-attaching here removes
/// that footgun from call sites. @p revealRadius > 0 also reveals an
/// origin-centered disc of that radius on @p canvas (pass kFogOfWarSize for a
/// full reveal); 0 (default) attaches only, leaving the grid unexplored.
inline void attachToCanvas(IREntity::EntityId canvas, int revealRadius = 0) {
    if (!IREntity::getComponentOptional<IRComponents::C_TrixelCanvasRenderBehavior>(canvas)
             .has_value())
        IREntity::setComponent(canvas, IRComponents::C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(canvas, IRComponents::C_CanvasFogOfWar{});
    if (revealRadius > 0) {
        if (auto opt = IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(canvas))
            (*opt)->revealRadius(0, 0, revealRadius);
    }
}

/// Opt a grid-canvas voxel entity into whole-body fog reveal. A missing
/// C_VoxelSetNew is a no-op. Tagging starts hidden so an entity outside every
/// circle cannot flash before its first eval.
inline void setEntityRevealGoverned(IREntity::EntityId entity, bool governed = true) {
    auto setOpt = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity);
    if (!setOpt.has_value()) {
        return;
    }
    IRComponents::C_VoxelSetNew *voxelSet = *setOpt;
    const IREntity::EntityId activeCanvas = IRRender::getActiveCanvasEntityOrNull();
    const IREntity::EntityId canvas =
        voxelSet->canvasEntity_ == IREntity::kNullEntity ? activeCanvas : voxelSet->canvasEntity_;
    IR_ASSERT(
        canvas == activeCanvas,
        "whole-body fog reveal currently supports only the active grid canvas"
    );

    for (IRComponents::C_Voxel &voxel : voxelSet->voxels_) {
        if (governed) {
            voxel.reserved_ |= IRComponents::VoxelReserved::kFogWholeBodyExempt;
        } else {
            voxel.reserved_ &= ~IRComponents::VoxelReserved::kFogWholeBodyExempt;
        }
    }
    for (IRComponents::C_Voxel &voxel : voxelSet->rotationSourceVoxels_) {
        if (governed) {
            voxel.reserved_ |= IRComponents::VoxelReserved::kFogWholeBodyExempt;
        } else {
            voxel.reserved_ &= ~IRComponents::VoxelReserved::kFogWholeBodyExempt;
        }
    }

    // Structural component changes migrate the entity's archetype, so keep
    // them last: every preceding access through voxelSet must finish first.
    if (governed) {
        voxelSet->visible_ = false;
        IRPrefab::VoxelPool::markRangeInactive(
            voxelSet->voxelStartIdx_,
            voxelSet->numVoxels_,
            canvas
        );
        IREntity::setComponent(entity, IRComponents::C_FogRevealed{});
        return;
    }

    voxelSet->visible_ = true;
    IRPrefab::VoxelPool::resyncRangeFromColors(
        voxelSet->voxelStartIdx_,
        voxelSet->numVoxels_,
        canvas
    );
    IREntity::removeComponent<IRComponents::C_FogRevealed>(entity);
}

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_OF_WAR_H */
