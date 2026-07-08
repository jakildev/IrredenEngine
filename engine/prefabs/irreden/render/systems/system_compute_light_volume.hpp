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
// upload (~16 KiB at the 256-light cap). Per-light radius variation,
// per-light cone shaping (SPOT), and analytic point/spot LOS are
// pending for later phases.
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

inline GPULightSource
toGpuLight(const C_LightSource &light, const ivec3 &originVoxel, float seedAlpha) {
    GPULightSource gpu{};
    gpu.originAndType_ = vec4(
        static_cast<float>(originVoxel.x),
        static_cast<float>(originVoxel.y),
        static_cast<float>(originVoxel.z),
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
inline std::uint32_t gatherLightSources(
    std::vector<GPULightSource> &out,
    IREntity::EntityId currentCanvas,
    const ivec3 &volumeOriginVoxel,
    int &outMaxRadius,
    std::uint32_t &outEligible
) {
    out.clear();
    outMaxRadius = 0;
    outEligible = 0;
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
                continue;
            }
            out.push_back(toGpuLight(lights[i], volumeOriginVoxel + clamped, seedAlpha));
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
            // range without resizing the texture.
            const ivec3 volumeOrigin = IRRender::detail::cameraAnchorVoxel();
            volume.setWorldOriginVoxel(volumeOrigin);
            params_.worldOriginVoxel_ = ivec4(volumeOrigin.x, volumeOrigin.y, volumeOrigin.z, 0);
            int maxRadius = 0;
            std::uint32_t eligible = 0;
            const std::uint32_t count = detail::gatherLightSources(
                lightStaging_,
                canvasEntity,
                volumeOrigin,
                maxRadius,
                eligible
            );
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
        IRRender::tagGpuStage(systemId, "computeLightVolume");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_LIGHT_VOLUME_H */
