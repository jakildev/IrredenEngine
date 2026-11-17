#ifndef IMAGE_DATA_H
#define IMAGE_DATA_H

#include <irreden/ir_math.hpp>

#include <cstdint>
#include <string>
#include <vector>

using IRMath::Color;

namespace IRRender {

struct ImageData {
    uint8_t *data_;
    int width_;
    int height_;
    int nrChannels_;
    ImageData(const char *file);
    // Non-asserting load: on failure `data_` is null, the extents are zero and
    // @p error names the file and the decoder's reason.
    ImageData(const char *file, std::string &error);
    ~ImageData();
    ImageData(const ImageData &) = delete;
    ImageData &operator=(const ImageData &) = delete;
    Color getPixel(unsigned int x, unsigned int y) const;
};

// Box-filters an RGBA8 image (row-major, 4 bytes per pixel) into a
// `trixels.x` x `trixels.y` block of GUI-canvas texels, row-major. A texel is
// two pixels wide and one tall on screen, so each texel averages a 2s x s
// block of source pixels, s being the one uniform scale that fits the whole
// image; the image is centred and the letterbox is @p background. Each source
// pixel is composited over @p background by its alpha before averaging, so a
// texel written over a panel never punches through it. Fractional block edges
// weight the straddled pixels by their covered area.
std::vector<Color> boxFilterToTrixels(
    const std::uint8_t *rgba, IRMath::ivec2 imageSize, IRMath::ivec2 trixels, Color background
);

// A mip pyramid packed into one RGBA8 image, for kernels that pick their own
// level with integer texel reads: level 0 at the origin, every smaller level
// stacked top to bottom in a column at x = `baseSize_.x`. Colours are
// premultiplied by alpha, so a linear filter across a transparent edge stays
// exact.
struct MipAtlas {
    std::vector<std::uint8_t> pixels_;
    IRMath::ivec2 atlasSize_ = IRMath::ivec2(0);
    IRMath::ivec2 baseSize_ = IRMath::ivec2(0);
    int levelCount_ = 0;
};

// Each level halves the one above it, rounding down and stopping at one texel.
constexpr IRMath::ivec2 mipLevelSize(IRMath::ivec2 baseSize, int level) {
    const int width = baseSize.x >> level;
    const int height = baseSize.y >> level;
    return IRMath::ivec2(width < 1 ? 1 : width, height < 1 ? 1 : height);
}

// Levels down to the one whose longer side is a single texel.
constexpr int mipLevelCount(IRMath::ivec2 baseSize) {
    int longest = baseSize.x > baseSize.y ? baseSize.x : baseSize.y;
    int levels = 1;
    while (longest > 1) {
        longest >>= 1;
        ++levels;
    }
    return levels;
}

// Where `level`'s top-left texel sits in the atlas. The shader twin is
// `mipAtlasLevelOrigin` in c_canvas_image_to_trixel.
constexpr IRMath::ivec2 mipAtlasLevelOrigin(IRMath::ivec2 baseSize, int level) {
    if (level <= 0) {
        return IRMath::ivec2(0);
    }
    int y = 0;
    for (int above = 1; above < level; ++above) {
        y += mipLevelSize(baseSize, above).y;
    }
    return IRMath::ivec2(baseSize.x, y);
}

// Builds the pyramid of an RGBA8 image (row-major, straight alpha). An image
// whose longer side exceeds @p maxDimension is first box-filtered down to it,
// keeping its aspect; every level is the area average of the one above. Empty
// when the image is.
MipAtlas buildMipAtlas(const std::uint8_t *rgba, IRMath::ivec2 imageSize, int maxDimension);

void writePNG(const char *filename, int width, int height, int nrChannels, const uint8_t *data);
std::vector<Color> createColorPaletteFromFile(const char *filename);
} // namespace IRRender

#endif /* IMAGE_DATA_H */
