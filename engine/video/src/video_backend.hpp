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

// Reads the RGBA8 image `textureColor` holds once every draw issued so far
// this frame has landed. `Texture2D::getSubImage2D` alone does not promise
// that: on Metal it reads the texture's storage, which this frame's draws
// reach only when their command buffer completes.
void readFrameImage(
    const IRRender::Texture2D &textureColor, int width, int height, std::uint8_t *rgbaData
);

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
