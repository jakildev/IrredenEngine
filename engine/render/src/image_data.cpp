#include <irreden/ir_profile.hpp>

#include <irreden/render/image_data.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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
