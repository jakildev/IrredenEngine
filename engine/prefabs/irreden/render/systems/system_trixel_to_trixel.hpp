#ifndef SYSTEM_TRIXEL_TO_TRIXEL_H
#define SYSTEM_TRIXEL_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

namespace IRSystem {

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

        return createSystem<C_TriangleCanvasTextures, C_Position2DIso>(
            "CanvasToFramebuffer",
            [](const C_TriangleCanvasTextures &trixelTextures,
               const C_Position2DIso &position2DIso) {
                trixelTextures.bind(2, 3);
                frameData.trixelTextureOffsetZ1_ =
                    IRMath::trixelOriginOffsetZ1(trixelTextures.size_);
                frameData.texturePos2DIso_ = position2DIso.pos_;
                IRRender::getNamedResource<Buffer>("TrixelToTrixelFrameData")
                    ->subData(0, sizeof(FrameDataTrixelToTrixel), &frameData);
                const int groupsX = (trixelTextures.size_.x + 15) / 16;
                const int groupsY = (trixelTextures.size_.y + 15) / 16;
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            },
            []() {
                IRRender::getNamedResource<ShaderProgram>("TrixelToTrixelProgram")->use();
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
