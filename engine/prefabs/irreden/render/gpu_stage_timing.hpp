#ifndef GPU_STAGE_TIMING_H
#define GPU_STAGE_TIMING_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

#include <irreden/ir_math.hpp>

namespace IRRender {

inline constexpr float kFrameTimeBudgetMs = 1000.0f / 60.0f;

// Number of named GPU stages in `gpuStageRegistry()`. Single source of truth
// for both the registry array and the parallel per-stage accumulator array.
inline constexpr std::size_t kGpuStageCount = 29;

struct GpuStageTiming {
    float canvasClearMs_ = 0.0f;
    float voxelCompactMs_ = 0.0f;
    float voxelStage1Ms_ = 0.0f;
    float voxelStage2Ms_ = 0.0f;
    float voxelSunFacesMs_ = 0.0f;
    // Rotating-only per-axis burst sub-rows. Attributed by
    // GpuSubStageScope brackets inside the owning ticks; 0.0 at cardinal
    // (the per-axis canvases are released, none of the dispatches run).
    float voxelPerAxisStoreMs_ = 0.0f;
    float voxelPerAxisOverflowMs_ = 0.0f;
    float voxelPerAxisFinalizeMs_ = 0.0f;
    float perAxisCellCompactMs_ = 0.0f;
    float computeVoxelAoPerAxisMs_ = 0.0f;
    float lightingPerAxisMs_ = 0.0f;
    float lightingOverflowMs_ = 0.0f;
    float perAxisScatterMs_ = 0.0f;
    float shapeCompactMs_ = 0.0f;
    float shapePass0Ms_ = 0.0f;
    float shapePass1Ms_ = 0.0f;
    float textToTrixelMs_ = 0.0f;
    float buildLightOcclusionGridMs_ = 0.0f;
    float computeVoxelAoMs_ = 0.0f;
    float bakeSunShadowMapMs_ = 0.0f;
    float computeSunShadowMs_ = 0.0f;
    float computeLightVolumeMs_ = 0.0f;
    float lightingToTrixelMs_ = 0.0f;
    float fogToTrixelMs_ = 0.0f;
    float trixelToTrixelMs_ = 0.0f;
    float trixelToFbMs_ = 0.0f;
    float entityCanvasToFbMs_ = 0.0f;
    float resolvePerAxisScreenDepthMs_ = 0.0f;
    float fbToScreenMs_ = 0.0f;
    std::uint32_t visibleShapeCount_ = 0;
    std::uint32_t shapeGroupsZ_ = 0;
    // Last sampled compact dispatch; axis entries count repeated face routes.
    std::uint32_t visibleVoxelCount_ = 0;
    std::uint32_t totalVoxelCount_ = 0;
    std::uint32_t axisEntryCount_ = 0;
    // Shadow-feeder (struct 1) survivors from the same prior-frame readback —
    // the shadow-feeder tail population targeted by the domain-widened cull.
    // 0 whenever shadows are off / per-axis split active (feeders
    // exist only on the single-canvas path).
    std::uint32_t feederVoxelCount_ = 0;
    // Light-gather diagnostic. Populated by COMPUTE_LIGHT_VOLUME each
    // frame: `lightsSeeded_` = sources written to the seed SSBO (in-window
    // plus boundary-discounted), `lightsEligible_` = non-directional
    // canvas-scoped sources considered. eligible − seeded = sources whose
    // influence cannot reach the camera-anchored volume window.
    std::uint32_t lightsSeeded_ = 0;
    std::uint32_t lightsEligible_ = 0;
    // Shadow-feeder diagnostic. Populated by BAKE_SUN_SHADOW_MAP
    // each frame: `worldPlacedCasterCount_` = world-placed detached
    // re-voxelize canvases gathered for the cast resolve
    // (`gatherWorldPlacedCasters()`); `shadowFeederMin_`/`Max_` = the
    // iso-space AABB (shared cull viewport widened toward the sun by
    // `kSunShadowMaxDistance`, gated off when shadows are disabled) that
    // determines which off-screen casters still feed the bake.
    std::uint32_t worldPlacedCasterCount_ = 0;
    IRMath::vec2 shadowFeederMin_ = IRMath::vec2(0.0f);
    IRMath::vec2 shadowFeederMax_ = IRMath::vec2(0.0f);
    // Only flipped between frames by Lua on the main thread (Lua runs in
    // INPUT/UPDATE, never RENDER). Stable across the RENDER pipeline, so
    // probes can read `enabled_` twice and rely on both values matching.
    bool enabled_ = false;
    // Compatibility fallback using finish()-bracketed timing. Keep this off for
    // throughput runs; use it only for A/B checks.
    bool legacyFinishTiming_ = false;
};

inline GpuStageTiming &gpuStageTiming() {
    static GpuStageTiming instance;
    return instance;
}

using SteadyClock = std::chrono::steady_clock;
using TimePoint = SteadyClock::time_point;

inline float elapsedMs(TimePoint start, TimePoint end) {
    return std::chrono::duration<float, std::milli>(end - start).count();
}

// `budgetShare_` values do not sum to 1.0 — they are per-pass soft
// budgets reflecting the relative weight of each pass at current
// scales. A pass exceeding its share is flagged as over-budget; the
// absolute 16.67 ms frame limit is a separate top-line check.
struct GpuStageInfo {
    std::string_view name_;
    float GpuStageTiming::*field_;
    float budgetShare_;
};

struct CpuPhaseTiming {
    double totalMs_ = 0.0;
    double maxMs_ = 0.0;
    std::uint32_t sampleCount_ = 0;

    void record(double ms) {
        totalMs_ += ms;
        maxMs_ = IRMath::max(maxMs_, ms);
        ++sampleCount_;
    }

    void reset() {
        totalMs_ = 0.0;
        maxMs_ = 0.0;
        sampleCount_ = 0;
    }
};

struct ComputeLightVolumeTiming {
    CpuPhaseTiming clear_;
    CpuPhaseTiming populate_;
    CpuPhaseTiming upload_;

    void reset() {
        clear_.reset();
        populate_.reset();
        upload_.reset();
    }
};

inline ComputeLightVolumeTiming &computeLightVolumeTiming() {
    static ComputeLightVolumeTiming instance;
    return instance;
}

// Samples are compact dispatches, not frame totals: shared buffers can pass
// between canvases within a frame. Visible and feeder are disjoint source
// candidates; axis entries count repeated per-axis list work.
struct VoxelCullAccumulator {
    std::uint64_t visibleSum_ = 0;
    std::uint64_t totalSum_ = 0;
    std::uint64_t feederSum_ = 0;
    std::uint64_t axisEntrySum_ = 0;
    std::uint32_t maxVisible_ = 0;
    std::uint32_t maxTotal_ = 0;
    std::uint32_t maxFeeder_ = 0;
    std::uint32_t maxAxisEntries_ = 0;
    std::uint32_t sampleCount_ = 0;

    void record(
        std::uint32_t visible,
        std::uint32_t total,
        std::uint32_t feeder,
        std::uint32_t axisEntries = 0
    ) {
        visibleSum_ += visible;
        totalSum_ += total;
        feederSum_ += feeder;
        axisEntrySum_ += axisEntries;
        maxAxisEntries_ = IRMath::max(maxAxisEntries_, axisEntries);
        maxVisible_ = IRMath::max(maxVisible_, visible);
        maxTotal_ = IRMath::max(maxTotal_, total);
        maxFeeder_ = IRMath::max(maxFeeder_, feeder);
        ++sampleCount_;
    }

    void reset() {
        visibleSum_ = 0;
        totalSum_ = 0;
        feederSum_ = 0;
        axisEntrySum_ = 0;
        maxAxisEntries_ = 0;
        maxVisible_ = 0;
        maxTotal_ = 0;
        maxFeeder_ = 0;
        sampleCount_ = 0;
    }
};

inline VoxelCullAccumulator &voxelCullAccumulator() {
    static VoxelCullAccumulator instance;
    return instance;
}

// What a profile report vouches for in a build that logs nothing: the camera
// pose the voxel pass rendered at and the per-axis overflow lane's worst
// frame. Recorded every frame, independent of stage timing. Yaw is radians in
// [-π, π); travel sums the wrapped per-frame yaw change, so a static pose
// reads 0 and a sweep reads its arc across the ±π seam. Overflow samples stay
// 0 at a cardinal pose, where the per-axis canvases are not live. The overflow
// counters are read one frame late: the first sample after an allocation is
// the zero-seeded block, the first after an unpark is the last frame before the
// park, and the last rotating frame before a release or exit is never read.
// Unsynchronized: every writer is reached from VOXEL_TO_TRIXEL_STAGE_1, which
// runs serially on the main thread.
struct RenderRunWitness {
    float yawFirst_ = 0.0f;
    float yawLast_ = 0.0f;
    float yawTravel_ = 0.0f;
    float zoomFirst_ = 0.0f;
    float zoomLast_ = 0.0f;
    std::uint32_t poseSamples_ = 0;
    // Pose samples rendered with an explicit yaw pivot focus. With the default
    // pivot the part of the world a yaw shows depends on how the run began.
    std::uint32_t explicitPivotSamples_ = 0;
    std::uint32_t overflowSamples_ = 0;
    std::uint32_t maxOverflowEntries_ = 0;
    std::uint32_t maxOverflowDropped_ = 0;
    std::uint32_t overflowCap_ = 0;
    // CPU time inside the per-axis set's lifecycle calls, recorded on each
    // transition and not every frame; a GPU driver may defer part of an
    // allocation's cost to first use. Park and unpark are pointer swaps, so
    // their sample counts, not their milliseconds, are the record.
    CpuPhaseTiming perAxisAllocate_;
    CpuPhaseTiming perAxisRelease_;
    CpuPhaseTiming perAxisPark_;
    CpuPhaseTiming perAxisUnpark_;

    void recordPose(float yaw, float zoom, bool explicitPivot) {
        explicitPivotSamples_ += explicitPivot ? 1u : 0u;
        if (poseSamples_ == 0) {
            yawFirst_ = yaw;
            zoomFirst_ = zoom;
        } else {
            yawTravel_ += IRMath::abs(IRMath::wrapAnglePi(yaw - yawLast_));
        }
        yawLast_ = yaw;
        zoomLast_ = zoom;
        ++poseSamples_;
    }

    void recordOverflow(std::uint32_t entries, std::uint32_t dropped, std::uint32_t cap) {
        maxOverflowEntries_ = IRMath::max(maxOverflowEntries_, entries);
        maxOverflowDropped_ = IRMath::max(maxOverflowDropped_, dropped);
        overflowCap_ = cap;
        ++overflowSamples_;
    }

    void reset() {
        *this = RenderRunWitness{};
    }
};

inline RenderRunWitness &renderRunWitness() {
    static RenderRunWitness instance;
    return instance;
}

// Per-stage running GPU-timing accumulator. The `gpu_stage_timing_observer`
// records one sample per resolved timestamp pair per stage; the world's
// profile-report builder drains the array (indexed parallel to
// `gpuStageRegistry()` order) at shutdown for true avg / min / max across the
// run. The single `GpuStageTiming::*Ms_` field only ever holds the *last*
// frame's sample, so without this accumulator the report can only echo that
// one value, so Avg and Max intentionally match in that mode.
// `enableFrameTiming(true)` calls `resetGpuStageAccumulators()` so each
// measurement run starts from zero.
struct GpuStageAccumulator {
    double sumMs_ = 0.0;
    float maxMs_ = 0.0f;
    float minMs_ = 0.0f;
    std::uint32_t sampleCount_ = 0;

    void record(float ms) {
        sumMs_ += ms;
        maxMs_ = IRMath::max(maxMs_, ms);
        minMs_ = sampleCount_ == 0 ? ms : IRMath::min(minMs_, ms);
        ++sampleCount_;
    }

    void reset() {
        sumMs_ = 0.0;
        maxMs_ = 0.0f;
        minMs_ = 0.0f;
        sampleCount_ = 0;
    }
};

inline std::array<GpuStageAccumulator, kGpuStageCount> &gpuStageAccumulators() {
    static std::array<GpuStageAccumulator, kGpuStageCount> instance{};
    return instance;
}

inline void resetGpuStageAccumulators() {
    for (auto &acc : gpuStageAccumulators()) {
        acc.reset();
    }
}

// Commit one resolved GPU-timing sample for a registry stage: overwrite the
// live last-sample field the perf overlay reads, and feed the running
// accumulator the shutdown profile report drains. Shared by the per-system
// `GpuStageTimingObserver` and the intra-tick `GpuSubStageScope` so both write
// samples through one code path. `registryIndex` is the stage's slot in
// `gpuStageRegistry()` order (== its `gpuStageAccumulators()` index).
inline void commitGpuStageSample(const GpuStageInfo &info, int registryIndex, float ms) {
    gpuStageTiming().*(info.field_) = ms;
    if (registryIndex >= 0 &&
        static_cast<std::size_t>(registryIndex) < gpuStageAccumulators().size()) {
        gpuStageAccumulators()[registryIndex].record(ms);
    }
}

// Authoritative mapping name → field → budget. `gpu_stage_timing_observer`
// resolves a system's tag against this table; pass the same name to
// `IRRender::tagGpuStage(system, "<name>")` and the observer fills the
// matching `GpuStageTiming::*Ms_` field at end-of-tick.
//
// Per-system mapping (one `GpuStageTimingObserver` fire per `SystemId`):
//   `shapePass1`   ← SHAPES_TO_TRIXEL (covers former shapePass0 + shapePass1)
//   Most remaining names map 1:1 to single-stage systems.
//
// Intra-tick sub-stage rows: VOXEL_TO_TRIXEL_STAGE_1 is NOT tagged for
// the per-system observer. Instead its per-canvas tick brackets each of its
// four dispatch groups with a `GpuSubStageScope` (gpu_substage_timing.hpp),
// so these rows are attributed individually rather than bundled:
//   `canvasClear`  ← the per-frame distance-texture clear (blit)
//   `voxelCompact` ← the visibility-compaction dispatch
//   `voxelStage1`  ← the stage-1 raster dispatch only
//   `voxelStage2`  ← the stage-2 dispatch (runs inside STAGE_1's tick)
// The bundled `voxelStage1` value is reconstructed as the sum of these
// four rows. Sub-scopes are single-canvas-exact and record the last canvas's
// sample on multi-canvas scenes (like every `*Ms_` field's last-sample
// semantics).
//
// Per-axis burst sub-rows: COMPUTE_VOXEL_AO,
// LIGHTING_TO_TRIXEL, and TRIXEL_TO_FRAMEBUFFER are likewise NOT tagged for
// the per-system observer; each brackets its dispatch groups with
// GpuSubStageScopes so the rotating-only per-axis work is attributed
// separately from the always-on main-canvas work:
//   `computeVoxelAO`        ← the main-canvas AO dispatch ONLY
//   `computeVoxelAoPerAxis` ← the 3 per-axis AO dispatches
//   `lightingToTrixel`      ← the main-canvas lighting dispatch ONLY
//   `lightingPerAxis`       ← the 3 per-axis relight dispatches
// `lightingOverflow` ← the overflow-face relight dispatch
//   `trixelToFb`            ← the single-canvas gather draw ONLY
//   `perAxisScatter`        ← the 3 per-axis scatter draws + overflow draw
// and VOXEL_TO_TRIXEL_STAGE_1's rotating-only per-axis dispatch groups get
// their own rows (phases per docs/design/per-axis-trixel-canvas-rotation.md
// §"The overflow lane"):
//   `voxelPerAxisStore`     ← per-axis clears + cardinal stores + view-mask writes ×3
//   `voxelPerAxisOverflow`  ← overflow append ×3
//   `voxelPerAxisFinalize`  ← winner election + stage-2 ×3
//   `perAxisCellCompact`    ← the occupied-cell compaction + finalize
//                             dispatches feeding every per-axis consumer
// Every per-axis row reads 0.0 at cardinal (the canvases are released and
// none of those dispatches run), so cardinal-vs-yaw row deltas ARE the
// per-axis burst attribution.
//
// Two rows still have no current writer: `shapePass0` (folded into the
// SHAPES_TO_TRIXEL per-system measurement) and `shapeCompact` (no system has
// a writer). They stay in the registry to keep the Lua API and perf overlay
// stable — the overlay still
// shows them at 0.0f; the shutdown profile report omits them (sampleCount_ == 0).
inline const std::array<GpuStageInfo, kGpuStageCount> &gpuStageRegistry() {
    static const std::array<GpuStageInfo, kGpuStageCount> registry{{
        {"canvasClear", &GpuStageTiming::canvasClearMs_, 0.05f},
        {"voxelCompact", &GpuStageTiming::voxelCompactMs_, 0.10f},
        {"voxelStage1", &GpuStageTiming::voxelStage1Ms_, 0.20f},
        {"voxelStage2", &GpuStageTiming::voxelStage2Ms_, 0.15f},
        {"voxelSunFaces", &GpuStageTiming::voxelSunFacesMs_, 0.0f},
        {"voxelPerAxisStore", &GpuStageTiming::voxelPerAxisStoreMs_, 0.10f},
        {"voxelPerAxisOverflow", &GpuStageTiming::voxelPerAxisOverflowMs_, 0.05f},
        {"voxelPerAxisFinalize", &GpuStageTiming::voxelPerAxisFinalizeMs_, 0.10f},
        {"perAxisCellCompact", &GpuStageTiming::perAxisCellCompactMs_, 0.05f},
        {"computeVoxelAoPerAxis", &GpuStageTiming::computeVoxelAoPerAxisMs_, 0.05f},
        {"lightingPerAxis", &GpuStageTiming::lightingPerAxisMs_, 0.05f},
        {"lightingOverflow", &GpuStageTiming::lightingOverflowMs_, 0.05f},
        {"perAxisScatter", &GpuStageTiming::perAxisScatterMs_, 0.10f},
        {"shapeCompact", &GpuStageTiming::shapeCompactMs_, 0.05f},
        {"shapePass0", &GpuStageTiming::shapePass0Ms_, 0.10f},
        {"shapePass1", &GpuStageTiming::shapePass1Ms_, 0.10f},
        {"textToTrixel", &GpuStageTiming::textToTrixelMs_, 0.05f},
        {"buildLightOcclusionGrid", &GpuStageTiming::buildLightOcclusionGridMs_, 0.10f},
        {"computeVoxelAO", &GpuStageTiming::computeVoxelAoMs_, 0.10f},
        {"bakeSunShadowMap", &GpuStageTiming::bakeSunShadowMapMs_, 0.10f},
        {"computeSunShadow", &GpuStageTiming::computeSunShadowMs_, 0.10f},
        {"computeLightVolume", &GpuStageTiming::computeLightVolumeMs_, 0.10f},
        {"lightingToTrixel", &GpuStageTiming::lightingToTrixelMs_, 0.10f},
        {"fogToTrixel", &GpuStageTiming::fogToTrixelMs_, 0.05f},
        {"trixelToTrixel", &GpuStageTiming::trixelToTrixelMs_, 0.05f},
        {"trixelToFb", &GpuStageTiming::trixelToFbMs_, 0.15f},
        {"entityCanvasToFb", &GpuStageTiming::entityCanvasToFbMs_, 0.05f},
        {"resolvePerAxisScreenDepth", &GpuStageTiming::resolvePerAxisScreenDepthMs_, 0.05f},
        {"fbToScreen", &GpuStageTiming::fbToScreenMs_, 0.05f},
    }};
    return registry;
}

inline float budgetMsFor(const GpuStageInfo &info) {
    return kFrameTimeBudgetMs * info.budgetShare_;
}

} // namespace IRRender

#endif /* GPU_STAGE_TIMING_H */
