#ifndef WIDGET_THEME_H
#define WIDGET_THEME_H

#include <irreden/ir_math.hpp>

namespace IRPrefab::Widget {

// Visual styling shared by every widget in a creation. A single instance
// lives as `defaultTheme()`; widgets read from it at render time. The
// values lock in the look of the editor for every Phase 0 — Phase 10
// implementation that builds on top, so additions should be additive
// (new fields default to existing-color values) and renames are
// breaking.
struct WidgetTheme {
    // Surfaces
    IRMath::Color backgroundIdle_ = IRMath::Color{40, 44, 56, 255};
    IRMath::Color backgroundHover_ = IRMath::Color{60, 68, 88, 255};
    IRMath::Color backgroundPressed_ = IRMath::Color{90, 100, 124, 255};
    IRMath::Color backgroundDisabled_ = IRMath::Color{32, 34, 40, 255};
    IRMath::Color panelBackground_ = IRMath::Color{24, 26, 34, 220};
    IRMath::Color panelTitleBackground_ = IRMath::Color{52, 58, 76, 255};

    // Borders
    IRMath::Color borderIdle_ = IRMath::Color{96, 108, 132, 255};
    IRMath::Color borderHover_ = IRMath::Color{160, 200, 240, 255};
    IRMath::Color borderPressed_ = IRMath::Color{220, 240, 255, 255};
    IRMath::Color borderFocused_ = IRMath::Color{240, 200, 120, 255};
    IRMath::Color borderDisabled_ = IRMath::Color{64, 68, 80, 255};

    // Text
    IRMath::Color textIdle_ = IRMath::Color{220, 224, 232, 255};
    IRMath::Color textDisabled_ = IRMath::Color{120, 124, 132, 255};

    // Slider track / thumb
    IRMath::Color sliderTrack_ = IRMath::Color{72, 80, 96, 255};
    IRMath::Color sliderThumbIdle_ = IRMath::Color{160, 168, 180, 255};
    IRMath::Color sliderThumbHover_ = IRMath::Color{210, 220, 240, 255};

    // Checkbox fill (when checked)
    IRMath::Color checkboxFill_ = IRMath::Color{160, 200, 240, 255};

    // Layout
    int borderThickness_ = 2;
    int padding_ = 4;
    int fontSize_ = 2;
    int sliderTrackThickness_ = 6;
    int sliderThumbWidth_ = 8;
    int checkboxBoxSize_ = 16;
};

// Default theme. A creation that wants custom colors mutates this once
// at init before constructing widgets. Inline header-level storage so
// every translation unit shares one instance (C++17 inline variable;
// not a function-local static, which is forbidden for mutable state by
// the system-state rule).
inline WidgetTheme g_defaultTheme;
inline WidgetTheme &defaultTheme() { return g_defaultTheme; }

} // namespace IRPrefab::Widget

#endif /* WIDGET_THEME_H */
