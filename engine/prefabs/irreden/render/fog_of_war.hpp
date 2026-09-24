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
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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

/// The three fog subject classes. BODY is the default: an untagged voxel set
/// on a fogged canvas is adopted by FOG_SUBJECT_ADOPT within one frame. FIELD
/// (terrain, painted per sample) and EXEMPT (never fogged) are explicit tags.
enum class FogSubjectClass : std::uint8_t { FIELD = 0, BODY = 1, EXEMPT = 2 };

/// The 8-bit carrier form of a BODY reveal factor: round half up of
/// `factor * 255`, so 1.0 pins 255 and 0.0 pins 0.
inline std::uint8_t quantizeRevealFactor(float factor) {
    return static_cast<std::uint8_t>(
        IRMath::roundHalfUp(IRMath::clamp(factor, 0.0f, 1.0f) * 255.0f)
    );
}

/// The pool records @p voxelSet owns, addressed through the live @p pool by
/// index rather than the set's cached span: a canvas migration copies the
/// pool component and relocates its storage, so the span a set captured at
/// allocation is not a per-frame handle. `[voxelStartIdx_, +numVoxels_)` is.
inline std::span<IRComponents::C_Voxel>
poolRecords(IRComponents::C_VoxelPool &pool, const IRComponents::C_VoxelSetNew &voxelSet) {
    std::vector<IRComponents::C_Voxel> &records = pool.getColors();
    if (voxelSet.numVoxels_ <= 0 ||
        voxelSet.voxelStartIdx_ + static_cast<std::size_t>(voxelSet.numVoxels_) > records.size()) {
        return {};
    }
    return std::span<IRComponents::C_Voxel>{
        records.data() + voxelSet.voxelStartIdx_,
        static_cast<std::size_t>(voxelSet.numVoxels_)
    };
}

/// The BODY carrier currently stamped on @p voxelSet's first record, or 0
/// when the set is neither BODY nor EXEMPT. Every record of a set carries the
/// same bits, so the first one is the set's stamp.
inline std::uint32_t
bodyCarrierBits(IRComponents::C_VoxelPool &pool, const IRComponents::C_VoxelSetNew &voxelSet) {
    const std::span<IRComponents::C_Voxel> records = poolRecords(pool, voxelSet);
    if (records.empty()) {
        return 0u;
    }
    return records[0].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask;
}

/// Write the BODY class bit and the quantized factor into every record of
/// @p voxelSet in @p pool and into the rotation-source mirror, so a rotated
/// re-voxelization carries the same verdict. `body == false` clears both
/// fields (the FIELD form).
inline void stampBodyCarrier(
    IRComponents::C_VoxelPool &pool,
    IRComponents::C_VoxelSetNew &voxelSet,
    bool body,
    std::uint8_t factor = 0
) {
    using IRComponents::VoxelReserved::kFogBody;
    using IRComponents::VoxelReserved::kFogBodyFactorShift;
    using IRComponents::VoxelReserved::kFogCarrierMask;
    const std::uint32_t bits =
        body ? (kFogBody | (static_cast<std::uint32_t>(factor) << kFogBodyFactorShift)) : 0u;
    for (IRComponents::C_Voxel &voxel : poolRecords(pool, voxelSet)) {
        voxel.reserved_ = (voxel.reserved_ & ~kFogCarrierMask) | bits;
    }
    for (IRComponents::C_Voxel &voxel : voxelSet.rotationSourceVoxels_) {
        voxel.reserved_ = (voxel.reserved_ & ~kFogCarrierMask) | bits;
    }
}

/// The BODY verdict kernel: the larger of the grid term and the circle term.
/// @p gridCellState is the stored state of the cell under @p worldPosition
/// (`C_CanvasFogOfWar::getCell` at the round-half-up column, the same cell
/// the fog pass taps); a VISIBLE cell reveals fully, an EXPLORED cell
/// reveals nothing on its own. @p channels is accepted for the source-mask
/// seam and is not yet consulted: every source reveals on the default
/// channel.
inline float evalReveal(
    const IRComponents::FrameDataFogObservers &observers,
    std::uint8_t gridCellState,
    IRMath::vec3 worldPosition,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    (void)channels;
    if (gridCellState == IRComponents::kFogStateVisible) {
        return 1.0f;
    }
    return evalVisionReveal(observers, worldPosition);
}

/// The BODY verdict at @p worldPosition against @p fog's grid and circles.
/// A column outside the grid reads as visible, the contract the fog pass
/// applies to its own out-of-range taps.
inline float evalReveal(
    const IRComponents::C_CanvasFogOfWar &fog,
    IRMath::vec3 worldPosition,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    const IRMath::ivec3 column = IRMath::roundVec3HalfUp(worldPosition);
    if (!IRComponents::C_CanvasFogOfWar::inBounds(column.x, column.y)) {
        return 1.0f;
    }
    return evalReveal(fog.observers_, fog.getCell(column.x, column.y), worldPosition, channels);
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

/// The BODY verdict the reveal systems compute, at @p worldPosition on the
/// active canvas: grid VISIBLE cell or circle term, whichever is larger. An
/// absent fog attachment leaves gameplay unrestricted.
inline float evalActiveReveal(
    IRMath::vec3 worldPosition, std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    if (auto *fog = detail::activeFogComponent()) {
        return evalReveal(*fog, worldPosition, channels);
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
/// the @p observerZ / @p zCostUp / @p zCostDown / @p freeBand height penalty);
/// use this after `clearVisionCircles` to drive several vision
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
    const IREntity::EntityId activeCanvas = IRRender::getActiveCanvasEntityOrNull();
    if (setOpt.has_value()) {
        const IREntity::EntityId canvas = (*setOpt)->canvasEntity_ == IREntity::kNullEntity
                                              ? activeCanvas
                                              : (*setOpt)->canvasEntity_;
        if (activeCanvas != IREntity::kNullEntity && canvas != activeCanvas) {
            return false;
        }
    }
    auto shapeOpt = IREntity::getComponentOptional<IRComponents::C_ShapeDescriptor>(entity);
    if (shapeOpt.has_value()) {
        const IREntity::EntityId canvas = (*shapeOpt)->canvasEntity_ == IREntity::kNullEntity
                                              ? activeCanvas
                                              : (*shapeOpt)->canvasEntity_;
        if (activeCanvas != IREntity::kNullEntity && canvas != activeCanvas) {
            return false;
        }
    }
    return true;
}

/// The class @p entity currently reads as. EXEMPT and FIELD are their
/// markers; everything else is BODY, which is also the class an untagged
/// set becomes once adopted.
inline FogSubjectClass subjectClass(IREntity::EntityId entity) {
    if (IREntity::getComponentOptional<IRComponents::C_FogExempt>(entity).has_value()) {
        return FogSubjectClass::EXEMPT;
    }
    if (IREntity::getComponentOptional<IRComponents::C_FogField>(entity).has_value()) {
        return FogSubjectClass::FIELD;
    }
    return FogSubjectClass::BODY;
}

/// Classify @p entity synchronously. BODY stamps the carrier with factor 0,
/// hides the subject and attaches `C_FogRevealed`, so an entity outside every
/// source cannot flash before its first eval. FIELD clears the BODY carrier
/// and restores rendering. EXEMPT pins voxel carriers at 255; shape exemption
/// is marker-only until its dedicated bypass system runs. Each class removes
/// the other two classes' markers and state, so a call on an already-classed
/// entity is a reclassification.
inline void setSubjectClass(IREntity::EntityId entity, FogSubjectClass subjectClass) {
    using IRComponents::C_FogExempt;
    using IRComponents::C_FogField;
    using IRComponents::C_FogRevealed;
    auto setOpt = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity);
    auto shapeOpt = IREntity::getComponentOptional<IRComponents::C_ShapeDescriptor>(entity);
    IREntity::EntityId canvas = IREntity::kNullEntity;
    std::size_t rangeStart = 0;
    std::size_t rangeCount = 0;
    if (setOpt.has_value()) {
        IRComponents::C_VoxelSetNew *voxelSet = *setOpt;
        const IREntity::EntityId activeCanvas = IRRender::getActiveCanvasEntityOrNull();
        canvas = voxelSet->canvasEntity_ == IREntity::kNullEntity ? activeCanvas
                                                                  : voxelSet->canvasEntity_;
        IR_ASSERT(
            activeCanvas == IREntity::kNullEntity || canvas == activeCanvas,
            "fog subject classes currently support only the active grid canvas"
        );
        rangeStart = voxelSet->voxelStartIdx_;
        rangeCount = static_cast<std::size_t>(voxelSet->numVoxels_);
        IRComponents::C_VoxelPool *pool = IRPrefab::VoxelPool::detail::poolForCanvas(canvas);
        if (pool != nullptr) {
            switch (subjectClass) {
            case FogSubjectClass::BODY:
                stampBodyCarrier(*pool, *voxelSet, true, 0);
                break;
            case FogSubjectClass::FIELD:
                stampBodyCarrier(*pool, *voxelSet, false);
                break;
            case FogSubjectClass::EXEMPT:
                stampBodyCarrier(*pool, *voxelSet, true, 255);
                break;
            }
        }
        voxelSet->visible_ = subjectClass != FogSubjectClass::BODY;
    }
    if (shapeOpt.has_value()) {
        IRComponents::C_ShapeDescriptor &shape = **shapeOpt;
        const IREntity::EntityId activeCanvas = IRRender::getActiveCanvasEntityOrNull();
        const IREntity::EntityId shapeCanvas =
            shape.canvasEntity_ == IREntity::kNullEntity ? activeCanvas : shape.canvasEntity_;
        IR_ASSERT(
            activeCanvas == IREntity::kNullEntity || shapeCanvas == activeCanvas,
            "fog subject classes currently support only the active grid canvas"
        );
        shape.flags_ &= ~IRRender::SHAPE_FLAG_FOG_HIDDEN;
        shape.flags_ &= ~IRRender::SHAPE_FLAG_FOG_BODY;
        if (subjectClass == FogSubjectClass::BODY) {
            shape.flags_ |= IRRender::SHAPE_FLAG_FOG_BODY;
            shape.flags_ |= IRRender::SHAPE_FLAG_FOG_HIDDEN;
            shape.fogBodyFactor_ = 0;
        } else if (subjectClass == FogSubjectClass::EXEMPT) {
            shape.fogBodyFactor_ = 255;
        } else {
            shape.fogBodyFactor_ = 0;
        }
    } else if (!setOpt.has_value() && subjectClass == FogSubjectClass::BODY) {
        return;
    }

    // Structural component changes migrate the entity's archetype, so they
    // stay last: every access through the voxel-set pointer is above.
    const bool hadRevealed = IREntity::getComponentOptional<C_FogRevealed>(entity).has_value();
    if (subjectClass == FogSubjectClass::BODY) {
        if (rangeCount > 0) {
            IRPrefab::VoxelPool::markRangeInactive(rangeStart, rangeCount, canvas);
        }
        IREntity::removeComponent<C_FogField>(entity);
        IREntity::removeComponent<C_FogExempt>(entity);
        IREntity::setComponent(entity, C_FogRevealed{});
        return;
    }
    if (hadRevealed && rangeCount > 0) {
        IRPrefab::VoxelPool::resyncRangeFromColors(rangeStart, rangeCount, canvas);
    }
    IREntity::removeComponent<C_FogRevealed>(entity);
    if (subjectClass == FogSubjectClass::FIELD) {
        IREntity::removeComponent<C_FogExempt>(entity);
        IREntity::setComponent(entity, C_FogField{});
        return;
    }
    IREntity::removeComponent<C_FogField>(entity);
    IREntity::setComponent(entity, C_FogExempt{});
}

/// Synchronous BODY adoption (`governed`), or the explicit FIELD tag
/// (`!governed`). A missing voxel set and shape makes the BODY form a no-op.
inline void setEntityRevealGoverned(IREntity::EntityId entity, bool governed = true) {
    setSubjectClass(entity, governed ? FogSubjectClass::BODY : FogSubjectClass::FIELD);
}

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_OF_WAR_H */
