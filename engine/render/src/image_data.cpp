#include <irreden/ir_profile.hpp>

#include <irreden/render/image_data.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace IRRender {
ImageData::ImageData(const char *file) {
    data_ = stbi_load(file, &width_, &height_, &nrChannels_, 4);
    IR_ASSERT(this->data_, "Failed to load image file: {}", file);
    IRE_LOG_INFO("Loaded image width={}, height={}, channels={}", width_, height_, nrChannels_);
}

ImageData::ImageData(const char *file, std::string &error) {
    data_ = stbi_load(file, &width_, &height_, &nrChannels_, 4);
    if (data_ == nullptr) {
        const char *reason = stbi_failure_reason();
        error = std::string("failed to load image '") + file +
                "': " + (reason != nullptr ? reason : "unknown error");
        width_ = 0;
        height_ = 0;
        nrChannels_ = 0;
    }
}

ImageData::~ImageData() {
    IRE_LOG_INFO("Freeing image data");
    stbi_image_free(data_);
}

Color ImageData::getPixel(unsigned int x, unsigned int y) const {
    IR_ASSERT(x < width_, "x out of bounds");
    IR_ASSERT(y < height_, "y out of bounds");
    unsigned int index = (y * width_ + x) * 4;
    return Color(data_[index], data_[index + 1], data_[index + 2], data_[index + 3]);
}

std::vector<Color> boxFilterToTrixels(
    const std::uint8_t *rgba, IRMath::ivec2 imageSize, IRMath::ivec2 trixels, Color background
) {
    std::vector<Color> out(
        static_cast<std::size_t>(IRMath::max(trixels.x, 0)) *
            static_cast<std::size_t>(IRMath::max(trixels.y, 0)),
        background
    );
    if (rgba == nullptr || imageSize.x <= 0 || imageSize.y <= 0 || out.empty()) {
        return out;
    }

    const double scale = IRMath::max(
        static_cast<double>(imageSize.x) / (2.0 * trixels.x),
        static_cast<double>(imageSize.y) / trixels.y
    );
    const double blockW = 2.0 * scale;
    const double blockH = scale;
    const double blockArea = blockW * blockH;
    const double offsetX = (trixels.x - imageSize.x / blockW) * 0.5;
    const double offsetY = (trixels.y - imageSize.y / blockH) * 0.5;
    const double bg[4] = {
        static_cast<double>(background.red_),
        static_cast<double>(background.green_),
        static_cast<double>(background.blue_),
        static_cast<double>(background.alpha_),
    };

    for (int ty = 0; ty < trixels.y; ++ty) {
        const double y0 = IRMath::max((ty - offsetY) * blockH, 0.0);
        const double y1 = IRMath::min((ty + 1 - offsetY) * blockH, double(imageSize.y));
        if (y1 <= y0) {
            continue;
        }
        for (int tx = 0; tx < trixels.x; ++tx) {
            const double x0 = IRMath::max((tx - offsetX) * blockW, 0.0);
            const double x1 = IRMath::min((tx + 1 - offsetX) * blockW, double(imageSize.x));
            if (x1 <= x0) {
                continue;
            }
            double sum[4] = {0.0, 0.0, 0.0, 0.0};
            double covered = 0.0;
            for (int py = static_cast<int>(IRMath::floor(y0)); py < IRMath::ceil(y1); ++py) {
                const double wy = IRMath::min(py + 1.0, y1) - IRMath::max(double(py), y0);
                const std::uint8_t *row = rgba + static_cast<std::size_t>(py) * imageSize.x * 4;
                for (int px = static_cast<int>(IRMath::floor(x0)); px < IRMath::ceil(x1); ++px) {
                    const double weight =
                        wy * (IRMath::min(px + 1.0, x1) - IRMath::max(double(px), x0));
                    const std::uint8_t *pixel = row + static_cast<std::size_t>(px) * 4;
                    const double alpha = pixel[3] / 255.0;
                    for (int c = 0; c < 3; ++c) {
                        sum[c] += weight * (bg[c] + alpha * (pixel[c] - bg[c]));
                    }
                    sum[3] += weight * (bg[3] + alpha * (255.0 - bg[3]));
                    covered += weight;
                }
            }
            std::uint8_t channels[4];
            for (int c = 0; c < 4; ++c) {
                const double mean = (sum[c] + (blockArea - covered) * bg[c]) / blockArea;
                channels[c] = static_cast<std::uint8_t>(
                    IRMath::clamp(static_cast<int>(IRMath::floor(mean + 0.5)), 0, 255)
                );
            }
            out[static_cast<std::size_t>(ty) * trixels.x + tx] =
                Color{channels[0], channels[1], channels[2], channels[3]};
        }
    }
    return out;
}

namespace {

// One output texel's source span along an axis: the first source texel it
// covers and how much of each covered texel it takes.
struct AxisSpan {
    int first_ = 0;
    int count_ = 0;
    float weights_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Spans that area-average `sourceLength` texels into `spans.size()` texels.
// A ratio above 3 would need more than four taps per texel; callers halve in
// steps so it never does.
void buildAxisSpans(int sourceLength, std::vector<AxisSpan> &spans) {
    const double ratio = static_cast<double>(sourceLength) / static_cast<double>(spans.size());
    IR_ASSERT(ratio <= 3.0, "an area-average step may shrink an axis by at most 3x");
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const double begin = static_cast<double>(i) * ratio;
        const double end = IRMath::min(begin + ratio, static_cast<double>(sourceLength));
        AxisSpan &span = spans[i];
        span.first_ = static_cast<int>(IRMath::floor(begin));
        span.count_ = 0;
        for (int texel = span.first_; texel < sourceLength && texel < end && span.count_ < 4;
             ++texel) {
            const double covered =
                IRMath::min(texel + 1.0, end) - IRMath::max(static_cast<double>(texel), begin);
            if (covered <= 0.0) {
                continue;
            }
            span.weights_[span.count_++] = static_cast<float>(covered / ratio);
        }
    }
}

// Area-averages premultiplied RGBA8 `source` into `target`, whose rows start
// `targetStride` bytes apart.
void areaAverage(
    const std::uint8_t *source,
    IRMath::ivec2 sourceSize,
    int sourceStride,
    std::uint8_t *target,
    IRMath::ivec2 targetSize,
    int targetStride,
    std::vector<AxisSpan> &spansX,
    std::vector<AxisSpan> &spansY
) {
    spansX.resize(static_cast<std::size_t>(targetSize.x));
    spansY.resize(static_cast<std::size_t>(targetSize.y));
    buildAxisSpans(sourceSize.x, spansX);
    buildAxisSpans(sourceSize.y, spansY);
    for (int y = 0; y < targetSize.y; ++y) {
        const AxisSpan &spanY = spansY[static_cast<std::size_t>(y)];
        std::uint8_t *out = target + static_cast<std::size_t>(y) * targetStride;
        for (int x = 0; x < targetSize.x; ++x, out += 4) {
            const AxisSpan &spanX = spansX[static_cast<std::size_t>(x)];
            float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int row = 0; row < spanY.count_; ++row) {
                const std::uint8_t *in =
                    source + static_cast<std::size_t>(spanY.first_ + row) * sourceStride +
                    static_cast<std::size_t>(spanX.first_) * 4;
                for (int column = 0; column < spanX.count_; ++column, in += 4) {
                    const float weight = spanY.weights_[row] * spanX.weights_[column];
                    sum[0] += weight * in[0];
                    sum[1] += weight * in[1];
                    sum[2] += weight * in[2];
                    sum[3] += weight * in[3];
                }
            }
            for (int channel = 0; channel < 4; ++channel) {
                out[channel] = static_cast<std::uint8_t>(
                    IRMath::clamp(static_cast<int>(sum[channel] + 0.5f), 0, 255)
                );
            }
        }
    }
}

// `size` with its longer side cut to `maxDimension`, aspect kept.
IRMath::ivec2 fitWithin(IRMath::ivec2 size, int maxDimension) {
    const int longest = IRMath::max(size.x, size.y);
    if (longest <= maxDimension) {
        return size;
    }
    const double scale = static_cast<double>(maxDimension) / longest;
    return IRMath::ivec2(
        IRMath::max(1, static_cast<int>(IRMath::floor(size.x * scale + 0.5))),
        IRMath::max(1, static_cast<int>(IRMath::floor(size.y * scale + 0.5)))
    );
}

} // namespace

MipAtlas buildMipAtlas(const std::uint8_t *rgba, IRMath::ivec2 imageSize, int maxDimension) {
    MipAtlas atlas;
    if (rgba == nullptr || imageSize.x <= 0 || imageSize.y <= 0 || maxDimension <= 0) {
        return atlas;
    }

    std::vector<std::uint8_t> premultiplied(
        static_cast<std::size_t>(imageSize.x) * static_cast<std::size_t>(imageSize.y) * 4
    );
    for (std::size_t i = 0; i < premultiplied.size(); i += 4) {
        const unsigned alpha = rgba[i + 3];
        premultiplied[i] = static_cast<std::uint8_t>((rgba[i] * alpha + 127u) / 255u);
        premultiplied[i + 1] = static_cast<std::uint8_t>((rgba[i + 1] * alpha + 127u) / 255u);
        premultiplied[i + 2] = static_cast<std::uint8_t>((rgba[i + 2] * alpha + 127u) / 255u);
        premultiplied[i + 3] = static_cast<std::uint8_t>(alpha);
    }

    std::vector<AxisSpan> spansX;
    std::vector<AxisSpan> spansY;
    // Halve until one last step of at most 2x reaches the fitted size.
    const IRMath::ivec2 fitted = fitWithin(imageSize, maxDimension);
    IRMath::ivec2 size = imageSize;
    std::vector<std::uint8_t> halved;
    while (size != fitted) {
        const IRMath::ivec2 next(
            IRMath::max(fitted.x, (size.x + 1) / 2),
            IRMath::max(fitted.y, (size.y + 1) / 2)
        );
        halved.resize(static_cast<std::size_t>(next.x) * static_cast<std::size_t>(next.y) * 4);
        areaAverage(
            premultiplied.data(),
            size,
            size.x * 4,
            halved.data(),
            next,
            next.x * 4,
            spansX,
            spansY
        );
        premultiplied.swap(halved);
        size = next;
    }

    atlas.baseSize_ = size;
    atlas.levelCount_ = mipLevelCount(size);
    const IRMath::ivec2 lastOrigin = mipAtlasLevelOrigin(size, atlas.levelCount_ - 1);
    const IRMath::ivec2 lastSize = mipLevelSize(size, atlas.levelCount_ - 1);
    atlas.atlasSize_ = IRMath::ivec2(
        atlas.levelCount_ > 1 ? size.x + mipLevelSize(size, 1).x : size.x,
        IRMath::max(size.y, lastOrigin.y + lastSize.y)
    );
    const int stride = atlas.atlasSize_.x * 4;
    atlas.pixels_.assign(
        static_cast<std::size_t>(stride) * static_cast<std::size_t>(atlas.atlasSize_.y),
        0
    );
    for (int y = 0; y < size.y; ++y) {
        std::copy_n(
            premultiplied.data() + static_cast<std::size_t>(y) * size.x * 4,
            static_cast<std::size_t>(size.x) * 4,
            atlas.pixels_.data() + static_cast<std::size_t>(y) * stride
        );
    }
    for (int level = 1; level < atlas.levelCount_; ++level) {
        const IRMath::ivec2 aboveOrigin = mipAtlasLevelOrigin(size, level - 1);
        const IRMath::ivec2 origin = mipAtlasLevelOrigin(size, level);
        const auto texelAt = [&atlas, stride](IRMath::ivec2 at) {
            return atlas.pixels_.data() + static_cast<std::size_t>(at.y) * stride +
                   static_cast<std::size_t>(at.x) * 4;
        };
        areaAverage(
            texelAt(aboveOrigin),
            mipLevelSize(size, level - 1),
            stride,
            texelAt(origin),
            mipLevelSize(size, level),
            stride,
            spansX,
            spansY
        );
    }
    return atlas;
}

std::vector<Color> createColorPaletteFromFile(const char *filename) {
    std::vector<Color> res;
    IRRender::ImageData d{filename};
    for (int y = 0; y < d.height_; ++y) {
        for (int x = 0; x < d.width_; ++x) {
            Color color = d.getPixel(x, y);
            res.push_back(color);
        }
    }
    return res;
}

void writePNG(const char *filename, int width, int height, int nrChannels, const uint8_t *data) {
    int res = stbi_write_png(filename, width, height, nrChannels, data, width * nrChannels);
    IR_ASSERT(res, "Failed to write PNG file: {}", filename);
    IRE_LOG_DEBUG("Wrote PNG file: {}", filename);
}

} // namespace IRRender
