#ifndef SYSTEM_LIGHTING_TO_TRIXEL_H
#define SYSTEM_LIGHTING_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <array>
#include <cstdint>

#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

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
// `lightVolumeEnabled_` activates flood-fill light-volume sampling: the
// per-pixel world voxel is recovered from the distance texture and the
// bound 3D light volume is sampled and additively combined with the AO
// base.
// `debugLightLevel_` is reserved for future shadow-preview use; it is
// kept in the UBO for std140 layout stability but the shader currently
// uses AO.r as the LUT X-axis input.
// `debugOverlayMode_` mirrors `IRRender::DebugOverlayMode`. Non-zero
// values short-circuit the artistic path and write false-color into
// `trixelColors` instead — see ir_render_enums.hpp for the encoding.
// std140 note: five scalars pack tightly at offsets 0,4,8,12,16 for a
// 20-byte UBO. Both C++ and the GLSL/MSL structs lay out identically —
// no explicit padding is needed.
struct FrameDataLightingToTrixel {
    int   lightingEnabled_     = 0;
    int   lutEnabled_          = 0;
    int   lightVolumeEnabled_  = 0;
    float debugLightLevel_     = 0.0f;
    int   debugOverlayMode_    = 0;
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
        // Reuse the voxel pipeline's per-frame UBO so we can recover the
        // world voxel position of each pixel via the same iso math the AO
        // pass uses. Created by VOXEL_TO_TRIXEL_STAGE_1; this system runs
        // later in the pipeline so the buffer is always populated.
        static Buffer *s_voxelFrameDataBuf =
            IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
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
            C_CanvasSunShadow,
            C_CanvasLightVolume
        >(
            "LightingToTrixel",
            [](const C_TriangleCanvasTextures &canvasTextures,
               const C_TrixelCanvasRenderBehavior &behavior,
               const C_CanvasAOTexture &ao,
               const C_CanvasSunShadow &shadow,
               const C_CanvasLightVolume &lightVolume) {
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
                // Texture/image unit layout (must match GLSL + MSL):
                //   3: paletteLUT (sampler2D)
                //   4: canvasSunShadow (image2D, R/O)
                //   5: lightVolume (sampler3D)
                // Metal flattens the sampler and image namespaces into a
                // shared setTexture slot space, so all three slots must be
                // unique across both kinds.
                s_paletteLUT->bind(3);
                shadow.getTexture()->bindAsImage(
                    4, TextureAccess::READ_ONLY, TextureFormat::RGBA8
                );
                lightVolume.getTexture()->bind(5);
                s_frameDataBuf->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataLightingToTrixel
                );
                s_voxelFrameDataBuf->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas
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
                frameData.lightVolumeEnabled_ = 1;
                frameData.debugOverlayMode_ =
                    static_cast<int>(IRRender::getDebugOverlay());
                s_frameDataBuf->subData(
                    0, sizeof(FrameDataLightingToTrixel), &frameData
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_LIGHTING_TO_TRIXEL_H */
