#include "../video_backend.hpp"

namespace IRVideo {

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
    textureColor.getSubImage2D(
        0,
        0,
        sourceFrameWidth,
        sourceFrameHeight,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        rawFrameBuffer.data()
    );
    return CaptureFrameStatus::READY;
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
