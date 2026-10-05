#ifndef IR_PREFAB_FOG_OF_WAR_H
#define IR_PREFAB_FOG_OF_WAR_H

// Driver-side API for the render pipeline's FOG_TO_TRIXEL pass. All
// operations apply to the active canvas's `C_CanvasFogOfWar` component
// and silently no-op when no canvas is active or the active canvas
// does not own one — this lets scripts and init code run before the
// canvas is fully wired without crashing.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/fog_line_of_sight.hpp>
#include <irreden/system/ir_assert_main_thread.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
inline float evalVisionReveal(
    const IRComponents::FrameDataFogObservers &observers,
    IRMath::vec3 worldPosition,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    float reveal = 0.0f;
    for (int i = 0; i < observers.visionCircleCount_; ++i) {
        if ((observers.channels(i) & channels) == 0u) {
            continue;
        }
        reveal = IRMath::max(reveal, detail::evalVisionCircleReveal(observers, i, worldPosition));
    }
    return reveal;
}

/// The authoritative reveal: the cost curve above, each source gated in
/// @p observers' `losSourceMask_` scaled by its line-of-sight factor at
/// @p worldPosition over @p los (`losVisibility`). @p observers and @p los
/// must come from one publication (`C_CanvasFogOfWar::losPublishedObservers_`
/// + `losField()`); an unpublished field reveals nothing through a gated
/// source. The march is the cost, so gated sources are marched strongest
/// curve first and only while one could still raise the maximum: a source
/// whose ungated reveal is already covered cannot change it.
inline float evalVisionReveal(
    const IRComponents::FrameDataFogObservers &observers,
    const IRComponents::FogLosColumnField &los,
    IRMath::vec3 worldPosition,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    float reveal = 0.0f;
    float pending[IRComponents::kMaxFogVisionCircles] = {};
    for (int i = 0; i < observers.visionCircleCount_; ++i) {
        if ((observers.channels(i) & channels) == 0u) {
            continue;
        }
        const float circleReveal = detail::evalVisionCircleReveal(observers, i, worldPosition);
        if (observers.losGated(i)) {
            pending[i] = circleReveal;
        } else {
            reveal = IRMath::max(reveal, circleReveal);
        }
    }
    for (;;) {
        int strongest = -1;
        for (int i = 0; i < observers.visionCircleCount_; ++i) {
            if (pending[i] > reveal && (strongest < 0 || pending[i] > pending[strongest])) {
                strongest = i;
            }
        }
        if (strongest < 0) {
            return reveal;
        }
        const float visibility = losVisibility(los, observers, strongest, worldPosition);
        reveal = IRMath::max(reveal, visibility * pending[strongest]);
        pending[strongest] = 0.0f;
    }
}

/// The observers and field a reveal evaluates. With a gated live source: the
/// last FOG_LOS_BUILD publication — sources and columns together, one RENDER
/// frame old — so a slot re-authored since then never pairs with another
/// frame's columns; before the first publication, the live set with an
/// unpublished field (gated sources reveal nothing). Without one: the live set,
/// and the field is never read.
inline void selectRevealSnapshot(
    const IRComponents::FrameDataFogObservers &live,
    const IRComponents::FrameDataFogObservers &published,
    IRComponents::FogLosColumnField publishedField,
    IRComponents::FrameDataFogObservers &observers,
    IRComponents::FogLosColumnField &los
) {
    if (live.losSourceMask_ != 0 && publishedField.published()) {
        observers = published;
        los = publishedField;
        return;
    }
    observers = live;
    los = {};
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

/// Whether @p voxelSet is subject to @p activeCanvas's fog: a set names that
/// canvas or none. The subject systems' ticks and their residency pre-pass
/// share it, so the pre-pass touches only regions a tick reads.
inline bool
isOnFogCanvas(const IRComponents::C_VoxelSetNew &voxelSet, IREntity::EntityId activeCanvas) {
    return voxelSet.canvasEntity_ == IREntity::kNullEntity ||
           voxelSet.canvasEntity_ == activeCanvas;
}

inline bool
isOnFogCanvas(const IRComponents::C_ShapeDescriptor &shape, IREntity::EntityId activeCanvas) {
    return shape.canvasEntity_ == IREntity::kNullEntity || shape.canvasEntity_ == activeCanvas;
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

inline void
stampBodyCarrierRecords(std::span<IRComponents::C_Voxel> records, bool body, std::uint8_t factor) {
    for (IRComponents::C_Voxel &voxel : records) {
        IRComponents::VoxelReserved::setFogCarrier(voxel, body, factor);
    }
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
    stampBodyCarrierRecords(poolRecords(pool, voxelSet), body, factor);
    stampBodyCarrierRecords(voxelSet.rotationSourceVoxels_, body, factor);
}

inline void stampCanvasBodyCarrier(
    const IRComponents::C_EntityCanvas &entityCanvas, bool body, std::uint8_t factor = 0
) {
    IRPrefab::VoxelPool::withPoolByEntity(
        entityCanvas.canvasEntity_,
        [body, factor](IRComponents::C_VoxelPool &pool) {
            pool.setFogCarrierPolicy(
                body ? IRComponents::C_VoxelPool::FogCarrierPolicy::BODY
                     : IRComponents::C_VoxelPool::FogCarrierPolicy::FIELD,
                factor
            );
        }
    );
}

/// The BODY verdict kernel: the larger of the grid term and the circle term.
/// @p gridCellState is the stored state of the cell under @p worldPosition
/// (the round-half-up column, the same cell the fog pass taps); a VISIBLE
/// cell reveals fully, an EXPLORED cell reveals nothing on its own. The
/// circle term is the line-of-sight gated `evalVisionReveal` on one snapshot
/// (@p observers + @p los, see `selectRevealSnapshot`). @p channels is
/// applied to analytic sources; the stored grid term is channel-blind.
inline float evalReveal(
    const IRComponents::FrameDataFogObservers &observers,
    const IRComponents::FogLosColumnField &los,
    std::uint8_t gridCellState,
    IRMath::vec3 worldPosition,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    if (gridCellState == IRComponents::kFogStateVisible) {
        return 1.0f;
    }
    return evalVisionReveal(observers, los, worldPosition, channels);
}

/// The BODY verdict at @p worldPosition against @p fog's world field and the
/// @p observers + @p los snapshot a caller captured once per frame: the
/// parallel reveal ticks' overload. The grid term reads the field's resident
/// state for any column through the non-loading `peekCell`, with no window
/// test; the caller's `beginTick` made the anchor's region resident
/// (`touchAnchorRegions`, fog-of-war-world-field.md D13).
inline float evalReveal(
    const IRComponents::C_CanvasFogOfWar &fog,
    const IRComponents::FrameDataFogObservers &observers,
    const IRComponents::FogLosColumnField &los,
    IRMath::vec3 worldPosition,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    const IRMath::ivec3 column = IRMath::roundVec3HalfUp(worldPosition);
    return evalReveal(observers, los, fog.peekCell(column.x, column.y), worldPosition, channels);
}

/// The BODY verdict at @p worldPosition against @p fog, on the snapshot the
/// reveal systems read. Serial: selects that snapshot per call and reads the
/// grid term through the loading `getCell`.
inline float evalReveal(
    const IRComponents::C_CanvasFogOfWar &fog,
    IRMath::vec3 worldPosition,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    IRComponents::FrameDataFogObservers observers;
    IRComponents::FogLosColumnField los;
    selectRevealSnapshot(
        fog.observers_,
        fog.losPublishedObservers_,
        fog.losField(),
        observers,
        los
    );
    const IRMath::ivec3 column = IRMath::roundVec3HalfUp(worldPosition);
    return evalReveal(observers, los, fog.getCell(column.x, column.y), worldPosition, channels);
}

namespace detail {

template <typename Subject>
inline void touchAnchorRegionsFor(
    IRComponents::C_CanvasFogOfWar &fog,
    IREntity::EntityId activeCanvas,
    const std::vector<IREntity::ArchetypeNode *> &nodes
) {
    if (!fog.hasPersistence()) {
        return;
    }
    bool touched = false;
    IRMath::ivec2 lastRegion{};
    for (IREntity::ArchetypeNode *node : nodes) {
        const auto &transforms = IREntity::getComponentData<IRComponents::C_WorldTransform>(node);
        const auto &subjects = IREntity::getComponentData<Subject>(node);
        for (int i = 0; i < node->length_; ++i) {
            if (!isOnFogCanvas(subjects[i], activeCanvas)) {
                continue;
            }
            const IRMath::ivec3 column = IRMath::roundVec3HalfUp(transforms[i].translation_);
            const IRMath::ivec2 region = WorldField::regionOfCell({column.x, column.y});
            if (touched && region == lastRegion) {
                continue;
            }
            fog.touchCell(column.x, column.y);
            lastRegion = region;
            touched = true;
        }
    }
}

} // namespace detail

/// The residency pre-pass of fog-of-war-world-field.md D13, run from the
/// `beginTick` of a `PARALLEL_FOR` system whose tick takes the verdict
/// through the snapshot overload above: touches the region of every anchor
/// in @p nodes (each carrying `C_WorldTransform` and `C_VoxelSetNew`) whose
/// set is on @p activeCanvas's fog, so the tick reads it resident. A set on
/// another canvas is skipped, as the tick skips it. Consecutive anchors in
/// one region cost one touch. Without persistence nothing can load, so the
/// walk is skipped.
inline void touchAnchorRegions(
    IRComponents::C_CanvasFogOfWar &fog,
    IREntity::EntityId activeCanvas,
    const std::vector<IREntity::ArchetypeNode *> &nodes
) {
    detail::touchAnchorRegionsFor<IRComponents::C_VoxelSetNew>(fog, activeCanvas, nodes);
}

inline void touchShapeAnchorRegions(
    IRComponents::C_CanvasFogOfWar &fog,
    IREntity::EntityId activeCanvas,
    const std::vector<IREntity::ArchetypeNode *> &nodes
) {
    detail::touchAnchorRegionsFor<IRComponents::C_ShapeDescriptor>(fog, activeCanvas, nodes);
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
/// component with no sources reveals nothing. Gated sources read the snapshot
/// `FOG_REVEAL_EVAL` reads (`selectRevealSnapshot`).
inline float evalActiveVisionReveal(IRMath::vec3 worldPosition) {
    if (auto *fog = detail::activeFogComponent()) {
        IRComponents::FrameDataFogObservers observers;
        IRComponents::FogLosColumnField los;
        selectRevealSnapshot(
            fog->observers_,
            fog->losPublishedObservers_,
            fog->losField(),
            observers,
            los
        );
        return evalVisionReveal(observers, los, worldPosition);
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

/// Set a single fog cell at world-space voxel column @p (worldX, worldY), any
/// representable column. State values: 0 = unexplored, 128 = explored,
/// 255 = visible.
inline void setCell(int worldX, int worldY, std::uint8_t state) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->setCell(worldX, worldY, state);
    }
}

/// Read the fog state at @p (worldX, worldY): the written state, raised to
/// visible under a live field-tier vision disc. Returns `kFogStateUnexplored`
/// if the active canvas has no fog component or neither applies.
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
    float freeBand = 0.0f,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearVisionCircles();
        return fog->addVisionCircle(
            cx,
            cy,
            radius,
            edge,
            observerZ,
            zCostUp,
            zCostDown,
            freeBand,
            channels
        );
    }
    return -1;
}

/// Append one vision source to the live set. See `setVisionCircle` for disc
/// semantics (including the @p observerZ / @p zCostUp / @p zCostDown /
/// @p freeBand height penalty); use this after `clearVisionCircles` to drive
/// several vision sources in one frame, highest priority first: the first
/// `kMaxFogVisionCircles` are analytic, every later one is field-tier — an XY
/// disc stamped into the world field with no edge softness, height cost, line
/// of sight, channels or explored memory (the contract is at
/// `C_CanvasFogOfWar::addVisionCircle`). Returns the analytic slot — the index
/// `setVisionCircleLineOfSight` takes — or -1 when the source took none
/// (dropped, field-tier, or no active fog canvas).
inline int addVisionCircle(
    float cx,
    float cy,
    float radius,
    float edge = IRComponents::kFogVisionEdgeDefault,
    float observerZ = 0.0f,
    float zCostUp = 0.0f,
    float zCostDown = IRComponents::kFogVisionZCostMirrorUp,
    float freeBand = 0.0f,
    std::uint32_t channels = IRComponents::kFogChannelDefault
) {
    if (auto *fog = detail::activeFogComponent()) {
        return fog->addVisionCircle(
            cx,
            cy,
            radius,
            edge,
            observerZ,
            zCostUp,
            zCostDown,
            freeBand,
            channels
        );
    }
    return -1;
}

/// Gate vision circle @p source by line of sight, the eye @p losEyeHeight world
/// units above its `observerZ`; `kFogVisionLosOff` (any negative height)
/// ungates it. @p losSoftness > 0 grades the verdict over that many world
/// units of clearance above an occluder; `kFogLosHardGate` keeps a step. See
/// `C_CanvasFogOfWar::setVisionCircleLineOfSight` for the slot contract and
/// the occluder model; the RENDER pipeline must carry `FOG_LOS_BUILD`. The
/// per-frame clear-then-add pattern re-enables it every frame.
inline void setVisionCircleLineOfSight(
    int source, float losEyeHeight, float losSoftness = IRComponents::kFogLosHardGate
) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->setVisionCircleLineOfSight(source, losEyeHeight, losSoftness);
    }
}

/// Whether @p to is visible from the eye @p from under the line-of-sight model
/// (the exact segment march of `component_canvas_fog_of_war.hpp`, with @p to
/// lifted onto its column's top plane when it sits below it), over the active
/// canvas's current occluders at the frame's raster lattice. The query's
/// field is anchored with the fog window the last gather uploaded, as
/// `FOG_LOS_BUILD` anchors the built one. True without an active fog canvas,
/// when @p to shares @p from's half-cell, and when @p to lies outside that
/// field.
///
/// Cost: rebuilds a 2 MiB column view from every live pool voxel and flagged
/// shape on each call, then one lattice walk. A caller issuing many queries
/// against the same occluders captures a `LineOfSightView` once
/// (`captureLineOfSight`) and queries that instead. Needs no registered vision
/// circle, and agrees with the built field and with a view captured on the
/// same occluders.
inline bool lineOfSight(IRMath::vec3 from, IRMath::vec3 to) {
    using IRComponents::FogLosColumnField;
    auto *fog = detail::activeFogComponent();
    if (fog == nullptr) {
        return true;
    }
    const IRMath::ivec2 fieldMin = fog->losFieldMinForLiveWindow();
    if (!FogLosColumnField::cellInField(
            IRMath::ivec2(FogLosColumnField::halfCellOf(to.x), FogLosColumnField::halfCellOf(to.y)),
            fieldMin
        )) {
        return true;
    }
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    auto pool = IREntity::getComponentOptional<IRComponents::C_VoxelPool>(canvas);
    if (!pool.has_value()) {
        return true;
    }
    if (fog->losQueryColumnTops_.size() != IRComponents::kFogLosFieldFloatCount) {
        fog->losQueryColumnTops_.assign(
            IRComponents::kFogLosFieldFloatCount,
            IRComponents::kFogLosColumnEmpty
        );
    }
    rasterizeLosColumns(**pool, canvas, activeLosRasterFrame(), fieldMin, fog->losQueryColumnTops_);
    return losPointVisible(FogLosColumnField{fog->losQueryColumnTops_.data(), fieldMin}, from, to);
}

/// Capture the active canvas's current occluders into @p view at the frame's
/// raster lattice and the field `lineOfSight` would anchor now, resolving the
/// canvas, fog and pool once; `LineOfSightView::visible` then answers with
/// `lineOfSight`'s verdicts for those occluders at one lattice walk per query.
/// Without an active fog canvas or a voxel pool the view is reset to empty and
/// answers true. The snapshot answers for the canvas active at capture even
/// after the active canvas switches. Main thread only: the rasterize traverses
/// the pool and the archetype graph.
inline void captureLineOfSight(LineOfSightView &view) {
    IR_ASSERT_MAIN_THREAD();
    auto *fog = detail::activeFogComponent();
    if (fog == nullptr) {
        view.reset();
        return;
    }
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    auto pool = IREntity::getComponentOptional<IRComponents::C_VoxelPool>(canvas);
    if (!pool.has_value()) {
        view.reset();
        return;
    }
    view.capture(**pool, canvas, activeLosRasterFrame(), fog->losFieldMinForLiveWindow());
}

/// Drop every live vision source, analytic and field-tier → grid-only fog.
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

/// Reset every cell to `kFogStateUnexplored`. With a persistence root set,
/// this also deletes the saved fog files under it.
inline void clear() {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearAll();
    }
}

/// Persist the active canvas's fog under @p saveRoot (one region file per
/// 512×512 columns; docs/design/fog-of-war-world-field.md D4): cells load on
/// first touch and save through `flushToDisk`. False without an active fog
/// canvas, for an empty root, or once the field holds any cell — set it after
/// `attachToCanvas(canvas, 0)` and before the first reveal. Accepting a root
/// makes the next gather re-upload the whole window, so saved state reaches
/// the texture even when frames rendered first. Destruction never saves.
inline bool setPersistenceRoot(std::string saveRoot) {
    auto *fog = detail::activeFogComponent();
    if (fog == nullptr) {
        return false;
    }
    std::optional<IRWorld::FieldChunkDiskPersistence> persistence =
        IRWorld::FieldChunkDiskPersistence::create(
            std::move(saveRoot),
            kFogFieldLayer,
            kFogFieldBytesPerCell
        );
    if (!persistence.has_value() || !fog->field_->setPersistence(std::move(*persistence))) {
        return false;
    }
    fog->windowOrigin_.reset();
    return true;
}

/// Save every changed region of the active canvas's persisted fog; returns
/// the number of region files written. 0 without an active fog canvas or a
/// persistence root.
inline int flushToDisk() {
    if (auto *fog = detail::activeFogComponent()) {
        return fog->field_->flush();
    }
    return 0;
}

/// Resident counts, plus region probes, loads, saves and evictions since the
/// previous call. Empty without an active fog canvas.
inline WorldFieldStats fieldStats() {
    if (auto *fog = detail::activeFogComponent()) {
        return fog->field_->stats();
    }
    return {};
}

/// Edge of the active canvas's fog window texture in columns; 0 without an
/// active fog canvas.
inline int windowEdge() {
    if (auto *fog = detail::activeFogComponent()) {
        return fog->windowEdge_;
    }
    return 0;
}

/// The field column at texel (0, 0) of the window the active canvas's fog
/// texture currently shows; nullopt without an active fog canvas or before
/// the first gather.
inline std::optional<IRMath::ivec2> windowOrigin() {
    if (auto *fog = detail::activeFogComponent()) {
        return fog->windowOrigin_;
    }
    return std::nullopt;
}

/// Attach both components FOG_TO_TRIXEL's archetype requires to @p canvas:
/// C_TrixelCanvasRenderBehavior (added only if absent, preserving any prior
/// customized behavior component) and a fresh C_CanvasFogOfWar whose window
/// is sized for @p canvas's trixel footprint (logged once as `FOG-WINDOW`,
/// with a warning when the cap leaves the periphery over-fogged).
/// FOG_TO_TRIXEL silently no-ops on a canvas missing either, so co-attaching
/// here removes that footgun from call sites. @p revealRadius > 0 also reveals
/// an origin-centered disc of that radius on @p canvas (up to
/// `kFogRevealRadiusMax`); 0 (default) attaches only, leaving the grid
/// unexplored. A persistent creation attaches with 0, calls
/// `setPersistenceRoot`, then reveals: a reveal here would make the root
/// refuse.
inline void attachToCanvas(IREntity::EntityId canvas, int revealRadius = 0) {
    if (!IREntity::getComponentOptional<IRComponents::C_TrixelCanvasRenderBehavior>(canvas)
             .has_value())
        IREntity::setComponent(canvas, IRComponents::C_TrixelCanvasRenderBehavior{});
    IRMath::ivec2 canvasSize(IRRender::getMainCanvasSizeTrixels());
    if (auto textures =
            IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(canvas)) {
        canvasSize = (*textures)->size_;
    }
    const int edge = detail::windowEdgeForCanvas(canvasSize);
    const int uncapped = detail::windowEdgeUncapped(canvasSize);
    IR_LOG_INFO(
        "FOG-WINDOW edge={} canvas={}x{} coveredRadius={}",
        edge,
        canvasSize.x,
        canvasSize.y,
        detail::windowCoveredRadius(edge)
    );
    if (uncapped > edge) {
        IR_LOG_WARN(
            "Fog window for a {}x{} canvas wants edge {} but is capped at {}: columns beyond "
            "a Chebyshev radius of {} from the view centre read unexplored",
            canvasSize.x,
            canvasSize.y,
            uncapped,
            edge,
            detail::windowCoveredRadius(edge)
        );
    }
    IREntity::setComponent(canvas, IRComponents::C_CanvasFogOfWar{canvasSize});
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
/// and restores rendering. EXEMPT pins every raster carrier at 255. Each class removes
/// the other two classes' markers and state, so a call on an already-classed
/// entity is a reclassification.
inline void setSubjectClass(IREntity::EntityId entity, FogSubjectClass subjectClass) {
    using IRComponents::C_FogExempt;
    using IRComponents::C_FogField;
    using IRComponents::C_FogRevealed;
    auto setOpt = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity);
    auto shapeOpt = IREntity::getComponentOptional<IRComponents::C_ShapeDescriptor>(entity);
    auto canvasOpt = IREntity::getComponentOptional<IRComponents::C_EntityCanvas>(entity);
    IREntity::EntityId canvas = IREntity::kNullEntity;
    std::size_t rangeStart = 0;
    std::size_t rangeCount = 0;
    bool setRenders = false;
    const bool combinedCanvasOwner = setOpt.has_value() && canvasOpt.has_value() &&
                                     (*setOpt)->canvasEntity_ == (*canvasOpt)->canvasEntity_;
    if (setOpt.has_value()) {
        IRComponents::C_VoxelSetNew *voxelSet = *setOpt;
        const IREntity::EntityId activeCanvas = IRRender::getActiveCanvasEntityOrNull();
        canvas = voxelSet->canvasEntity_ == IREntity::kNullEntity ? activeCanvas
                                                                  : voxelSet->canvasEntity_;
        IR_ASSERT(
            combinedCanvasOwner || activeCanvas == IREntity::kNullEntity || canvas == activeCanvas,
            "fog subject classes support the active grid canvas or its detached owner"
        );
        IRComponents::C_VoxelPool *pool = IRPrefab::VoxelPool::detail::poolForCanvas(canvas);
        if (pool != nullptr && !combinedCanvasOwner) {
            rangeStart = voxelSet->voxelStartIdx_;
            rangeCount = static_cast<std::size_t>(voxelSet->numVoxels_);
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
            voxelSet->visible_ = subjectClass != FogSubjectClass::BODY;
            setRenders = voxelSet->renders();
        }
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
            shape.flags_ |= IRRender::SHAPE_FLAG_FOG_BODY;
            shape.fogBodyFactor_ = 255;
        } else {
            shape.fogBodyFactor_ = 0;
        }
    }
    if (canvasOpt.has_value()) {
        IRComponents::C_EntityCanvas &entityCanvas = **canvasOpt;
        const bool detached = IREntity::getComponentOptional<IRComponents::C_DetachedCanvas>(
                                  entityCanvas.canvasEntity_
        )
                                  .has_value();
        if (!entityCanvas.screenLocked_ && detached) {
            entityCanvas.fogRevealFactor_ = subjectClass == FogSubjectClass::BODY ? 0.0f : 1.0f;
            entityCanvas.fogHidden_ = subjectClass == FogSubjectClass::BODY;
            stampCanvasBodyCarrier(
                entityCanvas,
                subjectClass != FogSubjectClass::FIELD,
                subjectClass == FogSubjectClass::EXEMPT ? 255u : 0u
            );
        }
    } else if (
        !setOpt.has_value() && !shapeOpt.has_value() && subjectClass == FogSubjectClass::BODY
    ) {
        return;
    }

    // Structural component changes migrate the entity's archetype, so they
    // stay last: every access through the voxel-set pointer is above.
    C_FogRevealed freshRevealed{};
    bool hadRevealed = false;
    if (const auto revealed = IREntity::getComponentOptional<C_FogRevealed>(entity);
        revealed.has_value()) {
        hadRevealed = true;
        freshRevealed.override_ = (*revealed)->override_;
        freshRevealed.channels_ = (*revealed)->channels_;
    }
    if (subjectClass == FogSubjectClass::BODY) {
        if (rangeCount > 0) {
            IRPrefab::VoxelPool::markRangeInactive(rangeStart, rangeCount, canvas);
        }
        IREntity::removeComponent<C_FogField>(entity);
        IREntity::removeComponent<C_FogExempt>(entity);
        IREntity::setComponent(entity, freshRevealed);
        return;
    }
    // A set its LOD band hides stays masked off; the LOD gate restores it.
    if (hadRevealed && setRenders && rangeCount > 0) {
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
