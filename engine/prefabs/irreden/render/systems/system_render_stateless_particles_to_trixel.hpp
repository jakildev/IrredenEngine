#ifndef SYSTEM_RENDER_STATELESS_PARTICLES_TO_TRIXEL_H
#define SYSTEM_RENDER_STATELESS_PARTICLES_TO_TRIXEL_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/components/component_stateless_particle_emitters.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

/// T-163 Phase 1 — stateless particle → trixel canvas render pass.
///
/// Per matched canvas (must own both `C_StatelessParticleEmitters` and
/// `C_TriangleCanvasTextures`):
///   1. Refresh the per-frame UBO header (currentTime, emitterCount,
///      projection inputs) and upload via `subData`.
///   2. Bind UBO + emitter SSBO + canvas color/distance images.
///   3. Dispatch `emitterCount * kMaxParticlesPerEmitter` threads. Each
///      thread runs a closed-form gravity-with-jitter trajectory and writes
///      a single trixel via `imageAtomicMin`.
///
/// Pipeline placement: same slot as the T-139 SSBO render pass — between
/// `LIGHTING_TO_TRIXEL` and `FOG_TO_TRIXEL`. Both particle paths can
/// register on the same canvas; they composite correctly via
/// `imageAtomicMin` because the atomic ordering doesn't depend on which
/// kernel issued the write.
///
/// The emitter SSBO is owned and pushed at mutation time by
/// `C_StatelessParticleEmitters` (see `cpp-ecs.md` "No dirty flags"); this
/// system rebinds it for each dispatch but does not re-upload the
/// descriptor array per frame.
///
/// `currentTime_` is accumulated from `IRTime::deltaTime(RENDER)` each
/// frame so the closed-form steady-state stagger
/// (`spawnOffset = subIndex / spawnRate`) sweeps through every emitter's
/// lifetime as wall-clock advances.
template <> struct System<RENDER_STATELESS_PARTICLES_TO_TRIXEL> {
    ShaderProgram *program_ = nullptr;
    float currentTime_ = 0.0f;
    FrameDataStatelessParticles frameHeader_{};

    void beginTick() {
        currentTime_ += static_cast<float>(IRTime::deltaTime(IRTime::Events::RENDER));
        program_->use();
    }

    void tick(
        C_StatelessParticleEmitters &emitters,
        C_TriangleCanvasTextures &canvas
    ) {
        if (emitters.frameBuffer_.second == nullptr ||
            emitters.emitterBuffer_.second == nullptr) return;
        const std::uint32_t emitterCount = emitters.emitterCount();
        if (emitterCount == 0u) return;

        frameHeader_.currentTime_ = currentTime_;
        frameHeader_.emitterCount_ = emitterCount;
        frameHeader_.cameraTrixelOffset_ = IRRender::getCameraPosition2DIso();
        frameHeader_.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(canvas.size_);
        frameHeader_.canvasSizePixels_ = canvas.size_;
        // Mirror the voxel pipeline's (renderMode, effectiveSubdivisions) so
        // each particle expands into the same sub² × 6-trixel diamond a voxel
        // does at the current zoom — without this, particles stay at base
        // resolution while voxels refine, and the two read as different sizes
        // in the same frame.
        frameHeader_.voxelRenderOptions_ = ivec2(
            static_cast<int>(IRRender::getSubdivisionMode()),
            IRRender::getVoxelRenderEffectiveSubdivisions()
        );
        emitters.frameBuffer_.second
            ->subData(0, sizeof(FrameDataStatelessParticles), &frameHeader_);

        emitters.frameBuffer_.second->bindBase(
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataStatelessParticles
        );
        emitters.emitterBuffer_.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_StatelessParticleEmitters
        );

        canvas.getTextureColors()->bindAsImage(
            0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
        );
        canvas.getTextureDistances()->bindAsImage(
            1, TextureAccess::READ_WRITE, TextureFormat::R32I
        );

        constexpr std::uint32_t kLocalSize = 64u;
        const std::uint32_t threadCount = emitterCount * kMaxParticlesPerEmitter;
        const std::uint32_t groups = (threadCount + kLocalSize - 1u) / kLocalSize;
        IRRender::device()->dispatchCompute(groups, 1u, 1u);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "StatelessParticleRenderProgram",
            std::vector{ShaderStage{
                IRRender::kFileCompRenderStatelessParticlesToTrixel,
                ShaderType::COMPUTE
            }}
        );

        SystemId systemId = registerSystem<
            RENDER_STATELESS_PARTICLES_TO_TRIXEL,
            C_StatelessParticleEmitters,
            C_TriangleCanvasTextures>("RenderStatelessParticlesToTrixel");
        auto *p = getSystemParams<System<RENDER_STATELESS_PARTICLES_TO_TRIXEL>>(systemId);
        p->program_ =
            IRRender::getNamedResource<ShaderProgram>("StatelessParticleRenderProgram");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_RENDER_STATELESS_PARTICLES_TO_TRIXEL_H */
