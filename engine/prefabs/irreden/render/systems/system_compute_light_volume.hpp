#ifndef SYSTEM_COMPUTE_LIGHT_VOLUME_H
#define SYSTEM_COMPUTE_LIGHT_VOLUME_H

// GPU light propagation. For each `C_LightSource` entity in the world,
// uploads the light's world voxel origin + emissive color + intensity
// to a small SSBO, then dispatches three compute passes against the
// canvas's `C_CanvasLightVolume` ping-pong 3D textures:
//
//   1. `c_clear_light_volume` zeroes the read texture.
//   2. `c_seed_light_volume` writes one bright texel per light at its
//      world voxel origin: rgb = emit_color × intensity, alpha = 1.0
//      (full residual strength).
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
// Phase 1b (issue #362) addresses the volume-vs-occupancy extent
// mismatch by surfacing it: any light origin that rounds outside
// `[-kLightVolumeHalfExtent, +kLightVolumeHalfExtent)` is dropped on
// the CPU before the SSBO upload and logged once per distinct origin
// position (so the previous silent edge clamping at the volume
// boundary becomes a clear, actionable warning). Phase 1c (#360)
// replaces this with a camera-anchored window so most scenes keep
// every light in-range without growing the texture footprint.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/detail/camera_anchor.hpp>

#include <chrono>
#include <cstdint>
#include <unordered_set>
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

inline GPULightSource toGpuLight(const C_LightSource &light, const ivec3 &originVoxel) {
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
    gpu.coneAndPad_ = vec4(light.coneAngleDeg_, 0.0f, 0.0f, 0.0f);
    return gpu;
}

inline ivec3 roundedLightOrigin(const C_PositionGlobal3D &position) {
    // Round-half-up so light origins line up with the light-occlusion
    // grid cells populated by `system_build_light_occlusion_grid` (also
    // round-half-up).
    return IRMath::roundVec3HalfUp(position.pos_);
}

inline bool isOriginInLightVolume(const ivec3 &lightOrigin, const ivec3 &volumeOrigin) {
    constexpr int he = kLightVolumeHalfExtent;
    const ivec3 local = lightOrigin - volumeOrigin;
    return local.x >= -he && local.x < he && local.y >= -he && local.y < he && local.z >= -he &&
           local.z < he;
}

// Pack a signed-int voxel position into a single uint64 so the
// "already-warned" set can dedupe by exact origin without paying for a
// custom hash on `ivec3`. 21 bits per axis covers ±1M cells, well
// beyond any realistic scene; we add a bias to keep the value
// nonnegative before shifting.
inline std::uint64_t packOriginKey(const ivec3 &v) {
    constexpr int kBias = 1 << 20;
    constexpr std::uint64_t kMask = (1ull << 21) - 1ull;
    const auto x = static_cast<std::uint64_t>(v.x + kBias) & kMask;
    const auto y = static_cast<std::uint64_t>(v.y + kBias) & kMask;
    const auto z = static_cast<std::uint64_t>(v.z + kBias) & kMask;
    return (x << 42) | (y << 21) | z;
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
// Lights whose origin rounds outside the volume's camera-anchored window
// (`worldOriginVoxel ± kLightVolumeHalfExtent`) are skipped (the seed
// shader's bounds check would silently drop them anyway) and logged once
// per unique origin coordinate via `warnedOOBOrigins`. This makes the
// Phase 1b (#362) silent-clamping issue visible without spamming when
// the same misplaced static light is processed every frame; Phase 1c
// (#360) shrinks the warning surface by panning the window with the
// camera so most scenes stay in-range without manual intervention.
inline std::uint32_t gatherLightSources(
    std::vector<GPULightSource> &out,
    IREntity::EntityId currentCanvas,
    const ivec3 &volumeOriginVoxel,
    std::unordered_set<std::uint64_t> &warnedOOBOrigins
) {
    out.clear();
    const auto include = IREntity::getArchetype<C_LightSource, C_PositionGlobal3D>();
    const auto nodes = IREntity::queryArchetypeNodesSimple(include);
    auto &entityManager = IREntity::getEntityManager();
    for (auto *node : nodes) {
        // Per-canvas scope: skip the entire node when its lights are
        // CHILD_OF a different canvas. `kNullEntity` means the lights
        // are world-scope (no parent) and apply to every canvas — the
        // back-compat default for demos that never call setParent on
        // their light entities.
        const IREntity::EntityId nodeParent =
            entityManager.getParentEntityFromArchetype(node->type_);
        if (nodeParent != IREntity::kNullEntity && nodeParent != currentCanvas) {
            continue;
        }
        auto &lights = IREntity::getComponentData<C_LightSource>(node);
        auto &positions = IREntity::getComponentData<C_PositionGlobal3D>(node);
        for (int i = 0; i < node->length_; ++i) {
            // Directional lights drive sun shading via the FrameDataSun
            // path; they do not seed the world-space light volume.
            if (lights[i].type_ == LightType::DIRECTIONAL) {
                continue;
            }
            if (out.size() >= kLightVolumeMaxSources) {
                return static_cast<std::uint32_t>(out.size());
            }
            const ivec3 origin = roundedLightOrigin(positions[i]);
            if (!isOriginInLightVolume(origin, volumeOriginVoxel)) {
                if (warnedOOBOrigins.insert(packOriginKey(origin)).second) {
                    IR_LOG_WARN(
                        "C_LightSource at world voxel ({}, {}, {}) is "
                        "outside the camera-anchored light volume "
                        "centered on ({}, {}, {}) with half-extent {}; "
                        "dropping. Pan the camera nearer to the light "
                        "or move the light into the visible region.",
                        origin.x,
                        origin.y,
                        origin.z,
                        volumeOriginVoxel.x,
                        volumeOriginVoxel.y,
                        volumeOriginVoxel.z,
                        kLightVolumeHalfExtent
                    );
                }
                continue;
            }
            out.push_back(toGpuLight(lights[i], origin));
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
    struct Params {
        ShaderProgram *clearProgram_ = nullptr;
        ShaderProgram *seedProgram_ = nullptr;
        ShaderProgram *propagateProgram_ = nullptr;
        Buffer *lightSourceBuf_ = nullptr;
        Buffer *paramsBuf_ = nullptr;
        Buffer *occlusionBuf_ = nullptr;
        std::vector<GPULightSource> lightStaging_{};
        LightVolumeParams params_{};
        // Set of out-of-bounds light origins we have already warned
        // about — prevents the per-frame gather from spamming a stable
        // misplaced light. Keyed via `detail::packOriginKey`.
        std::unordered_set<std::uint64_t> warnedOOBOrigins_{};
    };

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

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
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

        SystemId systemId =
            createSystem<C_CanvasLightVolume, C_TrixelCanvasRenderBehavior>(
                "ComputeLightVolume",
                [p](const IREntity::EntityId canvasEntity,
                    C_CanvasLightVolume &volume,
                    const C_TrixelCanvasRenderBehavior &behavior) {
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
                        p->params_.worldOriginVoxel_ =
                            ivec4(volumeOrigin.x, volumeOrigin.y, volumeOrigin.z, 0);
                        const std::uint32_t count = detail::gatherLightSources(
                            p->lightStaging_,
                            canvasEntity,
                            volumeOrigin,
                            p->warnedOOBOrigins_
                        );
                        p->params_.lightCount_ = static_cast<int>(count);
                        if (count > 0) {
                            p->lightSourceBuf_->subData(
                                0,
                                sizeof(GPULightSource) * count,
                                p->lightStaging_.data()
                            );
                        }
                        p->paramsBuf_->subData(0, sizeof(LightVolumeParams), &p->params_);
                    }

                    // Phase: dispatch the GPU clear + seed + propagate
                    // chain. The Clear bucket is reserved for the clear
                    // pass alone; Populate covers seed + N propagate
                    // iterations so the legacy column name still maps
                    // to "where the bulk of the work lives".
                    p->paramsBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_LightVolumeParams);

                    {
                        detail::ScopedCpuPhaseTimer timer{phaseTiming.clear_};
                        IR_PROFILE_BLOCK("ComputeLightVolume::Clear", IR_PROFILER_COLOR_RENDER);
                        p->clearProgram_->use();
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
                        if (p->params_.lightCount_ > 0) {
                            p->seedProgram_->use();
                            p->lightSourceBuf_->bindBase(
                                BufferTarget::SHADER_STORAGE,
                                kBufferIndex_LightSourceBuffer
                            );
                            volume.getReadTexture()
                                ->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
                            const int seedGroups =
                                IRMath::divCeil(p->params_.lightCount_, kSeedGroupSize);
                            IRRender::device()
                                ->dispatchCompute(static_cast<std::uint32_t>(seedGroups), 1u, 1u);
                            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
                        }

                        // Propagate dilation chain. With no lights the
                        // chain still runs but every iteration reads
                        // zeros so the cost is bounded; we elide it for
                        // a tighter early-out.
                        if (p->params_.lightCount_ > 0) {
                            p->propagateProgram_->use();
                            // Light-occlusion SSBO slot 28 also aliases
                            // the sun-shadow depth map — rebind for this
                            // pipeline (Metal compute encoders don't
                            // preserve bindings across program switches).
                            // The SSBO's first 16 bytes are a header
                            // carrying the camera-anchored
                            // worldOriginVoxel — the propagate shader
                            // reads it directly.
                            if (p->occlusionBuf_ != nullptr) {
                                p->occlusionBuf_->bindBase(
                                    BufferTarget::SHADER_STORAGE,
                                    kBufferIndex_LightOcclusionGrid
                                );
                            }
                            const int gx = IRMath::divCeil(kVolumeSize, kPropagateGroupX);
                            const int gy = IRMath::divCeil(kVolumeSize, kPropagateGroupY);
                            const int gz = IRMath::divCeil(kVolumeSize, kPropagateGroupZ);
                            for (int iter = 0; iter < kLightVolumePropagateIterations; ++iter) {
                                volume.getReadTexture()->bindAsImage(
                                    0,
                                    TextureAccess::READ_ONLY,
                                    TextureFormat::RGBA8
                                );
                                volume.getWriteTexture()->bindAsImage(
                                    1,
                                    TextureAccess::WRITE_ONLY,
                                    TextureFormat::RGBA8
                                );
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
            );
        IRRender::tagGpuStage(systemId, "computeLightVolume");
        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_LIGHT_VOLUME_H */
