/*
 * Project: Irreden Engine
 * File: image_data.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/render/image_data.hpp>
#include <irreden/ir_profile.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace IRRender {
    ImageData::ImageData(const char* file) {
        data_ = stbi_load(
            file,
            &width_,
            &height_,
            &nrChannels_,
            4
        );
        IR_ASSERT(this->data_, "Failed to load image");
        IRProfile::engLogInfo(
            "Loaded image width={}, height={}, channels={}",
            width_,
            height_,
            nrChannels_
        );
    }

    ImageData::~ImageData() {
        IRProfile::engLogInfo("Freeing image data");
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


} // namespace IRRender
