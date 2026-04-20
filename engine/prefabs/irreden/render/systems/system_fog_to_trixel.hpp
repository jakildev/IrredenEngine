#ifndef SYSTEM_FOG_TO_TRIXEL_H
#define SYSTEM_FOG_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <cstdint>
#include <vector>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Must match local_size in c_fog_to_trixel.glsl / .metal.
constexpr int kFogToTrixelGroupSize = 16;

// Screen-space fog-of-war pass. Sits between LIGHTING_TO_TRIXEL (which
// modulates by AO × sun-shadow) and TRIXEL_TO_TRIXEL (compositing) so
// fog masks the *lit* canvas — explored cells fade their lighting too,
// not just the base albedo.
//
// GUI canvases skip this pass via the same
// `useCameraPositionIso_ == false` early-return that lighting uses; GUI
// pixels have no associated world position and would render garbage if
// the pos3D recovery ran on them.
//
// CPU→GPU sync: the fog texture is uploaded via `subImage2D` at most
// once per frame, gated by `C_CanvasFogOfWar::dirty_`. The CPU mirror
// stores only the .r channel (one byte per cell); upload expands to
// RGBA8 in a transient buffer because the GPU texture format is RGBA8
// (Metal binding-layout sharing — see C_CanvasSunShadow rationale).
template <> struct System<FOG_TO_TRIXEL> {
    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "FogToTrixelProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompFogToTrixel, ShaderType::COMPUTE}
            }
        );

        static ShaderProgram *s_program =
            IRRender::getNamedResource<ShaderProgram>("FogToTrixelProgram");
        static Buffer *s_voxelFrameDataBuf =
            IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");

        // Reusable CPU staging buffer for the .r → RGBA8 expansion. Held
        // at function scope (capturing static) so per-frame uploads stay
        // allocation-free regardless of how many fog canvases exist —
        // they all share this scratch since the system tick is serial
        // while the system pipeline is single-threaded per-canvas.
        // value-init on resize zeros the GBA bytes we never touch again.
        static std::vector<std::uint8_t> s_uploadScratch;

        return createSystem<
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior,
            C_CanvasFogOfWar
        >(
            "FogToTrixel",
            [](const C_TriangleCanvasTextures &canvasTextures,
               const C_TrixelCanvasRenderBehavior &behavior,
               C_CanvasFogOfWar &fog) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
                if (!behavior.useCameraPositionIso_) {
                    return;
                }

                if (fog.dirty_) {
                    const std::size_t cellCount = fog.cpuBuffer_.size();
                    if (s_uploadScratch.size() < cellCount * 4) {
                        s_uploadScratch.resize(cellCount * 4);
                    }
                    // Only the .r channel ever changes; GBA stay at the
                    // zero-init from resize. Cuts per-dirty-frame stores
                    // 4×, ~256K → ~64K at 256² grid.
                    for (std::size_t i = 0; i < cellCount; ++i) {
                        s_uploadScratch[i * 4] = fog.cpuBuffer_[i];
                    }
                    fog.getTexture()->subImage2D(
                        0, 0,
                        kFogOfWarSize, kFogOfWarSize,
                        PixelDataFormat::RGBA,
                        PixelDataType::UNSIGNED_BYTE,
                        s_uploadScratch.data()
                    );
                    fog.dirty_ = false;
                }

                canvasTextures.getTextureColors()->bindAsImage(
                    0, TextureAccess::READ_WRITE, TextureFormat::RGBA8
                );
                canvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                fog.getTexture()->bindAsImage(
                    2, TextureAccess::READ_ONLY, TextureFormat::RGBA8
                );
                // FrameDataVoxelToTrixel carries frameCanvasOffset /
                // trixelCanvasOffsetZ1 / voxelRenderOptions — needed for
                // the iso pixel → world pos3D recovery in the shader.
                // Each pass that consumes it re-binds explicitly because
                // any intervening dispatch may have rebound binding 7.
                s_voxelFrameDataBuf->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas
                );

                const int groupsX = IRMath::divCeil(
                    canvasTextures.size_.x, kFogToTrixelGroupSize
                );
                const int groupsY = IRMath::divCeil(
                    canvasTextures.size_.y, kFogToTrixelGroupSize
                );
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            },
            []() { s_program->use(); }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_TO_TRIXEL_H */
