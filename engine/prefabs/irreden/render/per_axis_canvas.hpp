#ifndef IR_PREFAB_PER_AXIS_CANVAS_H
#define IR_PREFAB_PER_AXIS_CANVAS_H

// Driver-side lifecycle for the main world canvas's per-axis trixel canvases
// (smooth camera Z-yaw; docs/design/per-axis-trixel-canvas-rotation.md).
// Cross-entity orchestration — look up the main canvas, read the camera yaw,
// allocate, park and release the GPU set — lives here in a prefab-scoped
// namespace rather than on the component (engine/prefabs/CLAUDE.md Pattern B),
// so the C_PerAxisTrixelCanvases layout stays trivial and archetype-iteration
// friendly.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace IRPrefab::PerAxisCanvas {

// Minimum on-screen trixel size (framebuffer px) bounding the skinny-axis
// texture density (see IRMath::perAxisTrixelCanvasWorstCaseSize). ≈1 px is the
// configured balance between seam-free coverage and texture size.
inline constexpr float kMinOnScreenTrixelSizePx = 1.0f;

// Consecutive cardinal frames a parked per-axis set stays resident before it is
// freed: about two seconds at 60 fps. A turn that pauses on a cardinal, or a
// sweep that steps across one, re-enters the per-axis path on the resident set
// instead of re-allocating it; a camera that settles on a cardinal frees the
// memory once the window passes. Longer than any capture suite's settle between
// poses (the canvas_stress suite holds each pose 60 frames, the yaw ramp 16),
// so a suite that alternates cardinal and rotated poses exercises the unpark.
inline constexpr int kParkedCardinalFrames = 120;

// Diagnostic readback blocks until queued GPU work completes. Requires a live
// allocated set; the overflow control block begins with indexed draw arguments.
inline std::uint32_t readOverflowInstanceCount(const IRComponents::C_PerAxisTrixelCanvases &axes) {
    IRRender::device()->finish();
    std::uint32_t count = 0;
    axes.winnerIds_.second->getSubData(
        static_cast<std::ptrdiff_t>(axes.ctrlBaseUints_) * sizeof(std::uint32_t) +
            offsetof(IRRender::PerAxisCellDrawCommand, instanceCount),
        sizeof(count),
        &count
    );
    return count;
}

// Texel size of each per-axis face store for the current main canvas.
inline IRMath::ivec2 storeSize() {
    return IRMath::perAxisTrixelCanvasWorstCaseSize(
        IRMath::ivec2(IRRender::getMainCanvasSizeTrixels()),
        kMinOnScreenTrixelSizePx
    );
}

// The frame a store of @p size is keyed in this frame
// (FrameDataVoxelToCanvas::perAxisStoreFrame_): the nearest-cardinal view,
// with the window anchored on the world point at the middle of the screen.
// The store pass, every pass that inverts a store cell, and the framebuffer
// scatter each call this from the same camera state.
inline IRMath::ivec4 storeFrame(IRMath::ivec2 size) {
    if (IRRender::isSpatialViewEnabled()) {
        const float depth = IRRender::hasRotationPivotFocus()
                                ? IRRender::viewDepth(IRRender::getRotationPivotFocus())
                                : 0.0f;
        const auto center = IRRender::unprojectView(-IRRender::getEffectiveCameraIso(), depth);
        const auto projected = IRMath::pos3DtoPos2DIso(center);
        const IRMath::ivec2 anchor(
            IRMath::roundHalfUp(-projected.x),
            IRMath::roundHalfUp(-projected.y)
        );
        return IRMath::ivec4(IRMath::trixelOriginOffsetZ1(size) + anchor, 0, 0);
    }
    const float visualYaw = IRPrefab::Camera::getYaw();
    const IRMath::CardinalIndex cardinal =
        IRMath::rasterYawCardinalIndex(IRPrefab::Camera::computeYawSplit(visualYaw).first);
    const IRMath::ivec2 anchor = IRMath::perAxisStoreAnchor(
        IRRender::getEffectiveCameraIso(),
        visualYaw,
        cardinal,
        IRRender::getViewReferenceHeight()
    );
    return IRMath::ivec4(
        IRMath::trixelOriginOffsetZ1(size) + anchor,
        static_cast<int>(cardinal),
        0
    );
}

// Framebuffer offset (Y-up) the per-axis scatter adds for the camera's position
// inside its anchor cell; the scatter's anchor has already placed
// `floor(cameraIso)` whole cells.
//
// Under either policy it is the whole-pixel offset detached canvases place
// with (plus the carried phase when @p zoomFrame is the frame's continuous
// sample; null means the snapped policy), so the upscale residual that
// `FRAMEBUFFER_TO_SCREEN` adds on top is the remainder of THIS offset. An
// unrounded offset under that residual counts the sub-pixel fraction twice,
// and an edge steps back a screen pixel each time the two round at different
// camera positions.
//
// @p framebufferPxPerCell is read under the snapped policy only and must be
// the exact `IRMath::cameraZoomPitch` of the camera zoom.
inline IRMath::vec2 scatterCameraOffset(
    const IRMath::CameraRasterPhase *zoomFrame,
    const IRMath::vec2 &cameraIso,
    const IRMath::vec2 &framebufferPxPerCell
) {
    if (zoomFrame != nullptr) {
        return IRMath::vec2(IRMath::cameraRasterDetachedOffset(*zoomFrame));
    }
    return IRMath::cameraSnappedDetachedOffset(cameraIso, framebufferPxPerCell);
}

// Where the scatter places the faces of a store of @p storeSize cells in a
// framebuffer of @p framebufferResolution pixels. @p mainCanvasSize is the
// main canvas's logical size, @p canvasZoomLevel its display zoom over its
// raster density, and @p zoomFrame the frame's continuous-zoom sample (null
// under the snapped policy). Reads the camera pose, so it is the frame's one
// placement only while the camera is not written between its callers.
inline IRComponents::PerAxisScatterPlacement scatterPlacement(
    IRMath::ivec2 storeSize,
    IRMath::ivec2 mainCanvasSize,
    IRMath::vec2 framebufferResolution,
    IRMath::vec2 canvasZoomLevel,
    const IRMath::CameraRasterPhase *zoomFrame
) {
    using namespace IRMath;
    IRComponents::PerAxisScatterPlacement placement;
    // The store holds the face lattice at base (world-unit) resolution, so the
    // scatter's screen scale is the full camera zoom: multiply the effective
    // subdivisions canvasZoomLevel divided out back in, then rescale
    // component-wise for the larger per-axis texture extent.
    const int effSub = IRMath::max(IRRender::getVoxelRenderEffectiveSubdivisions(), 1);
    const vec2 zoomEff =
        canvasZoomLevel * static_cast<float>(effSub) * vec2(storeSize) / vec2(mainCanvasSize);

    placement.perAxisStoreFrame_ = storeFrame(storeSize);
    const float visualYaw = IRPrefab::Camera::getYaw();
    const auto cardinalIndex =
        IRMath::rasterYawCardinalIndex(IRPrefab::Camera::computeYawSplit(visualYaw).first);
    const auto visibleFaces =
        IRRender::isSpatialViewEnabled()
            ? IRMath::visibleTriplet(IRMath::quatInverse(IRRender::getViewRotation()))
            : IRMath::visibleFaceTripletCardinal(cardinalIndex);
    placement.cameraViewRotation_ =
        IRRender::isSpatialViewEnabled() ? IRRender::getViewRotation() : vec4(0.0f);
    // Whole-iso screen anchor of the recovered faces: the store is
    // base-resolution, so it is `floor(cameraIso)`, the cardinal path's
    // anchor, never scaled by the subdivision density.
    const vec2 cameraIso = IRRender::getEffectiveCameraIso();
    placement.perAxisBase_ =
        IRMath::trixelOriginOffsetZ1(storeSize) + ivec2(IRMath::floor(cameraIso));

    // Sub-cell camera pan. `perAxisBase_` places content in whole store cells
    // and the model scale makes one cell span the zoom pitch in framebuffer
    // pixels; the camera's offset inside its cell lands in whole framebuffer
    // pixels here and `FRAMEBUFFER_TO_SCREEN` adds the remainder.
    //
    // The pitch is the exact one every snapped stage floors against, never
    // `framebufferResolution * zoomEff / storeSize`: that round trip through
    // the store extent can land one ulp under the whole number, which costs
    // the floor a whole pixel.
    const vec2 cameraOffsetPx = scatterCameraOffset(
        zoomFrame,
        cameraIso,
        vec2(IRMath::cameraZoomPitch(IRRender::getCameraZoom()))
    );
    mat4 model = translate(
        mat4(1.0f),
        vec3(
            framebufferResolution.x * 0.5f + cameraOffsetPx.x,
            framebufferResolution.y * 0.5f + cameraOffsetPx.y,
            0.0f
        )
    );
    model = scale(
        model,
        vec3(framebufferResolution.x * zoomEff.x, framebufferResolution.y * zoomEff.y, 1.0f)
    );
    placement.mpMatrix_ =
        ortho(0.0f, framebufferResolution.x, 0.0f, framebufferResolution.y, -1.0f, 100.0f) * model;
    placement.visualYaw_ = visualYaw;
    placement.visibleFaceIds_ = ivec4(
        static_cast<int>(visibleFaces[0]),
        static_cast<int>(visibleFaces[1]),
        static_cast<int>(visibleFaces[2]),
        0
    );
    // The store is base-resolution but the SDF floor and the cardinal gather
    // encode depth subdivided, so the scatter lifts its depth by the effective
    // subdivisions to co-sort with them.
    placement.effectiveSubdivisions_ = static_cast<float>(effSub);
    placement.framebufferResolution_ = framebufferResolution;
    return placement;
}

// The scatter placement of the main canvas's store, derived ahead of
// TRIXEL_TO_FRAMEBUFFER from the state that system reads when it draws: the
// main canvas's render behaviour and zoom, the main framebuffer's extent and
// the zoom sample it is about to publish. KEEP IN SYNC with that system's
// per-canvas tick: a difference between the two reads as a camera write
// between the systems and stands the overflow rejection down.
inline IRComponents::PerAxisScatterPlacement mainCanvasScatterPlacement(IRMath::ivec2 storeSize) {
    using namespace IRMath;
    const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
    auto behaviorOptional =
        IREntity::getComponentOptional<IRComponents::C_TrixelCanvasRenderBehavior>(mainCanvas);
    const IRComponents::C_TrixelCanvasRenderBehavior behavior =
        behaviorOptional.has_value() ? (*behaviorOptional.value())
                                     : IRComponents::C_TrixelCanvasRenderBehavior{};
    auto zoomLevel = IREntity::getComponentOptional<IRComponents::C_ZoomLevel>(mainCanvas);
    const vec2 baseCanvasZoom =
        behavior.useCameraZoom_ ? IRRender::getCameraZoom()
                                : (zoomLevel.has_value() ? (*zoomLevel.value()).zoom_ : vec2(1.0f));
    const int canvasDensity =
        behavior.applyRenderSubdivisions_ &&
                IRRender::getSubdivisionMode() != IRRender::SubdivisionMode::NONE
            ? IRRender::getVoxelRenderEffectiveSubdivisions()
            : 1;
    const auto &framebuffer =
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const std::optional<IRMath::CameraRasterPhase> zoomFrame = IRPrefab::Camera::upcomingZoomFrame(
        IRRender::getEffectiveCameraIso(),
        IRRender::getCameraZoom()
    );
    return scatterPlacement(
        storeSize,
        ivec2(IRRender::getMainCanvasSizeTrixels()),
        vec2(framebuffer.getResolutionPlusBuffer()),
        baseCanvasZoom / vec2(canvasDensity),
        zoomFrame.has_value() ? &zoomFrame.value() : nullptr
    );
}

// What syncAllocationToCameraYaw does to the main canvas's per-axis set this
// frame.
enum class LifecycleStep : int {
    KEEP = 0,
    RESIZE_RESOLVE,
    REPLACE_LIVE,
    UNPARK_RESIZE_RESOLVE,
    ALLOCATE,       // rotating, nothing resident
    UNPARK,         // rotating, the parked set fits the cardinal canvas
    REPLACE_PARKED, // rotating, the parked set was sized for another canvas
    PARK,           // cardinal frame with a live set
    RELEASE_PARKED, // cardinal frame; the parked set has waited out its window
};

struct LifecycleState {
    bool rotating_ = false;
    bool live_ = false;
    bool parked_ = false;
    bool parkedFits_ = false;
    bool liveFits_ = true;
    bool liveResolveFits_ = true;
    bool parkedResolveFits_ = true;
    int parkedFrames_ = 0;
};

// The lifecycle policy with the ECS lookups and GPU calls lifted out, so it
// runs headlessly.
constexpr LifecycleStep lifecycleStep(LifecycleState state) {
    if (state.rotating_) {
        if (state.live_) {
            if (!state.liveFits_) {
                return LifecycleStep::REPLACE_LIVE;
            }
            return state.liveResolveFits_ ? LifecycleStep::KEEP : LifecycleStep::RESIZE_RESOLVE;
        }
        if (!state.parked_) {
            return LifecycleStep::ALLOCATE;
        }
        if (!state.parkedFits_) {
            return LifecycleStep::REPLACE_PARKED;
        }
        return state.parkedResolveFits_ ? LifecycleStep::UNPARK
                                        : LifecycleStep::UNPARK_RESIZE_RESOLVE;
    }
    if (state.live_) {
        return LifecycleStep::PARK;
    }
    if (state.parked_ && state.parkedFrames_ >= kParkedCardinalFrames) {
        return LifecycleStep::RELEASE_PARKED;
    }
    return LifecycleStep::KEEP;
}

// Keep the main canvas's per-axis set in step with the camera: live while the
// camera sits at a non-cardinal residual yaw, parked on a cardinal frame and
// freed after kParkedCardinalFrames of them. Idempotent — safe to call every
// frame; it acts only on the transitions lifecycleStep names. No-op when the
// main canvas has no C_PerAxisTrixelCanvases (e.g. before the renderer is
// wired). Called once per frame from VOXEL_TO_TRIXEL_STAGE_1::beginTick.
inline void syncAllocationToCameraYaw() {
    const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
    if (mainCanvas == IREntity::kNullEntity) {
        return;
    }
    auto perAxis =
        IREntity::getComponentOptional<IRComponents::C_PerAxisTrixelCanvases>(mainCanvas);
    if (!perAxis.has_value()) {
        return;
    }
    IRComponents::C_PerAxisTrixelCanvases &axes = *perAxis.value();

    const float residualYaw = IRPrefab::Camera::computeYawSplit(IRPrefab::Camera::getYaw()).second;
    // computeYawSplit deadbands the residual to exactly 0 at a settled cardinal
    // (Camera::kResidualYawDeadband), so this `!= 0` allocation gate and
    // the render path-select gate in system_voxel_to_trixel share the identical
    // predicate — they can never disagree about whether the per-axis textures
    // should be live.
    const bool rotating = IRRender::isSpatialViewEnabled() || residualYaw != 0.0f;

    // Face storage covers the logical viewport; the resolve uses the density-scaled backing.
    IRMath::ivec2 size{0, 0};
    IRMath::ivec2 mainSize{0, 0};
    if (rotating) {
        auto cardinal =
            IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
        if (!cardinal.has_value()) {
            return;
        }
        mainSize = (*cardinal.value()).size_;
        size = storeSize();
    }
    if (!rotating && !axes.isAllocated() && axes.hasParked()) {
        ++axes.parkedFrames_;
    }

    IRRender::RenderRunWitness &witness = IRRender::renderRunWitness();
    const auto timed = [](IRRender::CpuPhaseTiming &phase, auto &&step) {
        const IRRender::TimePoint start = IRRender::SteadyClock::now();
        step();
        phase.record(IRRender::elapsedMs(start, IRRender::SteadyClock::now()));
    };
    switch (lifecycleStep({
        .rotating_ = rotating,
        .live_ = axes.isAllocated(),
        .parked_ = axes.hasParked(),
        .parkedFits_ = axes.parked_.size_ == size,
        .liveFits_ = axes.size_ == size,
        .liveResolveFits_ = axes.mainSize_ == mainSize,
        .parkedResolveFits_ = axes.parked_.mainSize_ == mainSize,
        .parkedFrames_ = axes.parkedFrames_,
    })) {
    case LifecycleStep::KEEP:
        return;
    case LifecycleStep::RESIZE_RESOLVE:
        timed(witness.perAxisAllocate_, [&] { axes.resizeResolveDepth(mainSize); });
        return;
    case LifecycleStep::REPLACE_LIVE:
        timed(witness.perAxisRelease_, [&] { axes.release(); });
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::UNPARK_RESIZE_RESOLVE:
        timed(witness.perAxisUnpark_, [&] { axes.unpark(); });
        timed(witness.perAxisAllocate_, [&] { axes.resizeResolveDepth(mainSize); });
        return;
    case LifecycleStep::ALLOCATE:
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::UNPARK:
        timed(witness.perAxisUnpark_, [&] { axes.unpark(); });
        return;
    case LifecycleStep::REPLACE_PARKED:
        timed(witness.perAxisRelease_, [&] { axes.releaseParked(); });
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::PARK:
        timed(witness.perAxisPark_, [&] { axes.park(); });
        return;
    case LifecycleStep::RELEASE_PARKED:
        timed(witness.perAxisRelease_, [&] { axes.releaseParked(); });
        return;
    }
}

// Keep the subdivided voxel footprint inside the fixed-size per-axis store.
// Store, scatter and screen-depth resolve share this cap. Lighting-family
// position recovery instead uses the store frame and encoded sub-cell fraction.
inline int subdivisionDensity() {
    const int effSub = IRRender::getVoxelRenderEffectiveSubdivisions();
    if (IRRender::getSubdivisionMode() == IRRender::SubdivisionMode::NONE) {
        return effSub;
    }
    const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
    if (mainCanvas == IREntity::kNullEntity) {
        return effSub;
    }
    auto cardinal =
        IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
    if (!cardinal.has_value()) {
        return effSub;
    }
    const int cap = IRMath::perAxisSubdivisionCap(
        IRMath::ivec2(IRRender::getMainCanvasSizeTrixels()),
        IRRender::getCameraZoom(),
        kMinOnScreenTrixelSizePx
    );
    return IRMath::clamp(effSub, 1, cap);
}

// Density-dependent passes own the capped-density patch and restore the
// effective single-canvas density before returning.
inline void setUboSubdivisionDensity(IRRender::Buffer *frameDataUbo, int density) {
    frameDataUbo->subData(
        offsetof(IRRender::FrameDataVoxelToCanvas, voxelRenderOptions_) + sizeof(int),
        sizeof(int),
        &density
    );
}

// Rebind SSBO slots 25/26 (kBufferIndex_PerAxisCellCompacted/Indirect) to
// VOXEL_TO_TRIXEL_STAGE_1's single-canvas voxel-compaction buffers after a
// per-axis compute dispatch has borrowed them for its own cell list. Every
// per-axis consumer (AO, sun shadow, lighting, screen-depth
// resolve, the framebuffer scatter) binds these slots to its
// C_PerAxisTrixelCanvases-owned buffers via bindRange; leaving them bound
// past the dispatch means the next frame's STAGE_1 compact — which binds
// its own buffers once at create() and trusts sticky global state
// thereafter — silently re-reads the cell list as the voxel index list and
// corrupts world voxels (the center-cube regression). Callers own
// the lazy-resolved pointer pair as members (mirrors the member-on-System
// caching pattern) and pass them by reference so the named-resource lookup
// only runs once per system.
inline void restoreVoxelCompactionSlots(
    IRRender::Buffer *&voxelCompactedBuf, IRRender::Buffer *&voxelIndirectBuf
) {
    if (voxelCompactedBuf == nullptr) {
        voxelCompactedBuf = IRRender::getNamedResource<IRRender::Buffer>("CompactedVoxelIndices");
    }
    if (voxelIndirectBuf == nullptr) {
        voxelIndirectBuf = IRRender::getNamedResource<IRRender::Buffer>("IndirectDispatchParams");
    }
    // Unguarded: getNamedResource asserts on a miss rather than returning null
    // and VOXEL_TO_TRIXEL_STAGE_1, which creates both buffers,
    // is a hard precondition of every per-axis consumer.
    voxelCompactedBuf->bindBase(
        IRRender::BufferTarget::SHADER_STORAGE,
        IRRender::kBufferIndex_PerAxisCellCompacted
    );
    voxelIndirectBuf->bindBase(
        IRRender::BufferTarget::SHADER_STORAGE,
        IRRender::kBufferIndex_PerAxisCellIndirect
    );
}

// AO, sun shadow, lighting and fog use a boolean per-axis route; each cell's
// face id supplies its axis. Store-frame recovery is independent of density.
// Restore route zero, main-canvas size and borrowed voxel-compaction bindings
// on exit so subsequent single-canvas dispatches retain their frame state.
class LightingRouteScope {
  public:
    LightingRouteScope(
        IRRender::Buffer *frameDataUbo,
        IRRender::Buffer *&voxelCompactedBuf,
        IRRender::Buffer *&voxelIndirectBuf,
        IRMath::ivec2 storeCanvasSize,
        IRMath::ivec2 mainCanvasSize
    )
        : m_frameDataUbo{frameDataUbo}
        , m_voxelCompactedBuf{voxelCompactedBuf}
        , m_voxelIndirectBuf{voxelIndirectBuf}
        , m_mainCanvasSize{mainCanvasSize} {
        const int kPerAxisRoute = 1;
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kPerAxisRoute
        );
        setUboCanvasSize(storeCanvasSize);
    }

    ~LightingRouteScope() {
        const int kSingleCanvasRoute = 0;
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kSingleCanvasRoute
        );
        setUboCanvasSize(m_mainCanvasSize);
        restoreVoxelCompactionSlots(m_voxelCompactedBuf, m_voxelIndirectBuf);
    }

    LightingRouteScope(const LightingRouteScope &) = delete;
    LightingRouteScope &operator=(const LightingRouteScope &) = delete;
    LightingRouteScope(LightingRouteScope &&) = delete;
    LightingRouteScope &operator=(LightingRouteScope &&) = delete;

  private:
    void setUboCanvasSize(IRMath::ivec2 size) {
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, canvasSizePixels_),
            sizeof(IRMath::ivec2),
            &size
        );
    }

    IRRender::Buffer *m_frameDataUbo;
    // References to the owning system's lazily-resolved members (the
    // restoreVoxelCompactionSlots contract) — the scope is stack-local inside
    // one tick, so the referents always outlive it.
    IRRender::Buffer *&m_voxelCompactedBuf;
    IRRender::Buffer *&m_voxelIndirectBuf;
    IRMath::ivec2 m_mainCanvasSize;
};

// One indirect compute dispatch per axis over that axis's compacted OCCUPIED
// cell list: calls @p bindAxis(axis) for the pass-specific image
// bindings, binds the axis's region of the component-owned compacted-cell +
// dispatch-args buffers onto slots 25/26 (borrowing them — the caller restores
// via restoreVoxelCompactionSlots / LightingRouteScope), then issues the
// indirect dispatch from the axis's region of the args copy. One definition of the loop
// the per-axis AO / sun-shadow / lighting / screen-depth-resolve dispatches
// each hand-rolled.
template <typename BindAxis>
inline void dispatchPerAxisCells(IRComponents::C_PerAxisTrixelCanvases &axes, BindAxis &&bindAxis) {
    IRRender::Buffer *cellCompacted = axes.cellCompacted_.second;
    IRRender::Buffer *cellIndirect = axes.cellIndirect_.second;
    const int regionStride = axes.cellRegionStride_;
    for (int axis = 0; axis < IRComponents::C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
        bindAxis(axis);
        cellCompacted->bindRange(
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_PerAxisCellCompacted,
            static_cast<std::ptrdiff_t>(axis) * regionStride *
                static_cast<int>(sizeof(std::uint32_t)),
            static_cast<size_t>(regionStride) * sizeof(std::uint32_t)
        );
        cellIndirect->bindRange(
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_PerAxisCellIndirect,
            static_cast<std::ptrdiff_t>(axis) * IRRender::kPerAxisCellIndirectStrideBytes,
            IRRender::kPerAxisCellIndirectStrideBytes
        );
        IRRender::device()->dispatchComputeIndirect(
            axes.cellIndirectArgs_.second,
            static_cast<std::ptrdiff_t>(axis) * IRRender::kPerAxisCellIndirectStrideBytes +
                IRRender::kPerAxisCellDispatchArgsOffsetBytes
        );
    }
}

} // namespace IRPrefab::PerAxisCanvas

#endif /* IR_PREFAB_PER_AXIS_CANVAS_H */
