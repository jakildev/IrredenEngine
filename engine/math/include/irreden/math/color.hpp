#ifndef IR_MATH_COLOR_H
#define IR_MATH_COLOR_H

#include <irreden/math/ir_math_types.hpp>

#include <algorithm>
#include <vector>

namespace IRMath {

/// Returns `colors` sorted ascending by HSV hue (0°–360°).
/// Converts each Color from RGB to HSV via `glm::hsvColor` and compares the
/// H channel (vec3.x). Takes by value so the caller's vector is not mutated.
inline std::vector<Color> sortByHue(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.x < hb.x;
    });
    return colors;
}

/// Returns `colors` sorted ascending by HSV saturation (0–1).
/// Converts each Color from RGB to HSV and compares the S channel (vec3.y).
inline std::vector<Color> sortBySaturation(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.y < hb.y;
    });
    return colors;
}

/// Returns `colors` sorted ascending by HSV value (brightness, 0–1).
/// Converts each Color from RGB to HSV and compares the V channel (vec3.z).
inline std::vector<Color> sortByValue(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.z < hb.z;
    });
    return colors;
}

/// Returns `colors` sorted ascending by perceptual luminance.
/// Uses the Rec. 601 luma formula: Y = 0.299R + 0.587G + 0.114B, where R/G/B
/// are raw uint8_t byte values (not normalized). Darker colors sort first.
inline std::vector<Color> sortByLuminance(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        float la = 0.299f * a.red_ + 0.587f * a.green_ + 0.114f * a.blue_;
        float lb = 0.299f * b.red_ + 0.587f * b.green_ + 0.114f * b.blue_;
        return la < lb;
    });
    return colors;
}

} // namespace IRMath

#endif /* IR_MATH_COLOR_H */
