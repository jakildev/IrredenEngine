#ifndef COMPARISON_IMAGE_H
#define COMPARISON_IMAGE_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_platform.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace IRRender {

inline constexpr std::size_t kComparisonImagePixelLimit = 1024u * 1024u;

struct ComparisonImage {
    IRMath::ivec2 size_{0};
    std::vector<IRMath::Color> pixels_;

    bool valid() const {
        return size_.x > 0 && size_.y > 0 &&
               static_cast<std::size_t>(size_.x) * static_cast<std::size_t>(size_.y) <=
                   kComparisonImagePixelLimit &&
               pixels_.size() == static_cast<std::size_t>(size_.x) * size_.y;
    }
};

namespace detail {
inline bool comparisonSizeValid(IRMath::ivec2 size) {
    return size.x > 0 && size.y > 0 &&
           static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) <=
               kComparisonImagePixelLimit;
}
} // namespace detail

// Public readback returns top-down rows, but OpenGL's subrectangle origin is
// bottom-up. Keep that backend convention out of capture consumers.
inline IRMath::ivec2 comparisonReadbackOrigin(
    IRMath::ivec2 topLeft,
    IRMath::ivec2 size,
    IRMath::ivec2 framebuffer,
    IRPlatform::GraphicsBackend backend
) {
    return backend == IRPlatform::GraphicsBackend::OPENGL
               ? IRMath::ivec2(topLeft.x, framebuffer.y - topLeft.y - size.y)
               : topLeft;
}

// Capture after framebuffer-to-screen, before GUI/overlay composition. A bounded
// crop avoids full-window readbacks and contains no widget or input state.
inline std::optional<ComparisonImage>
captureComparisonImage(IRMath::ivec2 topLeft, IRMath::ivec2 size) {
    const auto framebuffer = IRRender::getViewport();
    if (!detail::comparisonSizeValid(size) || topLeft.x < 0 || topLeft.y < 0 ||
        size.x > framebuffer.x - topLeft.x || size.y > framebuffer.y - topLeft.y)
        return std::nullopt;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size.x) * size.y * 4);
    const auto origin =
        comparisonReadbackOrigin(topLeft, size, framebuffer, IRPlatform::kGraphicsBackend);
    if (!IRRender::readDefaultFramebuffer(origin.x, origin.y, size.x, size.y, rgba.data()))
        return std::nullopt;
    ComparisonImage image{size, {}};
    image.pixels_.reserve(rgba.size() / 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        image.pixels_.push_back({rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]});
    return image;
}

struct ComparisonPresentation {
    ComparisonImage image_;
    bool clipped_ = false;
};

// Centered nearest-neighbor presentation at one explicit rational scale.
// A shared divisor preserves relative extents; no occupied-bounds fitting occurs.
inline ComparisonPresentation presentComparisonImage(
    const ComparisonImage &source,
    IRMath::ivec2 tileSize,
    int magnification,
    IRMath::Color background = {16, 20, 24, 255},
    int divisor = 1
) {
    ComparisonPresentation result;
    if (!source.valid() || !detail::comparisonSizeValid(tileSize) || magnification < 1 ||
        magnification > 64 || divisor < 1 || divisor > 64 ||
        source.size_.x * magnification < divisor || source.size_.y * magnification < divisor)
        return result;
    result.image_.size_ = tileSize;
    result.image_.pixels_.assign(static_cast<std::size_t>(tileSize.x) * tileSize.y, background);
    const auto enlarged = source.size_ * magnification / divisor;
    const auto start = IRMath::ivec2(IRMath::floor(IRMath::vec2(tileSize - enlarged) * 0.5f));
    result.clipped_ = enlarged.x > tileSize.x || enlarged.y > tileSize.y;
    for (int y = 0; y < tileSize.y; ++y) {
        const int sourceY = y - start.y;
        if (sourceY < 0 || sourceY >= enlarged.y)
            continue;
        for (int x = 0; x < tileSize.x; ++x) {
            const int sourceX = x - start.x;
            if (sourceX < 0 || sourceX >= enlarged.x)
                continue;
            result.image_.pixels_[static_cast<std::size_t>(y) * tileSize.x + x] =
                source.pixels_
                    [static_cast<std::size_t>(sourceY * divisor / magnification) * source.size_.x +
                     sourceX * divisor / magnification];
        }
    }
    return result;
}

// Inputs must share the same world framing and presentation scale. Cyan is the
// reference and magenta is the selected tier; matching silhouettes overlap.
inline ComparisonImage onionComparisonImages(
    const ComparisonImage &reference, const ComparisonImage &selected, float selectedOpacity
) {
    if (!reference.valid() || !selected.valid() || reference.size_ != selected.size_ ||
        !IRMath::isFinite(selectedOpacity))
        return {};
    const float alpha = IRMath::clamp(selectedOpacity, 0.0f, 1.0f);
    ComparisonImage output{reference.size_, {}};
    output.pixels_.reserve(reference.pixels_.size());
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(IRMath::clamp(IRMath::roundHalfUp(value), 0, 255));
    };
    for (std::size_t i = 0; i < reference.pixels_.size(); ++i) {
        const auto &a = reference.pixels_[i];
        const auto &b = selected.pixels_[i];
        const float lumaA = (static_cast<float>(a.red_) + a.green_ + a.blue_) / 3.0f;
        const float lumaB = (static_cast<float>(b.red_) + b.green_ + b.blue_) / 3.0f;
        output.pixels_.push_back(
            {channel(lumaB * alpha),
             channel(lumaA * (1.0f - alpha)),
             channel(lumaA * (1.0f - alpha) + lumaB * alpha),
             255}
        );
    }
    return output;
}

} // namespace IRRender
#endif /* COMPARISON_IMAGE_H */
