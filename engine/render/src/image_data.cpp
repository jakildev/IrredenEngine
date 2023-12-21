/*
 * Project: Irreden Engine
 * File: image_data.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>

#include <irreden/render/image_data.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace IRRender {
    ImageData::ImageData(const char* file) {
        data_ = stbi_load(
            file,
            &width_,
            &height_,
            &nrChannels_,
            4
        );
        IR_ASSERT(this->data_, "Failed to load image file: {}", file);
        IRE_LOG_INFO(
            "Loaded image width={}, height={}, channels={}",
            width_,
            height_,
            nrChannels_
        );
    }

    ImageData::~ImageData() {
        IRE_LOG_INFO("Freeing image data");
        stbi_image_free(data_);
    }

    Color ImageData::getPixel(unsigned int x, unsigned int y) const {
        IR_ASSERT(x < width_, "x out of bounds");
        IR_ASSERT(y < height_, "y out of bounds");
        unsigned int index = (y * width_ + x) * 4;
        return Color(
            data_[index],
            data_[index + 1],
            data_[index + 2],
            data_[index + 3]
        );
    }

    std::vector<Color> createColorPaletteFromFile(const char* filename) {
        std::vector<Color> res;
        IRRender::ImageData d{filename};
        for(int y = 0; y < d.height_; ++y) {
            for(int x = 0; x < d.width_; ++x) {
                Color color = d.getPixel(x, y);
                res.push_back(color);
            }
        }
        return res;
    }

    void writePNG(
        const char* filename,
        int width,
        int height,
        int nrChannels,
        const uint8_t* data
    ) {
        int res = stbi_write_png(
            filename,
            width,
            height,
            nrChannels,
            data,
            width * nrChannels
        );
        IR_ASSERT(res, "Failed to write PNG file: {}", filename);
        IRE_LOG_DEBUG("Wrote PNG file: {}", filename);
    }



} // namespace IRRender
