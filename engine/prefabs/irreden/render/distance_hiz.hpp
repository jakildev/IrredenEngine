#ifndef DISTANCE_HIZ_H
#define DISTANCE_HIZ_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

namespace IRPrefab::OcclusionCull::detail {

inline void buildDistanceHiZ(
    const IRComponents::C_TriangleCanvasTextures &canvas, IRRender::ShaderProgram &program
) {
    using namespace IRRender;
    program.use();
    const Texture2D *source = canvas.getTextureDistances();
    for (const auto &mip : canvas.hiZMips_) {
        const Texture2D *destination = mip.second;
        source->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        destination->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
        const auto size = destination->getSize();
        device()->dispatchCompute(
            IRMath::divCeil(static_cast<int>(size.x), 16),
            IRMath::divCeil(static_cast<int>(size.y), 16),
            1
        );
        device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        source = destination;
    }
}

} // namespace IRPrefab::OcclusionCull::detail

#endif
