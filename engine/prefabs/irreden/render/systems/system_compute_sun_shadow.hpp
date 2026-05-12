#ifndef SYSTEM_COMPUTE_SUN_SHADOW_H
#define SYSTEM_COMPUTE_SUN_SHADOW_H

// Per-pixel directional sun-shadow compute pass. For each rasterized
// surface pixel reconstructs the voxel-space surface position from the
// canvas distance texture, samples the sun-aligned depth map baked by
// BAKE_SUN_SHADOW_MAP, and writes a brightness factor (1.0 lit,
// kShadowDarken shadowed) into the canvas sun-shadow texture consumed
// later by LIGHTING_TO_TRIXEL.
//
// Pipeline order: must run after BAKE_SUN_SHADOW_MAP (writes the depth
// map this pass reads) and after VOXEL_TO_TRIXEL_STAGE_2 / SHAPES_TO_TRIXEL
// (so the distance texture is populated), and before LIGHTING_TO_TRIXEL
// which folds the shadow factor into final color.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_compute_sun_shadow.glsl.
constexpr int kComputeSunShadowGroupSize = 16;

template <> struct System<COMPUTE_SUN_SHADOW> {
    ShaderProgram *program_ = nullptr;
    Buffer *sunShadowFrameDataBuf_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    // Created by BAKE_SUN_SHADOW_MAP. Resolved lazily so this lookup
    // links even when the bake system isn't registered.
    Buffer *sunShadowDepthMap_ = nullptr;

    void tick(
        const C_TriangleCanvasTextures &canvasTextures,
        const C_CanvasSunShadow &shadow,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        // Skip GUI-only canvases — same rationale as the AO pass.
        if (!behavior.useCameraPositionIso_)
            return;
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        canvasTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        shadow.getTexture()->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        if (sunShadowDepthMap_ == nullptr) {
            sunShadowDepthMap_ = IRRender::getNamedResource<Buffer>("SunShadowDepthMap");
        }
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);

        const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kComputeSunShadowGroupSize);
        const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kComputeSunShadowGroupSize);
        IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
    }

    void beginTick() {
        program_->use();
        // BAKE_SUN_SHADOW_MAP owns FrameDataSun uploads — its
        // beginTick writes the full struct (sun direction +
        // basis + AABB + flags) before this pass reads it.
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeSunShadowProgram",
            std::vector{ShaderStage{IRRender::kFileCompComputeSunShadow, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "ComputeSunShadowFrameData",
            nullptr,
            sizeof(FrameDataSun),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataSun
        );

        SystemId systemId = registerSystem<
            COMPUTE_SUN_SHADOW,
            C_TriangleCanvasTextures,
            C_CanvasSunShadow,
            C_TrixelCanvasRenderBehavior>("ComputeSunShadow");
        auto *p = getSystemParams<System<COMPUTE_SUN_SHADOW>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("ComputeSunShadowProgram");
        p->sunShadowFrameDataBuf_ = IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        IRRender::tagGpuStage(systemId, "computeSunShadow");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_SUN_SHADOW_H */
