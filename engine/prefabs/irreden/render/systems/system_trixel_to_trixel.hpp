#ifndef SYSTEM_TRIXEL_TO_TRIXEL_H
#define SYSTEM_TRIXEL_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

namespace IRSystem {

constexpr int kTrixelToTrixelGroupSize = 16; // must match local_size in c_trixel_to_trixel.glsl

template <> struct System<TRIXEL_TO_TRIXEL> {
    struct Params {
        ShaderProgram *program_ = nullptr;
        Buffer *frameDataBuf_ = nullptr;
        FrameDataTrixelToTrixel frameData_{};
    };

    static SystemId create() {
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

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->program_ = IRRender::getNamedResource<ShaderProgram>("TrixelToTrixelProgram");
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("TrixelToTrixelFrameData");

        SystemId systemId = createSystem<C_TriangleCanvasTextures, C_Position2DIso>(
            "CanvasToFramebuffer",
            [p](const C_TriangleCanvasTextures &trixelTextures,
                const C_Position2DIso &position2DIso) {
                trixelTextures.bind(2, 3);
                p->frameData_.trixelTextureOffsetZ1_ =
                    IRMath::trixelOriginOffsetZ1(trixelTextures.size_);
                p->frameData_.texturePos2DIso_ = position2DIso.pos_;
                p->frameDataBuf_->subData(0, sizeof(FrameDataTrixelToTrixel), &p->frameData_);
                const int groupsX = IRMath::divCeil(trixelTextures.size_.x, kTrixelToTrixelGroupSize);
                const int groupsY = IRMath::divCeil(trixelTextures.size_.y, kTrixelToTrixelGroupSize);
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            },
            [p]() {
                p->program_->use();
                vec2 camIso = IRRender::getCameraPosition2DIso();
                p->frameData_.cameraTrixelOffset_ = ivec2(
                    static_cast<int>(IRMath::floor(camIso.x)),
                    static_cast<int>(IRMath::floor(camIso.y))
                );
            },
            nullptr,
            // TODO: Add position here and bind camera position to
            // main trixel canvas.
            RelationParams<C_TriangleCanvasTextures>{Relation::CHILD_OF},
            [p](const C_TriangleCanvasTextures &parentTexture) {
                parentTexture.bind(0, 1);
                p->frameData_.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(parentTexture.size_);
            }

        );

        setSystemParams(systemId, std::move(paramsOwner));
        IRRender::tagGpuStage(systemId, "trixelToTrixel");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TRIXEL_TO_TRIXEL_H */
