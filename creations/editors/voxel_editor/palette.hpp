#ifndef IR_VOXEL_EDITOR_PALETTE_H
#define IR_VOXEL_EDITOR_PALETTE_H

#include <irreden/ir_math.hpp>

// The editor's paint palette: its colours and the GUI-canvas geometry of the
// 4x4 swatch grid.
//
// The layout constants live here rather than inside initEntities because two
// callers need them: the editor builds the swatch widgets from them, and an
// authoring session aims a scripted click at a swatch's centre
// (Session::Builder::selectPaletteSwatch). A session that hardcoded a screen
// pixel would silently start clicking the panel background the first time the
// panel moved, so both sides derive from one description of the grid.

namespace IRVoxelEditor {

constexpr int kPaletteCount = 16;

// 16 distinct palette colors. Indexed in row-major order across the
// 4x4 panel grid. Colors are picked to span hue and value so the
// active-swatch indicator (theme.borderFocused_ outline) reads against
// every cell.
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

// Palette panel — fixed dock at the bottom-left of the GUI canvas, so it sits
// below the iso scene render and never covers the edit target. Sized to fit a
// 4x4 grid of 20-trixel swatches inside a 120x175-trixel panel — small enough
// not to crowd the workspace.
constexpr IRMath::ivec2 kPalettePanelPos{4, 240};
constexpr IRMath::ivec2 kPalettePanelSize{120, 175};
constexpr int kPaletteSwatchSize = 20;
constexpr int kPaletteSwatchGap = 4;
constexpr int kPaletteGridCols = 4;
constexpr IRMath::ivec2 kPaletteSwatchOrigin{kPalettePanelPos.x + 8, kPalettePanelPos.y + 48};

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
