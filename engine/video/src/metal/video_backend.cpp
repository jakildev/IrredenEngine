#include "../video_backend.hpp"

#include <irreden/render/render_device.hpp>

namespace IRVideo {

void readFrameImage(
    const IRRender::Texture2D &textureColor, int width, int height, std::uint8_t *rgbaData
) {
    // The frame's draws are encoded into a command buffer that present()
    // commits after the capture runs, and getBytes waits for nothing.
    IRRender::device()->finish();
    textureColor.getSubImage2D(
        0,
        0,
        width,
        height,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        rgbaData
    );
}

CaptureFrameStatus captureFrame(
    const IRRender::Texture2D &textureColor,
    int sourceFrameWidth,
    int sourceFrameHeight,
    std::size_t,
    std::vector<std::uint8_t> &rawFrameBuffer,
    std::vector<unsigned int> &,
    int &,
    int &
) {
    readFrameImage(textureColor, sourceFrameWidth, sourceFrameHeight, rawFrameBuffer.data());
    return CaptureFrameStatus::READY;
}

bool readPendingFrame(
    std::size_t, std::vector<std::uint8_t> &, std::vector<unsigned int> &, int, int &
) {
    return false;
}

void initReadback(std::size_t, std::vector<unsigned int> &, int &pboWriteIndex, int &pboPrimingFrames) {
    pboWriteIndex = 0;
    pboPrimingFrames = 0;
}

void releaseReadback(std::vector<unsigned int> &readbackPbos, int &pboWriteIndex, int &pboPrimingFrames) {
    readbackPbos.clear();
    pboWriteIndex = 0;
    pboPrimingFrames = 0;
}

} // namespace IRVideo
