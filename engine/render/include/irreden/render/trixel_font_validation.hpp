#ifndef TRIXEL_FONT_VALIDATION_H
#define TRIXEL_FONT_VALIDATION_H

#include <irreden/render/trixel_font.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

namespace IRRender {

// The authoring rules a trixel glyph bitmap is checked against, on the lattice
// of a parity-aligned text origin: texel (c, r) is a left-pointing triangle
// when c + r is even, right-pointing when odd (docs/design/gui-trixel-display.md).
enum class GlyphRule : int {
    // A lit texel with no lit vertical neighbour and no lit diamond partner,
    // (c + 1, r) when c + r is even, else (c - 1, r).
    LONE_TRIANGLE = 0,
    // LARGE: a free end of an isolated 2-wide stem that renders as a notch.
    // SMALL: an odd-row texel in an outer column with no lit vertical
    // neighbour, which cannot pair into a diamond inside the cell.
    WRONG_PARITY = 1,
    // A non-empty bitmap another character of the same set also uses.
    DUPLICATE_BITMAP = 2,
};

struct GlyphTexelFinding {
    GlyphRule rule_;
    int col_;
    int row_;
};

constexpr const char *glyphRuleName(GlyphRule rule) {
    switch (rule) {
    case GlyphRule::LONE_TRIANGLE:
        return "lone_triangle";
    case GlyphRule::WRONG_PARITY:
        return "wrong_parity";
    case GlyphRule::DUPLICATE_BITMAP:
        return "duplicate_bitmap";
    }
    return "unknown";
}

// A glyph bitmap of `height_` rows, column 0 in bit (width_ - 1) of each row,
// the packing makeGlyph and makeSmallGlyph produce.
struct GlyphBitmap {
    int width_;
    int height_;
    std::span<const std::uint8_t> rows_;

    constexpr bool lit(int col, int row) const {
        return col >= 0 && col < width_ && row >= 0 && row < height_ &&
               glyphRowPixel(rows_[row], width_, col);
    }

    constexpr bool empty() const {
        for (std::uint8_t row : rows_) {
            if (row != 0) {
                return false;
            }
        }
        return true;
    }
};

inline void appendLoneTriangles(const GlyphBitmap &glyph, std::vector<GlyphTexelFinding> &out) {
    for (int row = 0; row < glyph.height_; ++row) {
        for (int col = 0; col < glyph.width_; ++col) {
            if (!glyph.lit(col, row)) {
                continue;
            }
            const int partner = (col + row) % 2 == 0 ? col + 1 : col - 1;
            if (!glyph.lit(col, row - 1) && !glyph.lit(col, row + 1) && !glyph.lit(partner, row)) {
                out.push_back({GlyphRule::LONE_TRIANGLE, col, row});
            }
        }
    }
}

// An isolated 2-wide stem is a run of 3+ rows where (c, r) and (c + 1, r) are
// lit and (c - 1, r) and (c + 2, r) are not. An end of it is free when the row
// beyond has both columns unlit, and it points only when c and the end row
// share parity. A shorter run is a diagonal segment, which may start anywhere.
inline void appendLargeStemParity(const GlyphBitmap &glyph, std::vector<GlyphTexelFinding> &out) {
    const auto stemRow = [&](int col, int row) {
        return glyph.lit(col, row) && glyph.lit(col + 1, row) && !glyph.lit(col - 1, row) &&
               !glyph.lit(col + 2, row);
    };
    const auto checkEnd = [&](int col, int end, int beyond) {
        if (!glyph.lit(col, beyond) && !glyph.lit(col + 1, beyond) && (col + end) % 2 != 0) {
            out.push_back({GlyphRule::WRONG_PARITY, col, end});
        }
    };
    for (int col = 0; col + 1 < glyph.width_; ++col) {
        int row = 0;
        while (row < glyph.height_) {
            if (!stemRow(col, row)) {
                ++row;
                continue;
            }
            const int first = row;
            while (row < glyph.height_ && stemRow(col, row)) {
                ++row;
            }
            const int last = row - 1;
            if (last - first + 1 >= 3) {
                checkEnd(col, first, first - 1);
                checkEnd(col, last, last + 1);
            }
        }
    }
}

inline void appendSmallBarParity(const GlyphBitmap &glyph, std::vector<GlyphTexelFinding> &out) {
    for (int row = 1; row < glyph.height_; row += 2) {
        for (int col : {0, glyph.width_ - 1}) {
            if (glyph.lit(col, row) && !glyph.lit(col, row - 1) && !glyph.lit(col, row + 1)) {
                out.push_back({GlyphRule::WRONG_PARITY, col, row});
            }
        }
    }
}

// Every per-texel finding for one glyph under `rules`' set-specific parity
// rule, lone triangles first.
inline std::vector<GlyphTexelFinding> glyphTexelFindings(TextSize rules, const GlyphBitmap &glyph) {
    std::vector<GlyphTexelFinding> findings;
    appendLoneTriangles(glyph, findings);
    if (rules == TextSize::SMALL) {
        appendSmallBarParity(glyph, findings);
    } else {
        appendLargeStemParity(glyph, findings);
    }
    return findings;
}

// Index pairs (i < j) of non-empty bitmaps that are identical. Every glyph
// must share the same width and height.
inline std::vector<std::pair<std::size_t, std::size_t>>
duplicateBitmapPairs(std::span<const GlyphBitmap> glyphs) {
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        if (glyphs[i].empty()) {
            continue;
        }
        for (std::size_t j = i + 1; j < glyphs.size(); ++j) {
            if (std::equal(
                    glyphs[i].rows_.begin(),
                    glyphs[i].rows_.end(),
                    glyphs[j].rows_.begin(),
                    glyphs[j].rows_.end()
                )) {
                pairs.emplace_back(i, j);
            }
        }
    }
    return pairs;
}

} // namespace IRRender

#endif /* TRIXEL_FONT_VALIDATION_H */
