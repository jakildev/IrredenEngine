/*
 * Project: Irreden Engine
 * File: image_data.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IMAGE_DATA_H
#define IMAGE_DATA_H

#include <irreden/ir_math.hpp>

using IRMath::Color;

namespace IRRender {

    struct ImageData {
        uint8_t* data_;
        int width_;
        int height_;
        int nrChannels_;
        ImageData(const char* file);
        ~ImageData();
        Color getPixel(unsigned int x, unsigned int y) const;

    };

    void writePNG(
        const char* filename,
        int width,
        int height,
        int nrChannels,
        const uint8_t* data
    );
    std::vector<Color> createColorPaletteFromFile(const char* filename);
}

#endif /* IMAGE_DATA_H */
