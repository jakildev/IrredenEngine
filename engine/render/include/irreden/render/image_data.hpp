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

void writePNG(const char *filename, int width, int height, int nrChannels, const uint8_t *data);
std::vector<Color> createColorPaletteFromFile(const char *filename);
} // namespace IRRender

#endif /* IMAGE_DATA_H */
