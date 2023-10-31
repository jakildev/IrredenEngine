/*
 * Project: Irreden Engine
 * File: ir_math.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_math.hpp>
#include <random>

namespace IRMath {

    const bool randomBool() {
        return rand() % 2 == 0;
    }

    const int randomInt(const int min, const int max) {
        return min + (rand() % (max - min + 1));
    }

    const float randomFloat(const float min, const float max) {
        return
            min +
            (
                static_cast<float>(rand()) /
                static_cast<float>(RAND_MAX/(max-min))
            );
    }
    const Color randomColor()
    {
        return Color {
            static_cast<uint8_t>(randomInt(0, 255)),
            static_cast<uint8_t>(randomInt(0, 255)),
            static_cast<uint8_t>(randomInt(0, 255)),
            255
        };
    }

    const Color randomColor(const std::vector<Color>& colorPalette) {
        if (colorPalette.empty()) {
            return randomColor();
        }

        return colorPalette[randomInt(0, colorPalette.size() - 1)];
    }

    vec2 symmetricRound(const vec2& input)
    {
        ivec2 rounded = round(input);
        vec2 fractional = input - vec2(rounded);

        if(fractional.x + fractional.y > 1.0f) {
            return ceil(input);
        }
        return floor(input);
    }

    int rgbDifference(const Color& color1, const Color& color2) {
        return
            abs(color1.red_ - color2.red_) +
            abs(color1.green_ - color2.green_) +
            abs(color1.blue_ - color2.blue_)
        ;
    }

    const vec3 randomVec(const vec3 min, const vec3 max) {
        return vec3(
            randomFloat(min.x, max.x),
            randomFloat(min.y, max.y),
            randomFloat(min.z, max.z)
        );
    }

    vec2 calculateTrianglePositionOffsetIso(
        const vec2 position
    )
    {
        vec2 res = position;
        vec2 flooredComp = glm::floor(position);
        vec2 fractComp = glm::fract(position);
        if(glm::mod(flooredComp.x + flooredComp.y, 2.0f) < 1) {
            if(fractComp.y < fractComp.x) {
                res += vec2(0, -1);
            }
        }
        else {
            if(fractComp.y < 1 - fractComp.x) {
                res += vec2(0, -1);
            }
        }
        return res;
    }

    Color colorHSVToColor(const ColorHSV& colorHSV) {
        vec3 rgbColor = glm::rgbColor(vec3(
            colorHSV.hue_,
            colorHSV.saturation_,
            colorHSV.value_
        ));

        return Color {
            roundFloatToByte(rgbColor.r),
            roundFloatToByte(rgbColor.g),
            roundFloatToByte(rgbColor.b),
            roundFloatToByte(colorHSV.alpha_)
        };
    }

    ColorHSV colorToColorHSV(const Color& color) {
        vec3 colorHSV = glm::hsvColor(vec3(
            roundByteToFloat(color.red_),
            roundByteToFloat(color.green_),
            roundByteToFloat(color.blue_)
        ));
        return ColorHSV {
            colorHSV.r,
            colorHSV.g,
            colorHSV.b,
            (float)color.alpha_ / 255.0f
        };
    }

} // namespace IRMath