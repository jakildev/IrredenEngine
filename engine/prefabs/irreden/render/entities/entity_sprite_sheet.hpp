#ifndef ENTITY_SPRITE_SHEET_H
#define ENTITY_SPRITE_SHEET_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_utility.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/ir_asset.hpp>
#include <irreden/render/components/component_sprite_sheet.hpp>

#include <string>
#include <vector>

/// @namespace IRSprite
/// Free functions for building and loading sprite-sheet data.
/// These live outside @c IRRender:: / @c IRAsset:: to keep each module
/// focused: @c IRAsset handles file I/O, @c IRRender owns GPU resources,
/// and @c IRSprite wires them together at the prefab layer.
namespace IRSprite {

/// Compute the frame UV-rect table from atlas pixel dimensions and sidecar
/// metadata.  Frames are enumerated row-major, left-to-right inside each row,
/// top-to-bottom across rows.  Returns an empty vector for degenerate inputs
/// (zero cell or atlas dimension).
///
/// Padding convention: @p meta.padding_ is the gap BETWEEN cells, not after
/// the last cell in each row/column (Aseprite / Texture Packer default).
/// Number of columns = (atlasW - 2*margin + padding) / (cellW + padding).
inline std::vector<IRComponents::SpriteFrame>
buildFrameTable(IRMath::uvec2 atlasSizePx, const IRAsset::SpriteSheetMeta &meta) {
    if (meta.cellSizePx_.x == 0 || meta.cellSizePx_.y == 0 || atlasSizePx.x == 0 ||
        atlasSizePx.y == 0) {
        return {};
    }
    const int cellW = static_cast<int>(meta.cellSizePx_.x);
    const int cellH = static_cast<int>(meta.cellSizePx_.y);
    const int aw = static_cast<int>(atlasSizePx.x);
    const int ah = static_cast<int>(atlasSizePx.y);
    const int cols = (aw - 2 * meta.margin_ + meta.padding_) / (cellW + meta.padding_);
    const int rows = (ah - 2 * meta.margin_ + meta.padding_) / (cellH + meta.padding_);
    if (cols <= 0 || rows <= 0) {
        return {};
    }

    const float faw = static_cast<float>(aw);
    const float fah = static_cast<float>(ah);
    const float uw = static_cast<float>(cellW) / faw;
    const float uh = static_cast<float>(cellH) / fah;

    std::vector<IRComponents::SpriteFrame> frames;
    frames.reserve(static_cast<std::size_t>(cols * rows));
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const float u = static_cast<float>(meta.margin_ + col * (cellW + meta.padding_)) / faw;
            const float v = static_cast<float>(meta.margin_ + row * (cellH + meta.padding_)) / fah;
            frames.push_back({IRMath::vec4{u, v, u + uw, v + uh}, IRMath::ivec2{cellW, cellH}});
        }
    }
    return frames;
}

/// Load a sprite sheet from disk and return a populated @c C_SpriteSheet.
///
/// Expects two files in @p path:
///   - @p name .png   — RGBA atlas image (loaded via stb_image).
///   - @p name .irsprite — sidecar metadata (see @c IRAsset::loadSpriteSheetMeta).
///
/// The returned component owns a GPU Texture2D identified by
/// @c sheet.textureHandle_.  The caller is responsible for releasing it via
/// @c IRRender::destroyResource<IRRender::Texture2D>(sheet.textureHandle_)
/// when the sheet is no longer needed.
inline IRComponents::C_SpriteSheet
loadSpriteSheet(const std::string &name, const std::string &path) {
    IRAsset::SpriteSheetMeta meta = IRAsset::loadSpriteSheetMeta(name, path);

    const std::string pngPath = IRUtility::joinPath(path, name, ".png");
    IRRender::ImageData img{pngPath.c_str()};
    if (!img.data_) {
        return {};
    }
    const IRMath::uvec2 atlasSizePx{
        static_cast<unsigned int>(img.width_),
        static_cast<unsigned int>(img.height_)
    };

    auto [texHandle, tex] = IRRender::createResource<IRRender::Texture2D>(
        IRRender::TextureKind::TEXTURE_2D,
        static_cast<unsigned int>(img.width_),
        static_cast<unsigned int>(img.height_),
        IRRender::TextureFormat::RGBA8,
        IRRender::TextureWrap::CLAMP_TO_EDGE,
        IRRender::TextureFilter::NEAREST
    );
    tex->subImage2D(
        0,
        0,
        img.width_,
        img.height_,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        img.data_
    );

    auto frames = buildFrameTable(atlasSizePx, meta);

    std::vector<IRComponents::NamedAnimation> animations;
    animations.reserve(meta.animations_.size());
    for (const auto &a : meta.animations_) {
        animations.push_back(
            {a.name_, IRComponents::SpriteAnimation{a.firstFrame_, a.frameCount_, a.fps_}}
        );
    }

    return IRComponents::C_SpriteSheet{
        texHandle,
        atlasSizePx,
        std::move(frames),
        std::move(animations)
    };
}

} // namespace IRSprite

#endif /* ENTITY_SPRITE_SHEET_H */
