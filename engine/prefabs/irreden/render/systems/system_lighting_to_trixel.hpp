#ifndef SYSTEM_LIGHTING_TO_TRIXEL_H
#define SYSTEM_LIGHTING_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <array>
#include <cstdint>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>

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
// `lutEnabled_` activates palette LUT shading, which replaces the plain
// grayscale AO multiplication with a luminance-indexed palette lookup
// whose X-axis is driven by the per-pixel AO value.
// `debugLightLevel_` is reserved for future shadow-preview use; it is
// kept in the UBO for std140 layout stability but the shader currently
// uses AO.r as the LUT X-axis input.
struct FrameDataLightingToTrixel {
    int   lightingEnabled_  = 0;
    int   lutEnabled_       = 0;
    float debugLightLevel_  = 0.0f;
    int   padding2_         = 0;  // std140 alignment
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
        IRRender::createNamedResource<Texture2D>(
            "PaletteLUT_Nearest",
            TextureKind::TEXTURE_2D, 256, 16,
            TextureFormat::RGBA8, TextureWrap::CLAMP_TO_EDGE, TextureFilter::NEAREST
        );
        IRRender::createNamedResource<Texture2D>(
            "PaletteLUT_Linear",
            TextureKind::TEXTURE_2D, 256, 16,
            TextureFormat::RGBA8, TextureWrap::CLAMP_TO_EDGE, TextureFilter::LINEAR
        );

        static ShaderProgram *s_program =
            IRRender::getNamedResource<ShaderProgram>("LightingToTrixelProgram");
        static Buffer *s_frameDataBuf =
            IRRender::getNamedResource<Buffer>("LightingToTrixelFrameData");
        static Texture2D *s_paletteLUT =
            IRRender::getNamedResource<Texture2D>("PaletteLUT_Nearest");
        // Upload default LUT: cool-shadow (x=0) → full-white (x=255) gradient.
        // Same data for both filter variants; the difference is sampling mode.
        {
            std::array<std::uint8_t, 256 * 16 * 4> data{};
            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 256; ++col) {
                    const float t = static_cast<float>(col) / 255.0f;
                    const std::size_t idx = static_cast<std::size_t>(row * 256 + col) * 4;
                    data[idx + 0] = static_cast<std::uint8_t>((0.15f + 0.85f * t) * 255.0f);
                    data[idx + 1] = static_cast<std::uint8_t>((0.20f + 0.80f * t) * 255.0f);
                    data[idx + 2] = static_cast<std::uint8_t>((0.35f + 0.65f * t) * 255.0f);
                    data[idx + 3] = 255u;
                }
            }
            s_paletteLUT->subImage2D(
                0, 0, 256, 16,
                PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, data.data()
            );
            IRRender::getNamedResource<Texture2D>("PaletteLUT_Linear")->subImage2D(
                0, 0, 256, 16,
                PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, data.data()
            );
        }

        return createSystem<
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior,
            C_CanvasAOTexture,
            C_CanvasSunShadow
        >(
            "LightingToTrixel",
            [](const C_TriangleCanvasTextures &canvasTextures,
               const C_TrixelCanvasRenderBehavior &behavior,
               const C_CanvasAOTexture &ao,
               const C_CanvasSunShadow &shadow) {
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
                // Palette LUT on texture unit 3; canvasSunShadow on image
                // unit 4. GLSL keeps sampler and image namespaces separate,
                // but Metal flattens them into a shared setTexture slot
                // space — so shadow must skip past unit 3 to avoid the LUT
                // sampler. Keep the GLSL and MSL unit numbers in lockstep.
                s_paletteLUT->bind(3);
                shadow.getTexture()->bindAsImage(
                    4, TextureAccess::READ_ONLY, TextureFormat::RGBA8
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
