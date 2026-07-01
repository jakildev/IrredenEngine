#ifndef COMPONENT_TRIANGLE_CANVAS_TEXTURES_H
#define COMPONENT_TRIANGLE_CANVAS_TEXTURES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

#include <utility>
#include <vector>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

// Factories for the trixel-canvas texture triple (color / distance / entity-id).
// Centralized here so C_TriangleCanvasTextures and C_PerAxisTrixelCanvases
// (smooth camera Z-yaw, #1308) cannot drift on format / wrap / filter — a
// silent mismatch between the two would corrupt the per-axis distance composite.
namespace detail {

inline std::pair<ResourceId, Texture2D *> makeCanvasColorTexture(ivec2 size) {
    return IRRender::createResource<IRRender::Texture2D>(
        TextureKind::TEXTURE_2D,
        size.x,
        size.y,
        TextureFormat::RGBA8,
        TextureWrap::REPEAT,
        TextureFilter::NEAREST
    );
}

inline std::pair<ResourceId, Texture2D *> makeCanvasDistanceTexture(ivec2 size) {
    return IRRender::createResource<IRRender::Texture2D>(
        TextureKind::TEXTURE_2D,
        size.x,
        size.y,
        TextureFormat::R32I,
        TextureWrap::REPEAT,
        TextureFilter::NEAREST
    );
}

inline std::pair<ResourceId, Texture2D *> makeCanvasEntityIdTexture(ivec2 size) {
    return IRRender::createResource<IRRender::Texture2D>(
        TextureKind::TEXTURE_2D,
        size.x,
        size.y,
        TextureFormat::RG32UI,
        TextureWrap::CLAMP_TO_EDGE,
        TextureFilter::NEAREST
    );
}

// Hi-Z (hierarchical max-depth) mip chain over the distance texture, for the
// voxel-pool occlusion cull (#1294 / docs/design/voxel-occlusion-culling.md).
// Returns the DOWNSAMPLED levels 1..N only — conceptual level 0 IS the
// canvas's own R32I distance texture, so the full-res copy is avoided. Each
// returned level is ceil(prev / 2) in each axis (ceil-division so every source
// texel maps into exactly one destination texel's 2x2 footprint; see
// c_build_distance_hiz.glsl), R32I to match the distance encoding, down to 1x1.
// A 1x1-or-smaller canvas yields an empty chain.
inline std::vector<std::pair<ResourceId, Texture2D *>> makeHiZMipChain(ivec2 size) {
    std::vector<std::pair<ResourceId, Texture2D *>> mips;
    ivec2 levelSize = size;
    while (levelSize.x > 1 || levelSize.y > 1) {
        levelSize =
            ivec2(IRMath::max(1, (levelSize.x + 1) / 2), IRMath::max(1, (levelSize.y + 1) / 2));
        mips.push_back(
            IRRender::createResource<IRRender::Texture2D>(
                TextureKind::TEXTURE_2D,
                levelSize.x,
                levelSize.y,
                TextureFormat::R32I,
                TextureWrap::CLAMP_TO_EDGE,
                TextureFilter::NEAREST
            )
        );
    }
    return mips;
}

} // namespace detail

struct C_TriangleCanvasTextures {
    ivec2 size_;
    std::pair<ResourceId, Texture2D *> textureTriangleColors_;
    std::pair<ResourceId, Texture2D *> textureTriangleDistances_;
    std::pair<ResourceId, Texture2D *> textureTriangleEntityIds_;
    // Hi-Z max-depth mip chain over textureTriangleDistances_ (#1294 child 1/3).
    // Conceptual level 0 IS textureTriangleDistances_; these are the downsampled
    // levels 1..N (each ceil(prev / 2), R32I), holding the per-texel MAX
    // (farthest) encoded distance over the source footprint. Produced each frame
    // by COMPUTE_DISTANCE_HIZ and consumed NEXT frame by the chunk-occlusion
    // pre-pass (child 2). Empty/background texels carry the 65535 sentinel — the
    // largest encoded value — so any footprint that still sees background keeps
    // the max at 65535 = "never occlude", the conservative direction.
    std::vector<std::pair<ResourceId, Texture2D *>> hiZMips_;

    // Subdivision factor this canvas actually rastered its voxel pool at this
    // frame, stamped by VOXEL_TO_TRIXEL_STAGE_1 after the per-canvas
    // subdivision cap (#1570 D2). For a DETACHED canvas this can be BELOW the
    // global IRRender::getVoxelRenderEffectiveSubdivisions() because the cap
    // keeps the model-space lattice inside the fixed canvas. The detached
    // composite (ENTITY_CANVAS_TO_FRAMEBUFFER) reads it to rescale the canvas's
    // model-frame depth into the shared framebuffer depth units (which run at
    // the global effSub × 4 encode scale) so a world-placed detached solid
    // depth-sorts against the floor / GRID solids correctly under zoom. 0 =
    // the canvas did not raster a voxel pool this frame (pure SDF / text
    // overlay) — the composite then keeps the pre-#1624 raw offset.
    int renderedSubdivisions_ = 0;

    // No-priority perf fast-path signal (#2155). Stamped each frame by
    // VOXEL_TO_TRIXEL_STAGE_1 from the canvas pool's per-trixel-priority
    // aggregate: 1 iff some voxel drawn into this canvas carries a non-zero
    // per-trixel priority (#1960), else 0. Published into the finalization UBO
    // (FrameDataTrixelToFramebuffer::anyPerTrixelPriority_) by both the main
    // gather (TRIXEL_TO_FRAMEBUFFER) and the detached composite
    // (ENTITY_CANVAS_TO_FRAMEBUFFER), gating the shader's entity-id decode read.
    // 0 = fast path (skip the read on non-hovered fragments). Same per-frame
    // stamp lifecycle as renderedSubdivisions_ (0 when no voxel pool rastered).
    int anyPerTrixelPriority_ = 0;

    C_TriangleCanvasTextures(ivec2 size)
        : size_{size}
        , textureTriangleColors_{detail::makeCanvasColorTexture(size)}
        , textureTriangleDistances_{detail::makeCanvasDistanceTexture(size)}
        , textureTriangleEntityIds_{detail::makeCanvasEntityIdTexture(size)}
        , hiZMips_{detail::makeHiZMipChain(size)} {}

    C_TriangleCanvasTextures() {}

    void onDestroy() {
        IRRender::destroyResource<Texture2D>(textureTriangleColors_.first);
        IRRender::destroyResource<Texture2D>(textureTriangleDistances_.first);
        IRRender::destroyResource<Texture2D>(textureTriangleEntityIds_.first);
        for (const auto &mip : hiZMips_) {
            IRRender::destroyResource<Texture2D>(mip.first);
        }
    }

    // Number of downsampled Hi-Z levels (levels 1..N). Level 0 is
    // getTextureDistances(); the cull samples whichever level's texel footprint
    // covers a pool-chunk's iso AABB.
    int hiZMipCount() const {
        return static_cast<int>(hiZMips_.size());
    }

    // Downsampled Hi-Z level `mip` (0-based over hiZMips_, i.e. conceptual mip
    // level `mip + 1`). Level 0 of the pyramid is getTextureDistances().
    const Texture2D *getHiZMip(int mip) const {
        IR_ASSERT(mip < hiZMipCount(), "getHiZMip: mip out of range");
        return hiZMips_[mip].second;
    }

    const Texture2D *getTextureColors() const {
        return textureTriangleColors_.second;
    }

    const Texture2D *getTextureDistances() const {
        return textureTriangleDistances_.second;
    }

    const Texture2D *getTextureEntityIds() const {
        return textureTriangleEntityIds_.second;
    }

    void bind(int textureUnitColors, int textureUnitDistances) const {
        textureTriangleColors_.second->bind(textureUnitColors);
        textureTriangleDistances_.second->bind(textureUnitDistances);
    }

    void bind(int textureUnitColors, int textureUnitDistances, int textureUnitEntityIds) const {
        textureTriangleColors_.second->bind(textureUnitColors);
        textureTriangleDistances_.second->bind(textureUnitDistances);
        textureTriangleEntityIds_.second->bind(textureUnitEntityIds);
    }

    void clear() const {
        textureTriangleColors_.second
            ->clear(PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, &u8vec4(0, 0, 0, 0)[0]);
        textureTriangleDistances_.second->clear(
            PixelDataFormat::RED_INTEGER,
            PixelDataType::INT32,
            &ivec1(IRConstants::kTrixelDistanceMaxDistance)[0]
        );
        uvec2 zero{0u, 0u};
        textureTriangleEntityIds_.second
            ->clear(PixelDataFormat::RG_INTEGER, PixelDataType::UINT32, &zero);
    }

    void clearWithColor(const Color &color) const {
        textureTriangleColors_.second
            ->clear(PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, &color);
        clearDistanceTexture();
    }

    void clearWithColorData(ivec2 size, const std::vector<Color> &colorData) const {
        textureTriangleColors_.second->subImage2D(
            0,
            0,
            size.x,
            size.y,
            PixelDataFormat::RGBA,
            PixelDataType::UNSIGNED_BYTE,
            colorData.data()
        );
        clearDistanceTexture();
    }

    void setTrixel(ivec2 index, Color color, int distance = 0) {
        textureTriangleColors_.second->subImage2D(
            index.x,
            index.y,
            1,
            1,
            PixelDataFormat::RGBA,
            PixelDataType::UNSIGNED_BYTE,
            &color
        );
        textureTriangleDistances_.second->subImage2D(
            index.x,
            index.y,
            1,
            1,
            PixelDataFormat::RED_INTEGER,
            PixelDataType::INT32,
            &distance
        );
    }

    IREntity::EntityId readEntityIdAt(ivec2 trixelPos) const {
        if (trixelPos.x < 0 || trixelPos.x >= size_.x || trixelPos.y < 0 ||
            trixelPos.y >= size_.y) {
            return IREntity::kNullEntity;
        }
        uvec2 packed{0u, 0u};
        textureTriangleEntityIds_.second->getSubImage2D(
            trixelPos.x,
            trixelPos.y,
            1,
            1,
            PixelDataFormat::RG_INTEGER,
            PixelDataType::UINT32,
            &packed
        );
        // Strip the per-trixel priority carrier (#1960) before reconstructing the
        // 64-bit id — same chokepoint as getEntityIdAtMouseTrixel.
        return static_cast<IREntity::EntityId>(IRRender::decodeCarrierEntityId(packed));
    }

    void clearDistances() const {
        textureTriangleDistances_.second->clear(
            PixelDataFormat::RED_INTEGER,
            PixelDataType::INT32,
            &ivec1(IRConstants::kTrixelDistanceMaxDistance)[0]
        );
    }

    void saveAsPNG() {}

  private:
    // Clears to kTrixelDistanceMaxDistance - 1 (= 65534) rather than
    // kTrixelDistanceMaxDistance (= 65535). This predates the introduction of
    // the public clearDistances() helper; the shader's "empty pixel" sentinel
    // is 65535, so this value is one step below the canonical empty state.
    //
    // When invoked via clearCanvasWithBackground() → clearCanvasAndDistances(),
    // the 65534 value is immediately overwritten by the unconditional
    // IRRender::device()->clearTexImage(...) call at the end of
    // clearCanvasAndDistances(), which restores kTrixelDistanceMaxDistance
    // (65535) on every frame. The
    // redundant GPU clear here is negligible in cost but retained for correctness
    // on call-sites (e.g. entity_trixel_canvas.hpp) that invoke clearWithColor()
    // or clearWithColorData() without a subsequent clearDistances().
    void clearDistanceTexture() const {
        textureTriangleDistances_.second->clear(
            PixelDataFormat::RED_INTEGER,
            PixelDataType::INT32,
            &ivec1(IRConstants::kTrixelDistanceMaxDistance - 1)[0]
        );
    }
};

} // namespace IRComponents

#endif /* COMPONENT_TRIANGLE_CANVAS_TEXTURES_H */
