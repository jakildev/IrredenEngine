/*
 * Project: Irreden Engine
 * File: color_palettes.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COLOR_PALETTES_H
#define COLOR_PALETTES_H

#include <irreden/math/ir_math_types.hpp>

#include <vector>

namespace IRMath {

    const std::vector<Color> kPalettePastel5 = {
        Color{
            0xFE, 0x93, 0x8C, 0xFF
        },
        Color{
            0x89, 0xDA, 0xFF, 0xFF
        },
        Color{
            0xF5, 0xCB, 0x5C, 0xFF
        },
        Color{
            0x8E, 0x7D, 0xBE, 0xFF
        },
        Color{
            0xD4, 0xF5, 0xF5, 0xFF
        }
    };

    const std::vector<Color> kPinkTanOrange = {
        Color{
            0xFF, 0xA9, 0xE7, 0xFF // lavender pink
        },
        Color{
            // 0x01, 0x17, 0x2F, 0xFF // oxford blue
            // 0x01, 0x0B, 0x16, 0xFF // Rich Black
            // 0x0c, 0x0b, 0x10, 0xFF // Gray dark blue
            // 0x21, 0x1A, 0x3F, 0xFF // Dark blue mostly sat
            0x0f, 0x0a, 0x23, 0xFF // Darker blue mostly sat
        },
        Color{
            // 0x08, 0x15, 0x19, 0xFF // Right black 2
            // 0x2d, 0x25, 0x1e, 0xFF // Cool Brown
            0x1f, 0x1b, 0x18, 0xFF // Dark Cool Brown

            // 0x1C, 0x41, 0x4C, 0xFF // Midnight green
            // 0x4D, 0x7C, 0x8A, 0xFF // air force blue
        },
        Color{
            0xD4, 0xCB, 0xB3, 0xFF // dun
        },
        Color{
            0xCE, 0x6C, 0x47, 0xFF // burnt siena
        }
    };

    const std::vector<Color> kPaletteSleepy = {
        Color{
            0x97, 0x75, 0xbd, 0xFF // lav
        },
        Color{
            0x21, 0x4d, 0x72, 0xFF // blue
        },
        Color{
            0x24, 0x2c, 0x3d, 0xFF // dark blue
        },
        Color{
            0xf1, 0xb3, 0xbe, 0xFF // salm
        },
        Color{
            0xff, 0x2c, 0x3d, 0xFF // tan
        }
    };

} // namespace IRMath

#endif /* COLOR_PALETTES_H */
