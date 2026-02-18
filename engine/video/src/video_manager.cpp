#include <irreden/video/video_manager.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace IRVideo {

VideoManager::VideoManager() {
    g_videoManager = this;
}

void VideoManager::configureCapture(const std::string &outputFilePath, int targetFps,
                                    int videoBitrate) {
    m_outputFilePath = outputFilePath;
    m_targetFps = std::max(targetFps, 1);
    m_videoBitrate = std::max(videoBitrate, 250000);
}

void VideoManager::toggleRecording() {
    m_toggleRequested = true;
}

void VideoManager::render() {
    joinFinalizeThreadIfDone();

    if (m_toggleRequested) {
        m_toggleRequested = false;
        toggleCapture();
    }

    if (!m_captureEnabled) {
        return;
    }

    const double captureIntervalSeconds = 1.0 / static_cast<double>(m_targetFps);
    m_captureAccumulatorSeconds += IRTime::deltaTime(IRTime::Events::RENDER);

    if (m_captureAccumulatorSeconds < captureIntervalSeconds) {
        return;
    }
    m_captureAccumulatorSeconds = std::fmod(m_captureAccumulatorSeconds, captureIntervalSeconds);

    const ivec2 currentOutputResolution = IRRender::getRenderManager().getOutputResolution();
    if ((currentOutputResolution.x != m_frameWidth || currentOutputResolution.y != m_frameHeight) &&
        !m_loggedResizeWarning) {
        m_loggedResizeWarning = true;
    }

    if (!captureFrame()) {
        stopRecording();
        m_captureEnabled = false;
    }
}

void VideoManager::shutdown() {
    joinFinalizeThreadIfDone();
    if (m_captureEnabled) {
        beginAsyncFinalize();
        m_captureEnabled = false;
    }
    if (m_finalizeThread.joinable()) {
        m_finalizeThread.join();
    }
}

bool VideoManager::startRecording(const VideoRecorderConfig &config) {
    std::lock_guard<std::mutex> lock(m_recorderMutex);
    return m_videoRecorder.start(config);
}

void VideoManager::stopRecording() {
    std::lock_guard<std::mutex> lock(m_recorderMutex);
    m_videoRecorder.stop();
    releaseReadbackPbos();
}

bool VideoManager::recordFrame(const std::uint8_t *rgbaData, int strideBytes) {
    std::lock_guard<std::mutex> lock(m_recorderMutex);
    return m_videoRecorder.submitVideoFrame(rgbaData, strideBytes);
}

bool VideoManager::isRecording() const {
    return m_videoRecorder.isRecording();
}

std::uint64_t VideoManager::getFrameCount() const {
    return m_videoRecorder.getVideoFrameCount();
}

const std::string &VideoManager::getLastError() const {
    return m_videoRecorder.getLastError();
}

void VideoManager::toggleCapture() {
    if (m_finalizeInProgress.load()) {
        return;
    }

    if (m_captureEnabled) {
        beginAsyncFinalize();
        m_captureEnabled = false;
        return;
    }

    const auto &framebuffer =
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>(
            IREntity::getEntity("mainFramebuffer"));
    const ivec2 sourceResolution = framebuffer.getResolution();
    ivec2 outputResolution = IRRender::getRenderManager().getOutputResolution();
    if (outputResolution.x <= 0 || outputResolution.y <= 0) {
        outputResolution = sourceResolution;
    }

    VideoRecorderConfig config;
    config.output_file_path_ = m_outputFilePath;
    config.width_ = outputResolution.x;
    config.height_ = outputResolution.y;
    config.target_fps_ = m_targetFps;
    config.video_bitrate_ = m_videoBitrate;

    if (!startRecording(config)) {
        return;
    }

    m_sourceFrameWidth = sourceResolution.x;
    m_sourceFrameHeight = sourceResolution.y;
    m_frameWidth = config.width_;
    m_frameHeight = config.height_;
    m_sourceFrameBytes = static_cast<std::size_t>(m_sourceFrameWidth) *
                         static_cast<std::size_t>(m_sourceFrameHeight) * 4U;
    const std::size_t targetFrameBytes =
        static_cast<std::size_t>(m_frameWidth) * static_cast<std::size_t>(m_frameHeight) * 4U;
    m_rawFrameBuffer.assign(m_sourceFrameBytes, 0);
    m_uploadFrameBuffer.assign(targetFrameBytes, 0);
    initReadbackPbos();
    m_captureAccumulatorSeconds = 0.0;
    m_loggedResizeWarning = false;
    m_captureEnabled = true;
}

bool VideoManager::captureFrame() {
    const auto &framebuffer =
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>(
            IREntity::getEntity("mainFramebuffer"));

    if (!m_readbackPbos.empty()) {
        const int pboWrite = m_pboWriteIndex;
        const int pboRead = (m_pboWriteIndex + 1) % kReadbackPboCount;

        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPbos[pboWrite]);
        glGetTextureSubImage(framebuffer.framebuffer_.second->getTextureColor().getHandle(), 0, 0, 0, 0,
                             m_sourceFrameWidth, m_sourceFrameHeight, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                             static_cast<GLsizei>(m_sourceFrameBytes), nullptr);

        bool hasCpuFrame = false;
        if (m_pboPrimingFrames >= (kReadbackPboCount - 1)) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPbos[pboRead]);
            void *mappedPtr =
                glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(m_sourceFrameBytes),
                                 GL_MAP_READ_BIT);
            if (mappedPtr != nullptr) {
                std::memcpy(m_rawFrameBuffer.data(), mappedPtr, m_sourceFrameBytes);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                hasCpuFrame = true;
            }
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        m_pboWriteIndex = (m_pboWriteIndex + 1) % kReadbackPboCount;
        if (m_pboPrimingFrames < kReadbackPboCount) {
            ++m_pboPrimingFrames;
        }

        if (!hasCpuFrame) {
            return true;
        }
        return encodeCurrentRawFrame();
    }

    framebuffer.framebuffer_.second->getTextureColor().getSubImage2D(
        0, 0, m_sourceFrameWidth, m_sourceFrameHeight, GL_RGBA, GL_UNSIGNED_BYTE,
        m_rawFrameBuffer.data());

    return encodeCurrentRawFrame();
}

void VideoManager::beginAsyncFinalize() {
    if (m_finalizeInProgress.exchange(true)) {
        return;
    }

    if (m_finalizeThread.joinable()) {
        m_finalizeThread.join();
    }

    m_finalizeThread = std::thread([this]() {
        {
            std::lock_guard<std::mutex> lock(m_recorderMutex);
            m_videoRecorder.stop();
        }
        releaseReadbackPbos();
        m_finalizeInProgress.store(false);
    });
}

void VideoManager::joinFinalizeThreadIfDone() {
    if (m_finalizeThread.joinable() && !m_finalizeInProgress.load()) {
        m_finalizeThread.join();
    }
}

bool VideoManager::encodeCurrentRawFrame() {
    if (m_frameWidth == m_sourceFrameWidth && m_frameHeight == m_sourceFrameHeight) {
        const std::size_t rowBytes = static_cast<std::size_t>(m_frameWidth) * 4U;
        for (int y = 0; y < m_frameHeight; ++y) {
            const int flippedY = m_frameHeight - 1 - y;
            std::memcpy(m_uploadFrameBuffer.data() + static_cast<std::size_t>(y) * rowBytes,
                        m_rawFrameBuffer.data() + static_cast<std::size_t>(flippedY) * rowBytes,
                        rowBytes);
        }
    } else {
        for (int y = 0; y < m_frameHeight; ++y) {
            const int srcY = (y * m_sourceFrameHeight) / m_frameHeight;
            const int flippedSrcY = m_sourceFrameHeight - 1 - srcY;
            for (int x = 0; x < m_frameWidth; ++x) {
                const int srcX = (x * m_sourceFrameWidth) / m_frameWidth;
                const std::size_t srcIndex =
                    (static_cast<std::size_t>(flippedSrcY) * static_cast<std::size_t>(m_sourceFrameWidth) +
                     static_cast<std::size_t>(srcX)) *
                    4U;
                const std::size_t dstIndex =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_frameWidth) +
                     static_cast<std::size_t>(x)) *
                    4U;
                m_uploadFrameBuffer[dstIndex + 0] = m_rawFrameBuffer[srcIndex + 0];
                m_uploadFrameBuffer[dstIndex + 1] = m_rawFrameBuffer[srcIndex + 1];
                m_uploadFrameBuffer[dstIndex + 2] = m_rawFrameBuffer[srcIndex + 2];
                m_uploadFrameBuffer[dstIndex + 3] = m_rawFrameBuffer[srcIndex + 3];
            }
        }
    }

    return recordFrame(m_uploadFrameBuffer.data(), m_frameWidth * 4);
}

void VideoManager::initReadbackPbos() {
    releaseReadbackPbos();
    if (m_sourceFrameBytes == 0) {
        return;
    }

    m_readbackPbos.assign(kReadbackPboCount, 0);
    glGenBuffers(kReadbackPboCount, m_readbackPbos.data());
    for (unsigned int pbo : m_readbackPbos) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(m_sourceFrameBytes), nullptr,
                     GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_pboWriteIndex = 0;
    m_pboPrimingFrames = 0;
}

void VideoManager::releaseReadbackPbos() {
    if (!m_readbackPbos.empty()) {
        glDeleteBuffers(static_cast<GLsizei>(m_readbackPbos.size()), m_readbackPbos.data());
        m_readbackPbos.clear();
    }
    m_pboWriteIndex = 0;
    m_pboPrimingFrames = 0;
}

} // namespace IRVideo
