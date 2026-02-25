#ifndef IR_MATH_COLOR_H
#define IR_MATH_COLOR_H

#include <irreden/math/ir_math_types.hpp>

#include <algorithm>
#include <vector>

namespace IRMath {

inline std::vector<Color> sortByHue(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.x < hb.x;
    });
    return colors;
}

inline std::vector<Color> sortBySaturation(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.y < hb.y;
    });
    return colors;
}

inline std::vector<Color> sortByValue(std::vector<Color> colors) {
    std::sort(colors.begin(), colors.end(), [](const Color &a, const Color &b) {
        vec3 ha = glm::hsvColor(vec3(a.red_ / 255.0f, a.green_ / 255.0f, a.blue_ / 255.0f));
        vec3 hb = glm::hsvColor(vec3(b.red_ / 255.0f, b.green_ / 255.0f, b.blue_ / 255.0f));
        return ha.z < hb.z;
    });
    return colors;
}

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
