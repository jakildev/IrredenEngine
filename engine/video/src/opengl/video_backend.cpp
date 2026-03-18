#include "../video_backend.hpp"

#include <irreden/render/opengl/opengl_types.hpp>

#include <cstring>

namespace IRVideo {

namespace {
constexpr int kReadbackPboCount = 3;
}

CaptureFrameStatus captureFrame(
    const IRRender::Texture2D &textureColor,
    int sourceFrameWidth,
    int sourceFrameHeight,
    std::size_t sourceFrameBytes,
    std::vector<std::uint8_t> &rawFrameBuffer,
    std::vector<unsigned int> &readbackPbos,
    int &pboWriteIndex,
    int &pboPrimingFrames
) {
    if (!readbackPbos.empty()) {
        const int pboWrite = pboWriteIndex;
        const int pboRead = (pboWriteIndex + 1) % kReadbackPboCount;

        glBindBuffer(GL_PIXEL_PACK_BUFFER, readbackPbos[pboWrite]);
        glGetTextureSubImage(
            textureColor.getHandle(),
            0,
            0,
            0,
            0,
            sourceFrameWidth,
            sourceFrameHeight,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            static_cast<GLsizei>(sourceFrameBytes),
            nullptr
        );

        bool hasCpuFrame = false;
        if (pboPrimingFrames >= (kReadbackPboCount - 1)) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, readbackPbos[pboRead]);
            void *mappedPtr = glMapBufferRange(
                GL_PIXEL_PACK_BUFFER,
                0,
                static_cast<GLsizeiptr>(sourceFrameBytes),
                GL_MAP_READ_BIT
            );
            if (mappedPtr != nullptr) {
                std::memcpy(rawFrameBuffer.data(), mappedPtr, sourceFrameBytes);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                hasCpuFrame = true;
            }
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        pboWriteIndex = (pboWriteIndex + 1) % kReadbackPboCount;
        if (pboPrimingFrames < kReadbackPboCount) {
            ++pboPrimingFrames;
        }

        return hasCpuFrame ? CaptureFrameStatus::READY : CaptureFrameStatus::PRIMING;
    }

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

void initReadback(
    std::size_t sourceFrameBytes,
    std::vector<unsigned int> &readbackPbos,
    int &pboWriteIndex,
    int &pboPrimingFrames
) {
    releaseReadback(readbackPbos, pboWriteIndex, pboPrimingFrames);
    if (sourceFrameBytes == 0) {
        return;
    }

    readbackPbos.assign(kReadbackPboCount, 0);
    glGenBuffers(kReadbackPboCount, readbackPbos.data());
    for (unsigned int pbo : readbackPbos) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(
            GL_PIXEL_PACK_BUFFER,
            static_cast<GLsizeiptr>(sourceFrameBytes),
            nullptr,
            GL_STREAM_READ
        );
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pboWriteIndex = 0;
    pboPrimingFrames = 0;
}

void releaseReadback(
    std::vector<unsigned int> &readbackPbos, int &pboWriteIndex, int &pboPrimingFrames
) {
    if (!readbackPbos.empty()) {
        glDeleteBuffers(static_cast<GLsizei>(readbackPbos.size()), readbackPbos.data());
        readbackPbos.clear();
    }
    pboWriteIndex = 0;
    pboPrimingFrames = 0;
}

} // namespace IRVideo
