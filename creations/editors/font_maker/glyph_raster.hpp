#ifndef FONT_MAKER_GLYPH_RASTER_H
#define FONT_MAKER_GLYPH_RASTER_H

#include "font_editor.hpp"

#include <irreden/ir_math.hpp>
#include <irreden/render/trixel_font.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// CPU rasterization of glyph bitmaps onto a rectangle of GUI-canvas texels,
// so the editor can show unsaved working bitmaps (the compiled font buffer
// only holds the constexpr headers). Coordinates are canvas texels.
namespace FontMaker {

// Calls `visit(texel)` for every canvas texel source texel (col, row) of a
// glyph at parity-aligned `origin` covers when drawn at lattice scale
// `scale`: the rule c_text_to_trixel applies to kGlyphStyleLatticeScale
// commands, one texel in the apex column and 2k + 1 in the k-th column out,
// centred on the source row.
template <typename Visit>
void forEachLatticeTexel(IRMath::ivec2 origin, int col, int row, int scale, Visit &&visit) {
    const bool left = (col + row) % 2 == 0;
    const IRMath::ivec2 center = origin + IRMath::ivec2(col, row) * scale;
    for (int k = 0; k < scale; ++k) {
        const int x = left ? center.x + k : center.x + scale - 1 - k;
        for (int dy = -k; dy <= k; ++dy) {
            visit(IRMath::ivec2(x, center.y + dy));
        }
    }
}

// A rectangle of canvas texels: a colour per texel and, for hit testing, the
// source texel index that last painted it (-1 for none).
struct LatticeImage {
    IRMath::ivec2 origin_{0};
    IRMath::ivec2 size_{0};
    std::vector<IRMath::Color> colors_;
    std::vector<int> owners_;

    void reset(IRMath::ivec2 origin, IRMath::ivec2 size, IRMath::Color background) {
        origin_ = origin;
        size_ = IRMath::max(size, IRMath::ivec2(0));
        colors_.assign(static_cast<std::size_t>(size_.x * size_.y), background);
        owners_.assign(colors_.size(), -1);
    }

    bool contains(IRMath::ivec2 texel) const {
        const IRMath::ivec2 local = texel - origin_;
        return local.x >= 0 && local.y >= 0 && local.x < size_.x && local.y < size_.y;
    }

    void set(IRMath::ivec2 texel, IRMath::Color color, int owner = -1) {
        if (!contains(texel)) {
            return;
        }
        const IRMath::ivec2 local = texel - origin_;
        const std::size_t index = static_cast<std::size_t>(local.y * size_.x + local.x);
        colors_[index] = color;
        owners_[index] = owner;
    }

    int ownerAt(IRMath::ivec2 texel) const {
        if (!contains(texel)) {
            return -1;
        }
        const IRMath::ivec2 local = texel - origin_;
        return owners_[static_cast<std::size_t>(local.y * size_.x + local.x)];
    }
};

inline void rasterGlyphRows(
    LatticeImage &image,
    std::span<const std::uint8_t> rows,
    int width,
    IRMath::ivec2 origin,
    int scale,
    IRMath::Color color
) {
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        for (int col = 0; col < width; ++col) {
            if (IRRender::glyphRowPixel(rows[row], width, col)) {
                forEachLatticeTexel(origin, col, row, scale, [&](IRMath::ivec2 texel) {
                    image.set(texel, color);
                });
            }
        }
    }
}

// Lays `text` out in one line with `set`'s working bitmaps at `scale`, the
// step the engine's text layout uses for that font size. Returns the width
// drawn, in texels.
inline int rasterText(
    LatticeImage &image,
    const GlyphSet &set,
    std::string_view text,
    IRMath::ivec2 origin,
    int scale,
    IRMath::Color color
) {
    if (!set.loaded()) {
        return 0;
    }
    const int stepX = (set.source_.width_ + set.source_.spacingX_) * scale;
    IRMath::ivec2 cursor = origin;
    for (char c : text) {
        const int glyph = set.drawGlyph(c);
        if (glyph >= 0) {
            rasterGlyphRows(image, set.working_[glyph], set.source_.width_, cursor, scale, color);
        }
        cursor.x += stepX;
    }
    return cursor.x - origin.x;
}

} // namespace FontMaker

#endif /* FONT_MAKER_GLYPH_RASTER_H */
