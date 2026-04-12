#ifndef COLOR_PALETTES_H
#define COLOR_PALETTES_H

#include <irreden/math/ir_math_types.hpp>

#include <vector>

namespace IRMath {

/// Five soft pastel tones: coral, sky blue, golden yellow, muted purple,
/// pale cyan.
const std::vector<Color> kPalettePastel5 = {
    Color{0xFE, 0x93, 0x8C, 0xFF}, // coral
    Color{0x89, 0xDA, 0xFF, 0xFF}, // sky blue
    Color{0xF5, 0xCB, 0x5C, 0xFF}, // golden yellow
    Color{0x8E, 0x7D, 0xBE, 0xFF}, // muted purple
    Color{0xD4, 0xF5, 0xF5, 0xFF}  // pale cyan
};

/// Warm accent palette: lavender pink, near-black dark blue, dark cool
/// brown, dun tan, burnt sienna.
const std::vector<Color> kPinkTanOrange = {
    Color{0xFF, 0xA9, 0xE7, 0xFF}, // lavender pink
    Color{0x0f, 0x0a, 0x23, 0xFF}, // near-black dark blue
    Color{0x1f, 0x1b, 0x18, 0xFF}, // dark cool brown
    Color{0xD4, 0xCB, 0xB3, 0xFF}, // dun
    Color{0xCE, 0x6C, 0x47, 0xFF}  // burnt sienna
};

/// Calm, muted palette suitable for dreamy or atmospheric backgrounds:
/// lavender, steel blue, dark navy, salmon, rose.
const std::vector<Color> kPaletteSleepy = {
    Color{0x97, 0x75, 0xbd, 0xFF}, // lavender
    Color{0x21, 0x4d, 0x72, 0xFF}, // steel blue
    Color{0x24, 0x2c, 0x3d, 0xFF}, // dark navy
    Color{0xf1, 0xb3, 0xbe, 0xFF}, // salmon
    Color{0xff, 0x2c, 0x3d, 0xFF}  // rose
};

} // namespace IRMath

#endif /* COLOR_PALETTES_H */
