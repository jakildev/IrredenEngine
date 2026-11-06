#ifndef IR_VOXEL_EDITOR_PALETTE_H
#define IR_VOXEL_EDITOR_PALETTE_H

#include <irreden/ir_math.hpp>

#include <string_view>

// Default colors and one page of swatch geometry. PalettePanel and
// Session::Builder share the geometry so scripted clicks follow dock moves.

namespace IRVoxelEditor {

constexpr int kPaletteCount = 16;

// Default indices are stable because authoring sessions select them directly.
constexpr IRMath::Color kPaletteColors[kPaletteCount] = {
    IRMath::Color{220, 80, 80, 255},
    IRMath::Color{220, 140, 60, 255},
    IRMath::Color{220, 200, 60, 255},
    IRMath::Color{120, 200, 60, 255},
    IRMath::Color{60, 200, 120, 255},
    IRMath::Color{60, 200, 200, 255},
    IRMath::Color{60, 140, 220, 255},
    IRMath::Color{80, 80, 220, 255},
    IRMath::Color{140, 60, 220, 255},
    IRMath::Color{220, 60, 200, 255},
    IRMath::Color{200, 200, 200, 255},
    IRMath::Color{140, 140, 140, 255},
    IRMath::Color{60, 60, 60, 255},
    IRMath::Color{220, 180, 140, 255},
    IRMath::Color{120, 80, 60, 255},
    IRMath::Color{240, 240, 240, 255},
};

constexpr std::string_view kPaletteNames[kPaletteCount] = {
    "Red",
    "Orange",
    "Yellow",
    "Lime",
    "Green",
    "Teal",
    "Blue",
    "Indigo",
    "Violet",
    "Magenta",
    "Silver",
    "Gray",
    "Charcoal",
    "Sand",
    "Brown",
    "White"
};

constexpr IRMath::ivec2 kPalettePanelPos{4, 112};
constexpr IRMath::ivec2 kPalettePanelSize{204, 440};
constexpr int kPaletteSwatchSize = 36;
constexpr int kPaletteSwatchGap = 8;
constexpr int kPaletteGridCols = 4;
constexpr IRMath::ivec2 kPaletteSwatchOffset{12, 56};
constexpr IRMath::ivec2 kPaletteSwatchOrigin{kPalettePanelPos + kPaletteSwatchOffset};

// Top-left GUI-canvas trixel of swatch @p index (the position its widget is
// built at).
constexpr IRMath::ivec2 paletteSwatchPos(int index) {
    const int row = index / kPaletteGridCols;
    const int col = index % kPaletteGridCols;
    return IRMath::ivec2(
        kPaletteSwatchOrigin.x + col * (kPaletteSwatchSize + kPaletteSwatchGap),
        kPaletteSwatchOrigin.y + row * (kPaletteSwatchSize + kPaletteSwatchGap)
    );
}

// Centre of swatch @p index, in GUI-canvas trixels — where a scripted cursor
// must land to hover it. Half a swatch in from the top-left corner keeps the
// aim clear of the neighbouring swatch even after the trixel->screen rounding.
constexpr IRMath::vec2 paletteSwatchCenterGuiTrixel(int index) {
    const IRMath::ivec2 pos = paletteSwatchPos(index);
    return IRMath::vec2(
        static_cast<float>(pos.x) + static_cast<float>(kPaletteSwatchSize) / 2.0f,
        static_cast<float>(pos.y) + static_cast<float>(kPaletteSwatchSize) / 2.0f
    );
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_PALETTE_H */
