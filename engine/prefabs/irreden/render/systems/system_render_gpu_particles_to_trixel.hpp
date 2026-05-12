#ifndef SYSTEM_RENDER_GPU_PARTICLES_TO_TRIXEL_H
#define SYSTEM_RENDER_GPU_PARTICLES_TO_TRIXEL_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/components/component_gpu_particle_pool.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <memory>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

/// T-139 Phase 1 — GPU particle → trixel canvas render pass.
///
/// Per matched canvas (must own both `C_GPUParticlePool` and
/// `C_TriangleCanvasTextures`):
///   1. Refresh per-frame data (camera offset, canvas size).
///   2. Bind particle SSBO + UBO + canvas color/distance images.
///   3. Dispatch the render kernel; one thread per particle slot writes a
///      single trixel via `imageAtomicMin` on the distance texture and a
///      conditional color write.
///
/// Pipeline placement: belongs in the RENDER pipeline AFTER the voxel and
/// shape passes have established their depth taps, and AFTER lighting (so
/// the particles render emissively without participating in AO/shadow). The
/// recommended slot is between `LIGHTING_TO_TRIXEL` and `FOG_TO_TRIXEL` —
/// late enough that the canvas color buffer holds final lit voxels, early
/// enough that fog still masks particles like everything else.
template <> struct System<RENDER_GPU_PARTICLES_TO_TRIXEL> {
    struct Params {
        ShaderProgram *renderProgram_ = nullptr;
        Buffer *frameDataBuf_ = nullptr;
        FrameDataGpuParticles frameData_{};
    };

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "GpuParticleRenderProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompRenderGpuParticlesToTrixel, ShaderType::COMPUTE}
            }
        );

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->renderProgram_ = IRRender::getNamedResource<ShaderProgram>("GpuParticleRenderProgram");
        // Reuse the FrameDataGpuParticles UBO created by the update system.
        // Both systems share one UBO so a single CPU upload feeds both passes
        // (the update system populates it earlier in the same frame).
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("GpuParticleFrameData");

        SystemId systemId = createSystem<C_GPUParticlePool, C_TriangleCanvasTextures>(
            "RenderGpuParticlesToTrixel",
            [p](C_GPUParticlePool &pool, C_TriangleCanvasTextures &canvas) {
                if (pool.capacity_ == 0u || pool.buffer_.second == nullptr) return;

                // Refresh render-pass fields every tick — camera and canvas
                // can both change. Update-pass fields (deltaTime / count)
                // were written by the update system earlier this frame.
                p->frameData_.cameraTrixelOffset_ = IRRender::getCameraPosition2DIso();
                p->frameData_.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(canvas.size_);
                p->frameData_.canvasSizePixels_ = canvas.size_;
                p->frameData_.particleCount_ = pool.capacity_;
                p->frameDataBuf_->subData(0, sizeof(FrameDataGpuParticles), &p->frameData_);

                p->renderProgram_->use();
                pool.buffer_.second->bindBase(
                    BufferTarget::SHADER_STORAGE, kBufferIndex_GpuParticleData
                );
                p->frameDataBuf_->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataGpuParticles
                );

                canvas.getTextureColors()->bindAsImage(
                    0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                );
                canvas.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_WRITE, TextureFormat::R32I
                );

                constexpr std::uint32_t kLocalSize = 64u;
                const std::uint32_t groups = (pool.capacity_ + kLocalSize - 1u) / kLocalSize;
                IRRender::device()->dispatchCompute(groups, 1u, 1u);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_RENDER_GPU_PARTICLES_TO_TRIXEL_H */
