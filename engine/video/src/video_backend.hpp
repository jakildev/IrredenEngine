#pragma once

#include <irreden/render/texture.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRVideo {

enum class CaptureFrameStatus {
    FAILED,
    PRIMING,
    READY
};

CaptureFrameStatus captureFrame(
    const IRRender::Texture2D &textureColor,
    int sourceFrameWidth,
    int sourceFrameHeight,
    std::size_t sourceFrameBytes,
    std::vector<std::uint8_t> &rawFrameBuffer,
    std::vector<unsigned int> &readbackPbos,
    int &pboWriteIndex,
    int &pboPrimingFrames
);

void initReadback(
    std::size_t sourceFrameBytes,
    std::vector<unsigned int> &readbackPbos,
    int &pboWriteIndex,
    int &pboPrimingFrames
);

void releaseReadback(
    std::vector<unsigned int> &readbackPbos, int &pboWriteIndex, int &pboPrimingFrames
);

} // namespace IRVideo
