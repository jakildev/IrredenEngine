#include <irreden/ir_math.hpp>
#include <algorithm>
#include <cmath>

namespace IRMath {

const bool randomBool() {
    return rand() % 2 == 0;
}

const int randomInt(const int min, const int max) {
    return min + (rand() % (max - min + 1));
}

const float randomFloat(const float min, const float max) {
    return min + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX / (max - min)));
}
const Color randomColor() {
    return Color{
        static_cast<uint8_t>(randomInt(0, 255)),
        static_cast<uint8_t>(randomInt(0, 255)),
        static_cast<uint8_t>(randomInt(0, 255)),
        255
    };
}

const Color randomColor(const std::vector<Color> &colorPalette) {
    if (colorPalette.empty()) {
        return randomColor();
    }

    return colorPalette[randomInt(0, colorPalette.size() - 1)];
}

vec2 symmetricRound(const vec2 &input) {
    ivec2 rounded = round(input);
    vec2 fractional = input - vec2(rounded);

    if (fractional.x + fractional.y > 1.0f) {
        return ceil(input);
    }
    return floor(input);
}

int rgbDifference(const Color &color1, const Color &color2) {
    return abs(color1.red_ - color2.red_) + abs(color1.green_ - color2.green_) +
           abs(color1.blue_ - color2.blue_);
}

const vec3 randomVec(const vec3 min, const vec3 max) {
    return vec3(randomFloat(min.x, max.x), randomFloat(min.y, max.y), randomFloat(min.z, max.z));
}

vec2 pos2DIsoToTriangleIndex(const vec2 position, const int originModifier) {
    vec2 res = position;
    vec2 flooredComp = glm::floor(position);
    vec2 fractComp = glm::fract(position);
    if (abs(glm::mod(flooredComp.x + flooredComp.y + originModifier, 2.0f)) >= 1) {
        if (fractComp.y < fractComp.x) {
            res += vec2(0, -1);
        }
    } else {
        if (fractComp.y < 1 - fractComp.x) {
            res += vec2(0, -1);
        }
    }
    return res;
}

float fract(float value) {
    return glm::fract(value);
}

vec3 hsvToRgb(const vec3 &colorHSV) {
    // Engine-facing hue is normalized [0, 1). GLM expects degrees.
    const vec3 hsvDegrees = vec3(IRMath::fract(colorHSV.x) * 360.0f, colorHSV.y, colorHSV.z);
    return glm::rgbColor(hsvDegrees);
}

u8vec3 hsvToRgbBytes(const vec3 &colorHSV) {
    const vec3 colorRGB = hsvToRgb(colorHSV);
    return u8vec3(
        roundFloatToByte(colorRGB.r),
        roundFloatToByte(colorRGB.g),
        roundFloatToByte(colorRGB.b)
    );
}

Color colorHSVToColor(const ColorHSV &colorHSV) {
    vec3 rgbColor = hsvToRgb(vec3(colorHSV.hue_, colorHSV.saturation_, colorHSV.value_));

    return Color{
        roundFloatToByte(rgbColor.r),
        roundFloatToByte(rgbColor.g),
        roundFloatToByte(rgbColor.b),
        roundFloatToByte(colorHSV.alpha_)
    };
}

ColorHSV colorToColorHSV(const Color &color) {
    vec3 colorHSV = glm::hsvColor(vec3(
        roundByteToFloat(color.red_),
        roundByteToFloat(color.green_),
        roundByteToFloat(color.blue_)
    ));
    return ColorHSV{
        IRMath::fract(colorHSV.r / 360.0f),
        colorHSV.g,
        colorHSV.b,
        (float)color.alpha_ / 255.0f
    };
}

Color applyHSVOffset(const Color &base, const ColorHSV &offset) {
    ColorHSV hsv = colorToColorHSV(base);
    hsv.hue_ = IRMath::fract(hsv.hue_ + offset.hue_);
    hsv.saturation_ = IRMath::clamp(hsv.saturation_ + offset.saturation_, 0.0f, 1.0f);
    hsv.value_ = IRMath::clamp(hsv.value_ + offset.value_, 0.0f, 1.0f);
    hsv.alpha_ = IRMath::clamp(hsv.alpha_ + offset.alpha_, 0.0f, 1.0f);
    return colorHSVToColor(hsv);
}

namespace {
vec3 mapPlaneToVec3(const vec2 &planePoint, float depth, PlaneIso plane) {
    switch (plane) {
    case PlaneIso::XZ:
        return vec3(planePoint.x, depth, planePoint.y);
    case PlaneIso::YZ:
        return vec3(depth, planePoint.x, planePoint.y);
    case PlaneIso::XY:
    default:
        return vec3(planePoint.x, planePoint.y, depth);
    }
}
} // namespace

vec3 layoutGridCentered(
    int index,
    int count,
    int columns,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane,
    float depth
) {
    if (count <= 0) {
        return mapPlaneToVec3(vec2(0.0f), depth, plane);
    }

    const int indexClamped = std::clamp(index, 0, count - 1);
    const int cols = std::max(1, columns);
    const int rows = (count + cols - 1) / cols;

    const int col = indexClamped % cols;
    const int row = indexClamped / cols;

    const float cx = (static_cast<float>(cols) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(rows) - 1.0f) * 0.5f;

    const vec2 p(
        (static_cast<float>(col) - cx) * spacingPrimary,
        (static_cast<float>(row) - cy) * spacingSecondary
    );
    return mapPlaneToVec3(p, depth, plane);
}

vec3 layoutZigZagCentered(
    int index,
    int count,
    int itemsPerZag,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane,
    float depth
) {
    if (count <= 0) {
        return mapPlaneToVec3(vec2(0.0f), depth, plane);
    }

    const int indexClamped = std::clamp(index, 0, count - 1);
    const int width = std::max(1, itemsPerZag);
    const int rows = (count + width - 1) / width;

    const int row = indexClamped / width;
    const int colInRow = indexClamped % width;
    const int col = (row % 2 == 0) ? colInRow : (width - 1 - colInRow);

    const float cx = (static_cast<float>(width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(rows) - 1.0f) * 0.5f;

    const vec2 p(
        (static_cast<float>(col) - cx) * spacingPrimary,
        (static_cast<float>(row) - cy) * spacingSecondary
    );
    return mapPlaneToVec3(p, depth, plane);
}

vec3 layoutZigZagPath(
    int index,
    int count,
    int itemsPerSegment,
    float spacingPrimary,
    float spacingSecondary,
    PlaneIso plane,
    float depth
) {
    if (count <= 0) {
        return mapPlaneToVec3(vec2(0.0f), depth, plane);
    }

    const int idx = std::clamp(index, 0, count - 1);
    const int S = std::max(1, itemsPerSegment);

    auto zigzagPos = [&](int i) -> vec2 {
        const int arm = i / S;
        const int pos = i % S;
        float x, y;
        if (arm % 2 == 0) {
            x = -static_cast<float>((arm / 2) * S);
            y =  static_cast<float>((arm / 2) * S + pos);
        } else {
            x = -static_cast<float>(((arm - 1) / 2) * S + 1 + pos);
            y =  static_cast<float>(((arm + 1) / 2) * S - 1);
        }
        return vec2(x, y);
    };

    const vec2 cur  = zigzagPos(idx);
    const vec2 last = zigzagPos(count - 1);
    const vec2 center(last.x * 0.5f, last.y * 0.5f);

    const vec2 p(
        (cur.x - center.x) * spacingPrimary,
        (cur.y - center.y) * spacingSecondary
    );
    return mapPlaneToVec3(p, depth, plane);
}

vec3 layoutSquareSpiral(int index, float spacing, PlaneIso plane, float depth) {
    if (index <= 0) {
        return mapPlaneToVec3(vec2(0.0f), depth, plane);
    }

    int x = 0;
    int y = 0;
    int dx = 1;
    int dy = 0;
    int segmentLength = 1;
    int segmentProgress = 0;
    int segmentRepeats = 0;

    for (int step = 0; step < index; ++step) {
        x += dx;
        y += dy;
        ++segmentProgress;
        if (segmentProgress == segmentLength) {
            segmentProgress = 0;
            const int oldDx = dx;
            dx = -dy;
            dy = oldDx;
            ++segmentRepeats;
            if (segmentRepeats == 2) {
                segmentRepeats = 0;
                ++segmentLength;
            }
        }
    }

    return mapPlaneToVec3(vec2(static_cast<float>(x), static_cast<float>(y)) * spacing, depth, plane);
}

vec3 layoutHelix(
    int index, int count, float radius, float turns, float heightSpan, CoordinateAxis axis
) {
    const int countSafe = std::max(1, count);
    const float t =
        (countSafe <= 1) ? 0.0f : (static_cast<float>(std::clamp(index, 0, countSafe - 1)) /
                                    static_cast<float>(countSafe - 1));
    const float angle = t * turns * 2.0f * glm::pi<float>();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float h = (t - 0.5f) * heightSpan;

    switch (axis) {
    case CoordinateAxis::XAxis:
        return vec3(h, c * radius, s * radius);
    case CoordinateAxis::YAxis:
        return vec3(c * radius, h, s * radius);
    case CoordinateAxis::ZAxis:
    default:
        return vec3(c * radius, s * radius, h);
    }
}

vec3 layoutPathTangentArcs(
    int index,
    int count,
    float radius,
    int blocksPerArc,
    float zStep,
    CoordinateAxis axis,
    float startAngleRad,
    bool invert
) {
    // Chain of half-circles, each tangent to the next (centers 2*radius apart).
    // After each 180°, rotation switches CW/CCW so the path stays continuous.
    const int countSafe = std::max(1, count);
    float t =
        (countSafe <= 1) ? 0.0f : (static_cast<float>(std::clamp(index, 0, countSafe - 1)) /
                                    static_cast<float>(countSafe - 1));
    if (invert) {
        t = 1.0f - t;
    }
    const float pi = glm::pi<float>();
    const int numHalfCircles =
        std::max(1, (countSafe + std::max(1, blocksPerArc) - 1) / std::max(1, blocksPerArc));
    const float totalArc = static_cast<float>(numHalfCircles) * pi * radius;
    const float s = t * totalArc; // arc length position

    int k = 0;
    float u = 0.0f;
    {
        const float segLen = pi * radius;
        const float kf = s / segLen;
        k = std::min(static_cast<int>(std::floor(kf)), numHalfCircles - 1);
        u = (s - static_cast<float>(k) * segLen) / segLen;
    }

    const float cx = static_cast<float>(2 * k) * radius;
    float angle;
    if (k % 2 == 0) {
        // Even: trace right half CW, angle π → 0
        angle = pi - u * pi;
    } else {
        // Odd: trace left half CCW, angle π → 2π
        angle = pi + u * pi;
    }

    float pathX = cx + radius * std::cos(angle);
    float pathY = radius * std::sin(angle);
    const float heightSpan = zStep * static_cast<float>(std::max(0, countSafe - 1));
    const float h = (t - 0.5f) * heightSpan;

    // Center: path extends from -r to (2*K-1)*r; center at (K-1)*r
    pathX -= static_cast<float>(numHalfCircles - 1) * radius;

    // Rotate in plane by startAngle (e.g. pi/4 for head-on iso along diagonal)
    const float c0 = std::cos(startAngleRad);
    const float s0 = std::sin(startAngleRad);
    const float px = pathX * c0 - pathY * s0;
    const float py = pathX * s0 + pathY * c0;
    pathX = px;
    pathY = py;

    switch (axis) {
    case CoordinateAxis::XAxis:
        return vec3(h, pathX, pathY);
    case CoordinateAxis::YAxis:
        return vec3(pathX, h, pathY);
    case CoordinateAxis::ZAxis:
    default:
        return vec3(pathX, pathY, h);
    }
}

} // namespace IRMath