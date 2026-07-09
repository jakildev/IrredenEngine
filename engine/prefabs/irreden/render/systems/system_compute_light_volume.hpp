#ifndef SYSTEM_COMPUTE_LIGHT_VOLUME_H
#define SYSTEM_COMPUTE_LIGHT_VOLUME_H

// GPU light propagation. For each `C_LightSource` entity in the world,
// uploads the light's world voxel origin + emissive color + intensity
// to a small SSBO, then dispatches three compute passes against the
// canvas's `C_CanvasLightVolume` ping-pong 3D textures:
//
//   1. `c_clear_light_volume` zeroes the read texture.
//   2. `c_seed_light_volume` writes one bright texel per light at its
//      world voxel origin: rgb = emit_color × intensity, alpha = the
//      CPU-computed seed residual (1.0 for in-window lights; lights whose
//      origin falls outside the camera-anchored window seed the nearest
//      boundary cell at the residual the propagation would have carried
//      there — see `gatherLightSources`).
//   3. `c_propagate_light_volume` runs `kLightVolumePropagateIterations`
//      Manhattan-dilation iterations against the camera-anchored
//      light-occlusion SSBO produced by `BUILD_LIGHT_OCCLUSION_GRID`
//      (light cannot pass through solid voxels or SDF light blockers).
//      Each step decrements alpha by `stepFalloff` and propagates the
//      closest light's color, giving linear falloff bounded at radius
//      `1 / stepFalloff`. The downstream consumer reads `rgb × alpha`
//      so out-of-range cells contribute zero. The ping-pong textures are
//      swapped after each iteration so the `LIGHTING_TO_TRIXEL` pass
//      always samples the latest state via
//      `C_CanvasLightVolume::getReadTexture()`.
//
// Pipeline order constraint: must run after `BUILD_LIGHT_OCCLUSION_GRID`
// (so the SSBO mirrors the current frame's voxel state) and before
// `LIGHTING_TO_TRIXEL` (which samples the light volume).
//
// Phase 1a (issue #359) replaced the previous CPU 6-connected BFS +
// 8 MiB sub-image upload with the GPU pass chain above. The CPU portion
// of this system is now bounded by the small per-frame light-source SSBO
// upload (~20 KiB at the 256-light cap; 80 B/light since #2318 added the
// true-origin field). Per-light radius variation and analytic point/spot
// LOS are pending for later phases.
//
// Winning-light ID channel (#2318, L2). A parallel RGBA8 ID ping-pong pair
// on `C_CanvasLightVolume` records the index+1 of the light that won each
// cell's residual contest: the clear zeroes it, the seed writes each light's
// id at its origin cell, and the propagate carries the winning candidate's id
// inward in lockstep with the color/alpha (the ping-pong swaps both pairs
// together). `LIGHTING_TO_TRIXEL` fetches the id at the surface cell and, when
// the winner is a SPOT, attenuates its volume contribution by an analytic cone
// factor — real cone shaping with no ray marching. Gated by
// `LightVolumeParams::worldOriginVoxel_.w` (the has-SPOT flag set below), so
// scenes with no seeded SPOT stay byte-identical.
//
// Phase 1c (#360) anchors the volume window on the camera so most scenes
// keep every light in-range without growing the texture footprint. A light
// whose origin falls outside the window is NOT dropped: it seeds the
// per-axis-clamped boundary cell at a distance-discounted residual alpha,
// which reproduces the exact in-window light field (the propagate pass
// decrements alpha per Manhattan step, and the L1 triangle inequality is
// an equality under per-axis clamping), so contribution fades continuously
// as the camera pans away instead of popping at a window margin. Lights
// whose discounted residual is ≤ 0 cannot reach the window and are skipped;
// the gathered/eligible counts surface on the perf HUD's CULL block.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/detail/camera_anchor.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Per-light gather outcome (#2315, V1 DOMAIN-STATE instrumentation).
// `SEEDED_FULL` — origin fell inside the camera-anchored window, no
// boundary clamp. `BOUNDARY_DISCOUNTED` — origin clamped to the window
// edge; `residual_` is the seed alpha the clamp survived at. `SKIPPED` —
// the discounted residual was ≤ 0 (light cannot reach the window);
// `residual_` is 0. Public (not `detail`) — the DOMAIN-STATE emission hook
// in a lighting demo's `main.cpp` reads these back via `lightGatherRecords()`
// below.
enum class LightGatherState : std::uint8_t {
    SEEDED_FULL,
    BOUNDARY_DISCOUNTED,
    SKIPPED,
};

struct LightGatherRecord {
    IREntity::EntityId entity_;
    LightGatherState state_;
    float residual_;
};

namespace detail {

class ScopedCpuPhaseTimer {
  public:
    explicit ScopedCpuPhaseTimer(IRRender::CpuPhaseTiming &timing)
        : m_timing{timing}
        , m_start{std::chrono::steady_clock::now()} {}

    ~ScopedCpuPhaseTimer() {
        const auto end = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double, std::milli>(end - m_start);
        m_timing.record(elapsed.count());
    }

  private:
    IRRender::CpuPhaseTiming &m_timing;
    std::chrono::steady_clock::time_point m_start;
};

// `seedCellVoxel` is where the seed pass writes this light's texel (the true
// origin for in-window lights, or the clamped window-boundary cell for an
// out-of-window light); `trueOriginVoxel` is the light's unclamped apex,
// carried separately so the spot-cone consumer orients the cone from the real
// position even when the seed cell is clamped to the window edge (#2318).
inline GPULightSource toGpuLight(
    const C_LightSource &light,
    const ivec3 &seedCellVoxel,
    const ivec3 &trueOriginVoxel,
    float seedAlpha
) {
    GPULightSource gpu{};
    gpu.originAndType_ = vec4(
        static_cast<float>(seedCellVoxel.x),
        static_cast<float>(seedCellVoxel.y),
        static_cast<float>(seedCellVoxel.z),
        static_cast<float>(light.type_)
    );
    gpu.colorAndIntensity_ = vec4(
        static_cast<float>(light.emitColor_.red_) / 255.0f,
        static_cast<float>(light.emitColor_.green_) / 255.0f,
        static_cast<float>(light.emitColor_.blue_) / 255.0f,
        IRMath::max(0.0f, light.intensity_)
    );
    const float radius = static_cast<float>(light.radius_);
    gpu.directionAndRadius_ =
        vec4(light.direction_.x, light.direction_.y, light.direction_.z, radius);
    gpu.coneAndSeedAlpha_ = vec4(light.coneAngleDeg_, seedAlpha, 0.0f, 0.0f);
    gpu.trueOriginVoxel_ = vec4(
        static_cast<float>(trueOriginVoxel.x),
        static_cast<float>(trueOriginVoxel.y),
        static_cast<float>(trueOriginVoxel.z),
        0.0f
    );
    return gpu;
}

inline ivec3 roundedLightOrigin(const C_WorldTransform &transform) {
    // Round-half-up so light origins line up with the light-occlusion
    // grid cells populated by `system_build_light_occlusion_grid` (also
    // round-half-up).
    return IRMath::roundVec3HalfUp(transform.translation_);
}

// Gathers all lights with positions into the supplied buffer (capped
// at `kLightVolumeMaxSources`). Returns the count actually written.
// Per-tick allocation is bounded by the buffer's reserved capacity.
//
// `currentCanvas` scopes the gather to lights that target this canvas —
// either world-scope lights (no CHILD_OF parent, the back-compat default)
// or lights whose CHILD_OF parent IS this canvas. Lights parented to a
// different canvas (or any other entity) are skipped. The parent lookup
// is per-archetype-node, not per-light, because all entities in a node
// share the same relation tag in their archetype — a node's lights are
// either all eligible for this canvas or all not.
//
// A light whose origin rounds outside the camera-anchored window seeds
// the per-axis-clamped boundary cell at a distance-discounted residual:
// `seedAlpha = 1 − manhattan(origin, clamped) × stepFalloff`. Because the
// propagate pass decrements alpha by `stepFalloff` per Manhattan step and
// per-axis clamping makes the L1 triangle inequality an equality, this
// reproduces the exact field the light would produce inside the window if
// the volume were unbounded (occluders outside the window are unknowable
// either way). Contribution therefore fades continuously as the camera
// pans away — no pop, no margin band. Lights whose discounted residual
// is ≤ 0 cannot reach the window and are skipped silently; `outEligible`
// vs the return count surfaces the drop rate on the perf HUD.
//
// `outMaxRadius` receives the largest `C_LightSource::radius_` across ALL
// eligible (non-DIRECTIONAL, canvas-scoped) lights — deliberately NOT just
// the seeded subset: `stepFalloff` derives from it and must be
// camera-independent (a light crossing the window boundary must not shift
// any other light's falloff curve). Capped at
// `kLightVolumePropagateIterations`; zero when no eligible lights exist.
// The host uses it as the propagate iteration count (all lights share the
// max-radius falloff curve — per-light falloff waits on the winning-light
// ID channel).
//
// Intended trade: because the falloff curve is shared across all eligible
// lights, a single wide-radius light stretches every other light's decay
// even when it never seeds. This is deliberate for multi-light scenes with
// wide radius variance — the alternative (deriving stepFalloff from just the
// seeded subset) would make a light's curve shift as another light crosses
// the camera window boundary. Per-light falloff lands with the winning-light
// ID channel (#2318, L2).
// `outHasSpot` is set true when at least one SPOT light is actually seeded this
// frame; it gates the consumer's winning-light-ID read so no-spot scenes stay
// byte-identical (#2318).
inline std::uint32_t gatherLightSources(
    std::vector<GPULightSource> &out,
    IREntity::EntityId currentCanvas,
    const ivec3 &volumeOriginVoxel,
    int &outMaxRadius,
    std::uint32_t &outEligible,
    bool &outHasSpot,
    std::vector<LightGatherRecord> *outStates = nullptr
) {
    out.clear();
    outMaxRadius = 0;
    outEligible = 0;
    outHasSpot = false;
    if (outStates != nullptr) {
        outStates->clear();
    }
    const auto include = IREntity::getArchetype<C_LightSource, C_WorldTransform>();
    const auto nodes = IREntity::queryArchetypeNodesSimple(include);
    auto &entityManager = IREntity::getEntityManager();
    const auto nodeInScope = [&](const auto *node) {
        // Per-canvas scope: skip the entire node when its lights are
        // CHILD_OF a different canvas. `kNullEntity` means the lights
        // are world-scope (no parent) and apply to every canvas — the
        // back-compat default for demos that never call setParent on
        // their light entities.
        const IREntity::EntityId nodeParent =
            entityManager.getParentEntityFromArchetype(node->type_);
        return nodeParent == IREntity::kNullEntity || nodeParent == currentCanvas;
    };
    for (auto *node : nodes) {
        if (!nodeInScope(node)) {
            continue;
        }
        auto &lights = IREntity::getComponentData<C_LightSource>(node);
        for (int i = 0; i < node->length_; ++i) {
            // Directional lights drive sun shading via the FrameDataSun
            // path; they do not seed the world-space light volume.
            if (lights[i].type_ == LightType::DIRECTIONAL) {
                continue;
            }
            ++outEligible;
            const int r = static_cast<int>(lights[i].radius_);
            if (r > outMaxRadius) {
                outMaxRadius = r;
            }
        }
    }
    if (outMaxRadius > kLightVolumePropagateIterations) {
        outMaxRadius = kLightVolumePropagateIterations;
    }
    if (outEligible == 0) {
        return 0;
    }
    const float stepFalloff =
        1.0f /
        static_cast<float>(outMaxRadius > 0 ? outMaxRadius : kLightVolumePropagateIterations);
    for (auto *node : nodes) {
        if (!nodeInScope(node)) {
            continue;
        }
        auto &lights = IREntity::getComponentData<C_LightSource>(node);
        auto &transforms = IREntity::getComponentData<C_WorldTransform>(node);
        for (int i = 0; i < node->length_; ++i) {
            if (lights[i].type_ == LightType::DIRECTIONAL) {
                continue;
            }
            if (out.size() >= kLightVolumeMaxSources) {
                // Cap reached: lights past kLightVolumeMaxSources are neither
                // seeded nor recorded in `outStates`, so the DOMAIN-STATE log
                // under-reports the true non-directional light count in an
                // overflow scene. Intentional — `lightGatherRecords_` is
                // reserved to exactly kLightVolumeMaxSources at create(), so
                // recording the tail would force a per-frame reallocation; this
                // mirrors the GPU-staging cap the seed buffer already enforces.
                // A future #2317 verify harness asserting total light counts
                // against the log must account for this ceiling.
                return static_cast<std::uint32_t>(out.size());
            }
            const ivec3 origin = roundedLightOrigin(transforms[i]);
            const ivec3 rel = origin - volumeOriginVoxel;
            // The seedable window is rel ∈ [−halfExtent, halfExtent−1] per
            // axis (texel index = rel + halfExtent ∈ [0, gridSize)).
            ivec3 clamped = rel;
            int boundaryDist = 0;
            for (int axis = 0; axis < 3; ++axis) {
                const int c =
                    IRMath::clamp(rel[axis], -kLightVolumeHalfExtent, kLightVolumeHalfExtent - 1);
                boundaryDist += IRMath::abs(rel[axis] - c);
                clamped[axis] = c;
            }
            const float seedAlpha = 1.0f - static_cast<float>(boundaryDist) * stepFalloff;
            if (seedAlpha <= 0.0f) {
                if (outStates != nullptr) {
                    outStates->push_back({node->entities_[i], LightGatherState::SKIPPED, 0.0f});
                }
                continue;
            }
            // This light is actually seeded — flag SPOTs so the consumer only
            // pays the winning-light-ID read when a cone can exist (#2318).
            if (lights[i].type_ == LightType::SPOT) {
                outHasSpot = true;
            }
            // Known residual (#2330, lighting epic #1717): the clamped
            // boundary cell is NOT tested against occlusion. If the clamp
            // lands inside solid geometry (an occupied voxel or an SDF
            // C_LightBlocker), the seed is still emitted here, but
            // `c_propagate_light_volume`'s symmetric occlusion gate traps
            // its alpha at that cell, so the light silently drops instead of
            // fading. Benign — a missing light, never light-through-wall —
            // and fundamental to the finite window. An occlusion-aware seed
            // (queryable against the CPU mirror BUILD_LIGHT_OCCLUSION_GRID
            // already maintains, which runs before this system) is deferred
            // to #2330 rather than shipped silently.
            if (outStates != nullptr) {
                const LightGatherState state = boundaryDist == 0
                                                   ? LightGatherState::SEEDED_FULL
                                                   : LightGatherState::BOUNDARY_DISCOUNTED;
                outStates->push_back({node->entities_[i], state, seedAlpha});
            }
            out.push_back(toGpuLight(lights[i], volumeOriginVoxel + clamped, origin, seedAlpha));
        }
    }
    return static_cast<std::uint32_t>(out.size());
}

} // namespace detail

// `LightVolumeParams` defaults must mirror the volume's CPU constants —
// the system only writes `lightCount_` per frame, so `gridSize_` /
// `halfExtent_` flow to the GPU straight from the struct's defaults.
// `system_compute_light_volume.hpp` is the only place that includes
// both headers, so the drift check belongs here.
static_assert(
    LightVolumeParams{}.gridSize_ == kLightVolumeSize,
    "LightVolumeParams::gridSize_ default must equal kLightVolumeSize"
);
static_assert(
    LightVolumeParams{}.halfExtent_ == kLightVolumeHalfExtent,
    "LightVolumeParams::halfExtent_ default must equal kLightVolumeHalfExtent"
);

template <> struct System<COMPUTE_LIGHT_VOLUME> {
    ShaderProgram *clearProgram_ = nullptr;
    ShaderProgram *seedProgram_ = nullptr;
    ShaderProgram *propagateProgram_ = nullptr;
    Buffer *lightSourceBuf_ = nullptr;
    Buffer *paramsBuf_ = nullptr;
    Buffer *occlusionBuf_ = nullptr;
    std::vector<GPULightSource> lightStaging_{};
    // Per-light gather outcome (#2315, V1) — reused every frame, read back
    // via `lightGatherRecords()` below by a lighting demo's DOMAIN-STATE
    // emission hook (`AutoScreenshotConfig::onCaptureFrame_`) for the
    // per-shot machine-readable log line.
    std::vector<LightGatherRecord> lightGatherRecords_{};
    LightVolumeParams params_{};
    // Adaptive iteration count for the per-frame propagate dilation
    // chain. Recomputed at upload time from the eligible lights' max
    // radius. Pre-initialized to the global cap so the propagate
    // budget defaults to today's behavior if a frame ever dispatches
    // the loop without first running the upload phase.
    int propagateIterations_ = kLightVolumePropagateIterations;

    void tick(
        IREntity::EntityId canvasEntity,
        C_CanvasLightVolume &volume,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        if (!behavior.useCameraPositionIso_)
            return;
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
        auto &phaseTiming = IRRender::computeLightVolumeTiming();

        constexpr int kClearGroupSize = 8;
        constexpr int kPropagateGroupX = 8;
        constexpr int kPropagateGroupY = 8;
        constexpr int kPropagateGroupZ = 4;
        constexpr int kSeedGroupSize = 64;
        constexpr int kVolumeSize = kLightVolumeSize;

        // Phase: gather + upload light SSBO.
        {
            detail::ScopedCpuPhaseTimer timer{phaseTiming.upload_};
            IR_PROFILE_BLOCK("ComputeLightVolume::Upload", IR_PROFILER_COLOR_RENDER);
            // Phase 1c (#360): re-anchor the volume on the
            // iso camera each frame; the seed/propagate/
            // lighting shaders subtract this origin before
            // indexing, so a panned camera keeps lights in
            // range without resizing the texture. #2315 V1:
            // freeze-aware — pins at the cull-freeze transition
            // so F10 / shot-table FREEZE keeps lighting from the
            // pinned window instead of tracking a free-flying
            // camera.
            const ivec3 volumeOrigin = IRRender::detail::frozenAwareCameraAnchorVoxel();
            volume.setWorldOriginVoxel(volumeOrigin);
            int maxRadius = 0;
            std::uint32_t eligible = 0;
            bool hasSpot = false;
            const std::uint32_t count = detail::gatherLightSources(
                lightStaging_,
                canvasEntity,
                volumeOrigin,
                maxRadius,
                eligible,
                hasSpot,
                &lightGatherRecords_
            );
            // worldOriginVoxel_.w carries the has-SPOT flag (#2318): the
            // consumer skips the winning-light-ID read entirely when 0, so
            // no-spot scenes stay byte-identical.
            params_.worldOriginVoxel_ =
                ivec4(volumeOrigin.x, volumeOrigin.y, volumeOrigin.z, hasSpot ? 1 : 0);
            params_.lightCount_ = static_cast<int>(count);
            {
                auto &timing = IRRender::gpuStageTiming();
                timing.lightsSeeded_ = count;
                timing.lightsEligible_ = eligible;
            }
            // Adaptive iter count: lights of radius R only need R Manhattan
            // steps to fade to alpha=0. The global stepFalloff must match
            // the iter count so alpha lands cleanly at 0 on the final step —
            // pick both from the ELIGIBLE lights' max radius (stable under
            // camera motion; the gather derives boundary seed alphas from the
            // same value). Falls back to the global cap when no lights are
            // present (the dispatch loop below early-outs on lightCount==0
            // anyway, so the value is moot in that path).
            propagateIterations_ = (maxRadius > 0) ? maxRadius : kLightVolumePropagateIterations;
            params_.stepFalloff_ = 1.0f / static_cast<float>(propagateIterations_);
            if (count > 0) {
                lightSourceBuf_->subData(0, sizeof(GPULightSource) * count, lightStaging_.data());
            }
            paramsBuf_->subData(0, sizeof(LightVolumeParams), &params_);
        }

        // Phase: dispatch the GPU clear + seed + propagate
        // chain. The Clear bucket is reserved for the clear
        // pass alone; Populate covers seed + N propagate
        // iterations so the legacy column name still maps
        // to "where the bulk of the work lives".
        paramsBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_LightVolumeParams);

        {
            detail::ScopedCpuPhaseTimer timer{phaseTiming.clear_};
            IR_PROFILE_BLOCK("ComputeLightVolume::Clear", IR_PROFILER_COLOR_RENDER);
            clearProgram_->use();
            volume.getReadTexture()
                ->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
            // Winning-light ID read texture cleared in lockstep (#2318).
            volume.getIdReadTexture()
                ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
            const int clearGroups = IRMath::divCeil(kVolumeSize, kClearGroupSize);
            IRRender::device()->dispatchCompute(clearGroups, clearGroups, clearGroups);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        }

        {
            detail::ScopedCpuPhaseTimer timer{phaseTiming.populate_};
            IR_PROFILE_BLOCK("ComputeLightVolume::Populate", IR_PROFILER_COLOR_RENDER);

            // Seed pass: one thread per light, writes one
            // bright texel into the read texture.
            if (params_.lightCount_ > 0) {
                seedProgram_->use();
                lightSourceBuf_->bindBase(
                    BufferTarget::SHADER_STORAGE,
                    kBufferIndex_LightSourceBuffer
                );
                volume.getReadTexture()
                    ->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
                // Seed writes the winning-light ID (index+1) alongside the
                // color at each light's origin cell (#2318).
                volume.getIdReadTexture()
                    ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
                const int seedGroups = IRMath::divCeil(params_.lightCount_, kSeedGroupSize);
                IRRender::device()->dispatchCompute(static_cast<std::uint32_t>(seedGroups), 1u, 1u);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            }

            // Propagate dilation chain. Skipped entirely when no lights
            // gathered for this canvas — the cleared read texture would
            // propagate zeros and contribute nothing to LIGHTING_TO_TRIXEL.
            // Iteration count is the adaptive value derived from the
            // gathered lights' max radius (set above), bounded by the
            // global `kLightVolumePropagateIterations` cap.
            if (params_.lightCount_ > 0) {
                propagateProgram_->use();
                // Light-occlusion SSBO slot 28 also aliases
                // the sun-shadow depth map — rebind for this
                // pipeline (Metal compute encoders don't
                // preserve bindings across program switches).
                // The SSBO's first 16 bytes are a header
                // carrying the camera-anchored
                // worldOriginVoxel — the propagate shader
                // reads it directly.
                if (occlusionBuf_ != nullptr) {
                    occlusionBuf_->bindBase(
                        BufferTarget::SHADER_STORAGE,
                        kBufferIndex_LightOcclusionGrid
                    );
                }
                const int gx = IRMath::divCeil(kVolumeSize, kPropagateGroupX);
                const int gy = IRMath::divCeil(kVolumeSize, kPropagateGroupY);
                const int gz = IRMath::divCeil(kVolumeSize, kPropagateGroupZ);
                for (int iter = 0; iter < propagateIterations_; ++iter) {
                    volume.getReadTexture()
                        ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
                    volume.getWriteTexture()
                        ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
                    // Winning-light ID ping-pong, swapped in lockstep with the
                    // color pair by volume.swap() below (#2318).
                    volume.getIdReadTexture()
                        ->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
                    volume.getIdWriteTexture()
                        ->bindAsImage(3, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
                    IRRender::device()->dispatchCompute(
                        static_cast<std::uint32_t>(gx),
                        static_cast<std::uint32_t>(gy),
                        static_cast<std::uint32_t>(gz)
                    );
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
                    volume.swap();
                }
            }
        }
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ClearLightVolumeProgram",
            std::vector{ShaderStage{IRRender::kFileCompClearLightVolume, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "SeedLightVolumeProgram",
            std::vector{ShaderStage{IRRender::kFileCompSeedLightVolume, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "PropagateLightVolumeProgram",
            std::vector{ShaderStage{IRRender::kFileCompPropagateLightVolume, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "LightSourceBuffer",
            nullptr,
            sizeof(GPULightSource) * kLightVolumeMaxSources,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_LightSourceBuffer
        );
        IRRender::createNamedResource<Buffer>(
            "LightVolumeParamsBuffer",
            nullptr,
            sizeof(LightVolumeParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_LightVolumeParams
        );

        SystemId systemId =
            registerSystem<COMPUTE_LIGHT_VOLUME, C_CanvasLightVolume, C_TrixelCanvasRenderBehavior>(
                "ComputeLightVolume"
            );
        auto *p = getSystemParams<System<COMPUTE_LIGHT_VOLUME>>(systemId);
        p->clearProgram_ = IRRender::getNamedResource<ShaderProgram>("ClearLightVolumeProgram");
        p->seedProgram_ = IRRender::getNamedResource<ShaderProgram>("SeedLightVolumeProgram");
        p->propagateProgram_ =
            IRRender::getNamedResource<ShaderProgram>("PropagateLightVolumeProgram");
        p->lightSourceBuf_ = IRRender::getNamedResource<Buffer>("LightSourceBuffer");
        p->paramsBuf_ = IRRender::getNamedResource<Buffer>("LightVolumeParamsBuffer");
        // LightOcclusionGridBuffer is created by
        // BUILD_LIGHT_OCCLUSION_GRID, which is registered ahead of this
        // system in every creation that uses either path; safe to look
        // up at init time. Phase 1c (#360): the SSBO carries a 16-byte
        // header (worldOriginVoxel) followed by the voxel + blocker
        // bitfields, so the propagate shader reads the camera-anchored
        // origin from the same binding.
        p->occlusionBuf_ = IRRender::getNamedResource<Buffer>("LightOcclusionGridBuffer");
        p->lightStaging_.reserve(kLightVolumeMaxSources);
        p->lightGatherRecords_.reserve(kLightVolumeMaxSources);
        IRRender::tagGpuStage(systemId, "computeLightVolume");
        return systemId;
    }
};

// Read-back accessor for the DOMAIN-STATE emission hook (#2315, V1) — a
// lighting demo holds the `SystemId` returned by
// `createSystem<COMPUTE_LIGHT_VOLUME>()` and calls this from its
// `AutoScreenshotConfig::onCaptureFrame_` callback to format the per-shot
// per-light state list. Mirrors the sanctioned `getSystemParams<System<N>>`
// diagnostics read-back pattern (engine/system/CLAUDE.md).
inline const std::vector<LightGatherRecord> &lightGatherRecords(SystemId computeLightVolumeSystem) {
    return getSystemParams<System<COMPUTE_LIGHT_VOLUME>>(computeLightVolumeSystem)
        ->lightGatherRecords_;
}

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_LIGHT_VOLUME_H */
