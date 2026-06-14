#ifndef SYSTEM_BUILD_DISTANCE_HIZ_H
#define SYSTEM_BUILD_DISTANCE_HIZ_H

// Hi-Z (hierarchical max-depth) distance mip-chain build (#1294 child 1/3;
// docs/design/voxel-occlusion-culling.md § Implementation sketch step 1).
//
// For each world canvas, downsample-max the R32I distance texture into the
// canvas's hiZMips_ chain: one compute dispatch per level, reading the prior
// level and writing the per-texel MAX (farthest) of its 2x2 source footprint.
// Conceptual level 0 is the distance texture itself, so the chain holds the
// downsampled levels 1..N. A max pyramid answers "the farthest visible surface
// over this footprint"; next frame's chunk-occlusion pre-pass (child 2) culls a
// pool-chunk only when its nearest depth is strictly behind that max.
//
// This PR PRODUCES the Hi-Z only — nothing consumes it yet, so render output is
// byte-identical. The chunk-occlusion pre-pass (child 2) and the off-by-default
// gate (child 3) follow in the blocked_by chain.
//
// Pipeline order: must run after every stage that writes trixelDistances
// (VOXEL_TO_TRIXEL_STAGE_1, SHAPES_TO_TRIXEL) and after COMPUTE_VOXEL_AO, so the
// distance texture is final — register it immediately after COMPUTE_VOXEL_AO.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

#include <cstddef>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_build_distance_hiz.glsl (and the Metal
// threadgroup size registered in metal_pipeline.cpp).
constexpr int kBuildDistanceHiZGroupSize = 16;

template <> struct System<COMPUTE_DISTANCE_HIZ> {
    ShaderProgram *program_ = nullptr;

    void tick(
        const C_TriangleCanvasTextures &canvasTextures, const C_TrixelCanvasRenderBehavior &behavior
    ) {
        // GUI canvases carry no world-space geometry and never feed the voxel
        // occlusion cull, so a Hi-Z over their distances is wasted work — skip
        // (mirrors COMPUTE_VOXEL_AO's guard). A 1x1 canvas yields no levels.
        if (!behavior.useCameraPositionIso_)
            return;
        if (canvasTextures.hiZMips_.empty())
            return;
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        // Level 0 is the canvas distance texture; each subsequent level reads
        // the previous level's output (a barrier per level enforces that RAW
        // dependency — the chain is inherently sequential).
        const Texture2D *src = canvasTextures.getTextureDistances();
        for (std::size_t level = 0; level < canvasTextures.hiZMips_.size(); ++level) {
            const Texture2D *dst = canvasTextures.hiZMips_[level].second;
            src->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
            dst->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);

            const uvec2 dstSize = dst->getSize();
            const int groupsX =
                IRMath::divCeil(static_cast<int>(dstSize.x), kBuildDistanceHiZGroupSize);
            const int groupsY =
                IRMath::divCeil(static_cast<int>(dstSize.y), kBuildDistanceHiZGroupSize);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

            src = dst;
        }
    }

    void beginTick() {
        program_->use();
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "BuildDistanceHiZProgram",
            std::vector{ShaderStage{IRRender::kFileCompBuildDistanceHiZ, ShaderType::COMPUTE}}
        );

        SystemId systemId = registerSystem<
            COMPUTE_DISTANCE_HIZ,
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior>("BuildDistanceHiZ");
        auto *p = getSystemParams<System<COMPUTE_DISTANCE_HIZ>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("BuildDistanceHiZProgram");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_DISTANCE_HIZ_H */
