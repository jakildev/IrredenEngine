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
#include <irreden/render/fog_line_of_sight.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstdint>

namespace IRPrefab::Fog {

namespace detail {

/// Source @p source's reveal of @p worldPosition, ignoring line of sight.
inline float evalVisionCircleReveal(
    const IRComponents::FrameDataFogObservers &observers, int source, IRMath::vec3 worldPosition
) {
    const IRMath::vec4 circle = observers.visionCircles_[source];
    const IRMath::vec4 height = observers.visionCircleHeights_[source];
    const IRMath::vec2 delta = IRMath::vec2(worldPosition) - IRMath::vec2(circle);
    const float edge = IRMath::max(circle.w, 0.0f);
    const float keepRadius = circle.z + edge;
    if (IRMath::dot(delta, delta) > keepRadius * keepRadius) {
        return 0.0f;
    }

    const float dzUp = IRMath::max(height.x - worldPosition.z, 0.0f);
    const float dzDown = IRMath::max(worldPosition.z - height.x, 0.0f);
    const float distanceEffective = IRMath::length(delta) +
                                    height.y * IRMath::max(dzUp - height.w, 0.0f) +
                                    height.z * IRMath::max(dzDown - height.w, 0.0f);
    if (edge <= 0.0f) {
        return distanceEffective <= circle.z ? 1.0f : 0.0f;
    }
    const float t =
        IRMath::clamp((distanceEffective - (circle.z - edge)) / (2.0f * edge), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

} // namespace detail

/// CPU mirror of the shader's analytic reveal curve, cost terms only: this
/// overload reads no line-of-sight field and ignores `losSourceMask_`. Screen-
/// space antialiasing remains a pixel concern; gameplay uses the authored
/// world-space edge.
inline float
evalVisionReveal(const IRComponents::FrameDataFogObservers &observers, IRMath::vec3 worldPosition) {
    float reveal = 0.0f;
    for (int i = 0; i < observers.visionCircleCount_; ++i) {
        reveal = IRMath::max(reveal, detail::evalVisionCircleReveal(observers, i, worldPosition));
    }
    return reveal;
}

/// The authoritative reveal: the cost curve above, with each source gated in
/// @p observers' `losSourceMask_` contributing only where @p los sees
/// @p worldPosition's rounded voxel. @p observers and @p los must come from one
/// publication (`C_CanvasFogOfWar::losPublishedObservers_` + `losField()`); an
/// unpublished field reveals nothing through a gated source.
inline float evalVisionReveal(
    const IRComponents::FrameDataFogObservers &observers,
    const IRComponents::FogLineOfSightField &los,
    IRMath::vec3 worldPosition
) {
    const IRMath::ivec3 sample = IRMath::roundVec3HalfUp(worldPosition);
    float reveal = 0.0f;
    for (int i = 0; i < observers.visionCircleCount_; ++i) {
        if (((observers.losSourceMask_ >> i) & 1) != 0 && !los.visible(i, sample)) {
            continue;
        }
        reveal = IRMath::max(reveal, detail::evalVisionCircleReveal(observers, i, worldPosition));
    }
    return reveal;
}

namespace detail {

inline IRComponents::C_CanvasFogOfWar *activeFogComponent() {
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntityOrNull();
    if (canvas == IREntity::kNullEntity)
        return nullptr;
    auto opt = IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(canvas);
    if (!opt.has_value())
        return nullptr;
    return *opt;
}

} // namespace detail

/// Evaluate the active canvas's analytic vision sources at @p worldPosition.
/// An absent fog attachment leaves gameplay unrestricted; an attached fog
/// component with no sources reveals nothing.
inline float evalActiveVisionReveal(IRMath::vec3 worldPosition) {
    if (auto *fog = detail::activeFogComponent()) {
        return evalVisionReveal(fog->observers_, worldPosition);
    }
    return 1.0f;
}

/// Read the last reveal verdict stored for @p entity. Entities without the
/// governance component are unrestricted.
inline float getEntityReveal(IREntity::EntityId entity) {
    auto revealed = IREntity::getComponentOptional<IRComponents::C_FogRevealed>(entity);
    return revealed.has_value() ? (*revealed)->revealFactor_ : 1.0f;
}

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

/// Mark every cell within @p radius (Euclidean distance) of
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
/// + @p zCostDown + @p freeBand add an
/// asymmetric, penalty-free-banded height penalty — see
/// `C_CanvasFogOfWar::addVisionCircle` for the exact effective-distance
/// formula. @p zCostDown < 0 (the default) mirrors @p zCostUp; all-defaults
/// (@p zCostUp 0, @p freeBand 0) is the plain 2D disc. For multiple sources,
/// call `clearVisionCircles` then `addVisionCircle` per source. Combines
/// (with the grid and other circles) via max. Returns the circle's slot (0) or
/// -1 when it was rejected; the slot starts with line of sight off.
inline int setVisionCircle(
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
        return fog->addVisionCircle(cx, cy, radius, edge, observerZ, zCostUp, zCostDown, freeBand);
    }
    return -1;
}

/// Append one analytic vision disc to the live set (up to
/// `kMaxFogVisionCircles`). See `setVisionCircle` for disc semantics (including
/// the @p observerZ / @p zCostUp / @p zCostDown / @p freeBand height penalty);
/// use this after `clearVisionCircles` to drive several vision
/// sources in one frame. Returns the assigned slot — the index
/// `setVisionCircleLineOfSight` takes — or -1 when the circle was dropped.
inline int addVisionCircle(
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
        return fog->addVisionCircle(cx, cy, radius, edge, observerZ, zCostUp, zCostDown, freeBand);
    }
    return -1;
}

/// Gate vision circle @p source by line of sight, the eye @p losEyeHeight world
/// units above its `observerZ`; `kFogVisionLosOff` (any negative height)
/// ungates it. See `C_CanvasFogOfWar::setVisionCircleLineOfSight` for the slot
/// contract and the occluder model; the RENDER pipeline must carry
/// `FOG_LOS_BUILD`. The per-frame clear-then-add pattern re-enables it every
/// frame.
inline void setVisionCircleLineOfSight(int source, float losEyeHeight) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->setVisionCircleLineOfSight(source, losEyeHeight);
    }
}

/// Whether @p to is visible from the eye @p from under the line-of-sight model
/// (the rounded voxel of @p to against the horizon of its column; see
/// `component_canvas_fog_of_war.hpp`), over the active canvas's current
/// occluders. True without an active fog canvas, when @p to shares @p from's
/// column, and when @p to's column is outside the fog footprint.
///
/// Cost: rebuilds a 256 KiB column view from every live pool voxel and flagged
/// shape on each call, then one supercover walk — an occasional gameplay query,
/// not a per-unit per-frame one. Needs no registered vision circle, and agrees
/// with the built field at integer heights on the same occluders.
inline bool lineOfSight(IRMath::vec3 from, IRMath::vec3 to) {
    auto *fog = detail::activeFogComponent();
    if (fog == nullptr) {
        return true;
    }
    const IRMath::ivec3 target = IRMath::roundVec3HalfUp(to);
    if (!IRComponents::FogLineOfSightField::cellInField(target.x, target.y)) {
        return true;
    }
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    auto pool = IREntity::getComponentOptional<IRComponents::C_VoxelPool>(canvas);
    if (!pool.has_value()) {
        return true;
    }
    if (fog->losQueryColumnTops_.size() != IRComponents::kFogLosColumnCount) {
        fog->losQueryColumnTops_.assign(
            IRComponents::kFogLosColumnCount,
            IRComponents::kFogLosColumnEmpty
        );
    }
    rasterizeLosColumns(**pool, canvas, fog->losQueryColumnTops_);
    return static_cast<float>(target.z) <=
           traceLosHorizon(fog->losQueryColumnTops_, from, IRMath::ivec2(target));
}

/// Drop every live analytic vision disc → grid-only fog.
inline void clearVisionCircles() {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearVisionCircles();
    }
}

/// Write @p color, normalized per channel, into @p observers as the unexplored
/// anchor FOG_TO_TRIXEL reads. Every other member of the payload is untouched.
inline void
setUnexploredColor(IRComponents::FrameDataFogObservers &observers, IRMath::Color color) {
    observers.unexploredColor_ = IRMath::colorToVec4(color);
}

/// Set the colour FOG_TO_TRIXEL paints fully unexplored matter with (default
/// opaque black). Partially revealed pixels and the rim fade blend from it, so
/// a non-black value separates painted-hidden matter from an empty background.
inline void setUnexploredColor(IRMath::Color color) {
    if (auto *fog = detail::activeFogComponent()) {
        setUnexploredColor(fog->observers_, color);
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

/// Whole-body fog governance is restricted to the active grid canvas.
inline bool entityRevealGovernanceSupportsActiveCanvas(IREntity::EntityId entity) {
    auto setOpt = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity);
    if (!setOpt.has_value()) {
        return true;
    }
    const IREntity::EntityId activeCanvas = IRRender::getActiveCanvasEntityOrNull();
    const IREntity::EntityId canvas =
        (*setOpt)->canvasEntity_ == IREntity::kNullEntity ? activeCanvas : (*setOpt)->canvasEntity_;
    return canvas == activeCanvas;
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
