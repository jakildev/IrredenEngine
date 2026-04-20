#ifndef SYSTEM_TRIXEL_TO_TRIXEL_H
#define SYSTEM_TRIXEL_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

namespace IRSystem {

constexpr int kTrixelToTrixelGroupSize = 16; // must match local_size in c_trixel_to_trixel.glsl

template <> struct System<TRIXEL_TO_TRIXEL> {
    static SystemId create() {
        static FrameDataTrixelToTrixel frameData{};
        IRRender::createNamedResource<ShaderProgram>(
            "TrixelToTrixelProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompTrixelToTrixel, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "TrixelToTrixelFrameData",
            nullptr,
            sizeof(FrameDataTrixelToTrixel),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataTrixelToTrixel
        );

        static ShaderProgram *s_program =
            IRRender::getNamedResource<ShaderProgram>("TrixelToTrixelProgram");
        static Buffer *s_frameDataBuf =
            IRRender::getNamedResource<Buffer>("TrixelToTrixelFrameData");

        return createSystem<C_TriangleCanvasTextures, C_Position2DIso>(
            "CanvasToFramebuffer",
            [](const C_TriangleCanvasTextures &trixelTextures,
               const C_Position2DIso &position2DIso) {
                auto &timing = IRRender::gpuStageTiming();
                IRRender::TimePoint t0;
                if (timing.enabled_) { IRRender::device()->finish(); t0 = IRRender::SteadyClock::now(); }

                trixelTextures.bind(2, 3);
                frameData.trixelTextureOffsetZ1_ =
                    IRMath::trixelOriginOffsetZ1(trixelTextures.size_);
                frameData.texturePos2DIso_ = position2DIso.pos_;
                s_frameDataBuf->subData(0, sizeof(FrameDataTrixelToTrixel), &frameData);
                const int groupsX = IRMath::divCeil(trixelTextures.size_.x, kTrixelToTrixelGroupSize);
                const int groupsY = IRMath::divCeil(trixelTextures.size_.y, kTrixelToTrixelGroupSize);
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                if (timing.enabled_) { IRRender::device()->finish(); timing.trixelToTrixelMs_ += IRRender::elapsedMs(t0, IRRender::SteadyClock::now()); }
            },
            []() {
                IRRender::gpuStageTiming().trixelToTrixelMs_ = 0.0f;
                s_program->use();
                vec2 camIso = IRRender::getCameraPosition2DIso();
                frameData.cameraTrixelOffset_ = ivec2(
                    static_cast<int>(IRMath::floor(camIso.x)),
                    static_cast<int>(IRMath::floor(camIso.y))
                );
            },
            nullptr,
            // TODO: Add position here and bind camera position to
            // main trixel canvas.
            RelationParams<C_TriangleCanvasTextures>{Relation::CHILD_OF},
            [](const C_TriangleCanvasTextures &parentTexture) {
                parentTexture.bind(0, 1);
                frameData.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(parentTexture.size_);
            }

        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TRIXEL_TO_TRIXEL_H */
