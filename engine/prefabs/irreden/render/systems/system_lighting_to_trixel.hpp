#ifndef SYSTEM_LIGHTING_TO_TRIXEL_H
#define SYSTEM_LIGHTING_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Must match local_size in c_lighting_to_trixel.glsl / .metal.
constexpr int kLightingToTrixelGroupSize = 16;

// Binding slot for the lighting frame-data UBO. Kept local to this prefab
// since the engine-wide binding table (ir_render_types.hpp) does not yet
// reserve one for lighting — slot 27 is currently unused.
constexpr std::uint32_t kBufferIndex_FrameDataLightingToTrixel = 27;

// CPU-side mirror of the GLSL/MSL `FrameDataLightingToTrixel` UBO.
struct FrameDataLightingToTrixel {
    int lightingEnabled_ = 0;
    int padding0_ = 0;
    int padding1_ = 0;
    int padding2_ = 0;
};

// Screen-space lighting application pass. Inserts between the final
// geometry stage and the compositing stage; reads the canvas distance
// texture plus per-pixel lighting inputs (currently AO via
// C_CanvasAOTexture) and modulates canvas color in place.
//
// GUI pixels are left untouched: canvases with
// `C_TrixelCanvasRenderBehavior::useCameraPositionIso_ == false` early-
// return in the tick.
template <> struct System<LIGHTING_TO_TRIXEL> {
    static SystemId create() {
        static FrameDataLightingToTrixel frameData{};

        IRRender::createNamedResource<ShaderProgram>(
            "LightingToTrixelProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompLightingToTrixel, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "LightingToTrixelFrameData",
            nullptr,
            sizeof(FrameDataLightingToTrixel),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataLightingToTrixel
        );

        static ShaderProgram *s_program =
            IRRender::getNamedResource<ShaderProgram>("LightingToTrixelProgram");
        static Buffer *s_frameDataBuf =
            IRRender::getNamedResource<Buffer>("LightingToTrixelFrameData");

        return createSystem<
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior,
            C_CanvasAOTexture
        >(
            "LightingToTrixel",
            [](const C_TriangleCanvasTextures &canvasTextures,
               const C_TrixelCanvasRenderBehavior &behavior,
               const C_CanvasAOTexture &ao) {
                if (!behavior.useCameraPositionIso_) {
                    return;
                }

                canvasTextures.getTextureColors()->bindAsImage(
                    0, TextureAccess::READ_WRITE, TextureFormat::RGBA8
                );
                canvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                ao.getTexture()->bindAsImage(
                    2, TextureAccess::READ_ONLY, TextureFormat::RGBA8
                );
                s_frameDataBuf->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataLightingToTrixel
                );

                const int groupsX = IRMath::divCeil(
                    canvasTextures.size_.x, kLightingToTrixelGroupSize
                );
                const int groupsY = IRMath::divCeil(
                    canvasTextures.size_.y, kLightingToTrixelGroupSize
                );
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            },
            []() {
                s_program->use();
                frameData.lightingEnabled_ = 1;
                s_frameDataBuf->subData(
                    0, sizeof(FrameDataLightingToTrixel), &frameData
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_LIGHTING_TO_TRIXEL_H */
