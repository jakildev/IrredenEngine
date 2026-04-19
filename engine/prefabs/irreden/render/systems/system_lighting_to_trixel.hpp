#ifndef SYSTEM_LIGHTING_TO_TRIXEL_H
#define SYSTEM_LIGHTING_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

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

// CPU-side mirror of the GLSL/MSL `FrameDataLightingToTrixel` UBO. Kept
// simple during the skeleton phase; later phases will add inputs for AO,
// shadow map, and flood-fill light volumes.
struct FrameDataLightingToTrixel {
    int lightingEnabled_ = 0;
    int padding0_ = 0;
    int padding1_ = 0;
    int padding2_ = 0;
};

// T-011: screen-space lighting application pass.
//
// Inserts `LIGHTING_TO_TRIXEL` between the final geometry stage (text /
// shapes / voxels — whichever a creation schedules last) and the
// compositing stage (`TRIXEL_TO_TRIXEL` / `TRIXEL_TO_FRAMEBUFFER`). The
// compute shader reconstructs a 3D world position from the distance
// texture and modulates the canvas color by lighting data produced by
// later phases (T-010 3D occupancy, T-012 AO, T-013 shadows).
//
// Acceptance-criterion notes:
//  - (2) no-op when no lighting data bound: the UBO's `lightingEnabled`
//    field stays at 0 until a later phase sets it, so every shader thread
//    early-returns.
//  - (4) GUI pixels untouched: we filter canvases on
//    `C_TrixelCanvasRenderBehavior::useCameraPositionIso_` — the engine's
//    GUI canvas is constructed with that flag false, so the dispatch
//    skips GUI-sourced pixels entirely. World canvases (main + any
//    per-entity canvas the creation parents into the world) pass through.
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

        return createSystem<C_TriangleCanvasTextures, C_TrixelCanvasRenderBehavior>(
            "LightingToTrixel",
            [](const C_TriangleCanvasTextures &canvasTextures,
               const C_TrixelCanvasRenderBehavior &behavior) {
                // Skip GUI canvases — acceptance criterion 4.
                if (!behavior.useCameraPositionIso_) {
                    return;
                }
                // Nothing to modulate until later lighting phases land;
                // avoid a per-canvas dispatch that would shader-early-out
                // anyway.
                if (frameData.lightingEnabled_ == 0) {
                    return;
                }

                canvasTextures.getTextureColors()->bindAsImage(
                    0, TextureAccess::READ_WRITE, TextureFormat::RGBA8
                );
                canvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_ONLY, TextureFormat::R32I
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
                // Lighting inputs land in later phases; until then the
                // shader treats the pass as a no-op via `lightingEnabled`.
                frameData.lightingEnabled_ = 0;
                s_frameDataBuf->subData(
                    0, sizeof(FrameDataLightingToTrixel), &frameData
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_LIGHTING_TO_TRIXEL_H */
