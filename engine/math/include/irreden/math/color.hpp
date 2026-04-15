#ifndef IR_MATH_COLOR_H
#define IR_MATH_COLOR_H

#include <irreden/math/ir_math_types.hpp>

#include <algorithm>
#include <vector>

namespace IRMath {

/// Returns @p colors sorted by HSV hue, ascending (red → yellow → green → … → red).
inline std::vector<Color> sortByHue(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.x < hb.x;
    });
    return colors;
}

/// Returns @p colors sorted by HSV saturation, ascending (grey → vivid).
inline std::vector<Color> sortBySaturation(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.y < hb.y;
    });
    return colors;
}

/// Returns @p colors sorted by HSV value (brightness), ascending (dark → bright).
inline std::vector<Color> sortByValue(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.z < hb.z;
    });
    return colors;
}

/// Returns @p colors sorted by perceptual luminance, ascending (dark → bright).
/// Luminance formula: `0.299r + 0.587g + 0.114b` (BT.601 luma coefficients).
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
