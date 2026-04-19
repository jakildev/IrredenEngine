#ifndef SYSTEM_COMPUTE_SUN_SHADOW_H
#define SYSTEM_COMPUTE_SUN_SHADOW_H

// Per-pixel directional sun-shadow compute pass. For each rasterized
// surface pixel reconstructs the voxel-space surface position from the
// canvas distance texture, marches toward the sun through the 3D
// occupancy grid, and writes a brightness factor (1.0 lit, kShadowDarken
// shadowed) into the canvas sun-shadow texture consumed later by
// LIGHTING_TO_TRIXEL.
//
// Pipeline order: must run after BUILD_OCCUPANCY_GRID so the SSBO is
// fresh, after VOXEL_TO_TRIXEL_STAGE_2 / SHAPES_TO_TRIXEL so the
// distance texture is populated, and before LIGHTING_TO_TRIXEL which
// folds the shadow factor into final color.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_compute_sun_shadow.glsl.
constexpr int kComputeSunShadowGroupSize = 16;

// CPU-side mirror of the GLSL/MSL `FrameDataSunShadow` UBO.
struct FrameDataSunShadow {
    // xyz = unit vector pointing from surface toward the sun; w unused.
    vec4 sunDirection_ = vec4(0.0f, 1.0f, 0.0f, 0.0f);
};

template <> struct System<COMPUTE_SUN_SHADOW> {
    static SystemId create() {
        static FrameDataSunShadow frameData{};

        IRRender::createNamedResource<ShaderProgram>(
            "ComputeSunShadowProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompComputeSunShadow, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "ComputeSunShadowFrameData",
            nullptr,
            sizeof(FrameDataSunShadow),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataSunShadow
        );

        static ShaderProgram *s_program =
            IRRender::getNamedResource<ShaderProgram>("ComputeSunShadowProgram");
        static Buffer *s_sunShadowFrameDataBuf =
            IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        static Buffer *s_occupancySSBO =
            IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");
        static Buffer *s_voxelFrameDataBuf =
            IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");

        return createSystem<
            C_TriangleCanvasTextures,
            C_CanvasSunShadow,
            C_TrixelCanvasRenderBehavior
        >(
            "ComputeSunShadow",
            [](const C_TriangleCanvasTextures &canvasTextures,
               const C_CanvasSunShadow &shadow,
               const C_TrixelCanvasRenderBehavior &behavior) {
                // Skip GUI-only canvases — same rationale as the AO pass.
                if (!behavior.useCameraPositionIso_) return;
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

                canvasTextures.getTextureDistances()->bindAsImage(
                    0, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                shadow.getTexture()->bindAsImage(
                    1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                );
                s_occupancySSBO->bindBase(
                    BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid
                );
                s_voxelFrameDataBuf->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas
                );
                s_sunShadowFrameDataBuf->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataSunShadow
                );

                const int groupsX = IRMath::divCeil(
                    canvasTextures.size_.x, kComputeSunShadowGroupSize
                );
                const int groupsY = IRMath::divCeil(
                    canvasTextures.size_.y, kComputeSunShadowGroupSize
                );
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            },
            []() {
                s_program->use();
                const vec3 sunDir = IRRender::getSunDirection();
                frameData.sunDirection_ = vec4(sunDir, 0.0f);
                s_sunShadowFrameDataBuf->subData(
                    0, sizeof(FrameDataSunShadow), &frameData
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_SUN_SHADOW_H */
