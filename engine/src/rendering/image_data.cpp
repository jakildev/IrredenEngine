/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\image_data.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "image_data.hpp"
#include "../profiling/logger_spd.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../foreign/stb/stb_image.h"

namespace IRRendering {
    ImageData::ImageData(const char* file) {
        data_ = stbi_load(
            file,
            &width_,
            &height_,
            &nrChannels_,
            4
        );
        ENG_ASSERT(this->data_, "Failed to load image");
        ENG_LOG_INFO(
            "Loaded image width={}, height={}, channels={}",
            width_,
            height_,
            nrChannels_
        );
    }

    ImageData::~ImageData() {
        ENG_LOG_INFO("Freeing image data");
        stbi_image_free(data_);
    }

    Color ImageData::getPixel(unsigned int x, unsigned int y) const {
        ENG_ASSERT(x < width_, "x out of bounds");
        ENG_ASSERT(y < height_, "y out of bounds");
        unsigned int index = (y * width_ + x) * 4;
        return Color(
            data_[index],
            data_[index + 1],
            data_[index + 2],
            data_[index + 3]
        );
    }

} // namespace IRRendering
