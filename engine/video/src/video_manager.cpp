#include <irreden/video/video_manager.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

namespace IRVideo {

VideoManager::VideoManager() {
    g_videoManager = this;
}

VideoManager::~VideoManager() {
    shutdown();
    if (g_videoManager == this) {
        g_videoManager = nullptr;
    }
}

void VideoManager::configureCapture(
    const std::string &outputFilePath,
    int targetFps,
    int videoBitrate,
    bool captureAudioInput,
    const std::string &audioInputDeviceName,
    int audioSampleRate,
    int audioChannels
) {
    m_outputFilePath = outputFilePath;
    m_targetFps = std::max(targetFps, 1);
    m_videoBitrate = std::max(videoBitrate, 250000);
    m_captureAudioInput = captureAudioInput;
    m_audioInputDeviceName = audioInputDeviceName;
    m_audioSampleRate = std::max(audioSampleRate, 8'000);
    m_audioChannels = std::clamp(audioChannels, 1, 2);
    if (m_captureAudioInput) {
        armAudioInput();
    }
}

void VideoManager::configureScreenshotOutputDir(const std::string &outputDirPath) {
    m_screenshotOutputDirPath = outputDirPath;
}

void VideoManager::toggleRecording() {
    m_toggleRequested = true;
}

void VideoManager::requestScreenshot() {
    m_screenshotRequested = true;
}

void VideoManager::render() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_VIDEO);
    joinFinalizeThreadIfDone();

    if (m_screenshotRequested) {
        m_screenshotRequested = false;
        captureScreenshot();
    }

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

    if (!captureFrame()) {
        stopRecording();
        m_captureEnabled = false;
    }
}

void VideoManager::shutdown() {
    const bool wasRecording = m_captureEnabled.exchange(false);
    if (wasRecording) {
        IRE_LOG_INFO("VideoManager::shutdown() -- recording was active, finalizing.");
    }

    if (m_audioInputArmed) {
        IRAudio::stopAudioInputCapture();
        m_audioInputArmed = false;
    }

    if (m_finalizeThread.joinable()) {
        m_finalizeThread.join();
    }

    if (wasRecording || m_videoRecorder.isRecording()) {
        std::lock_guard<std::mutex> lock(m_recorderMutex);
        m_videoRecorder.stop();
    }

    releaseReadbackPbos();
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

void VideoManager::armAudioInput() {
    if (m_audioInputArmed) {
        return;
    }
    IRAudio::AudioInputCallback audioCallback = [this](
                                                    const float *samples,
                                                    int frameCount,
                                                    double streamTime,
                                                    bool inputOverflow
                                                ) {
        if (!m_captureEnabled) {
            return;
        }
        if (inputOverflow) {
            IRE_LOG_WARN("Audio input overflow detected while recording.");
        }
        if (samples != nullptr && frameCount > 0) {
            m_videoRecorder.submitAudioInputSamples(samples, frameCount, streamTime);
        }
    };
    const bool started = IRAudio::startAudioInputCapture(
        m_audioInputDeviceName,
        m_audioSampleRate,
        m_audioChannels,
        std::move(audioCallback)
    );
    if (started) {
        m_audioInputArmed = true;
        IRE_LOG_INFO("Audio input armed (device='{}', rate={}, ch={})",
                     m_audioInputDeviceName.c_str(),
                     m_audioSampleRate,
                     m_audioChannels);
    } else {
        IRE_LOG_WARN("Audio input capture unavailable; video recording will continue without audio.");
    }
}

void VideoManager::toggleCapture() {
    if (m_finalizeInProgress.load()) {
        return;
    }

    if (m_captureEnabled) {
        IRE_LOG_INFO("Stopping capture (audio + video).");
        beginAsyncFinalize();
        m_captureEnabled = false;
        return;
    }

    const auto &framebuffer = IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>(
        IREntity::getEntity("mainFramebuffer")
    );
    const ivec2 sourceResolution = framebuffer.getResolution();
    ivec2 outputResolution = IRRender::getRenderManager().getOutputResolution();
    if (outputResolution.x <= 0 || outputResolution.y <= 0) {
        outputResolution = IRRender::getRenderManager().getViewport();
    }

    IRE_LOG_INFO("Recording: source={}x{} -> output={}x{}",
                 sourceResolution.x, sourceResolution.y,
                 outputResolution.x, outputResolution.y);

    VideoRecorderConfig config;
    config.output_file_path_ = m_outputFilePath;
    config.width_ = outputResolution.x;
    config.height_ = outputResolution.y;
    config.source_width_ = sourceResolution.x;
    config.source_height_ = sourceResolution.y;
    config.target_fps_ = m_targetFps;
    config.video_bitrate_ = m_videoBitrate;
    config.capture_audio_input_ = m_captureAudioInput;
    config.audio_input_device_name_ = m_audioInputDeviceName;
    config.audio_sample_rate_ = m_audioSampleRate;
    config.audio_channels_ = m_audioChannels;

    if (!startRecording(config)) {
        return;
    }

    m_sourceFrameWidth = sourceResolution.x;
    m_sourceFrameHeight = sourceResolution.y;
    m_sourceFrameBytes = static_cast<std::size_t>(m_sourceFrameWidth) *
                         static_cast<std::size_t>(m_sourceFrameHeight) * 4U;
    m_rawFrameBuffer.assign(m_sourceFrameBytes, 0);
    initReadbackPbos();
    m_captureAccumulatorSeconds = 0.0;
    m_captureEnabled = true;
    if (m_audioInputArmed) {
        IRE_LOG_INFO("Audio capture recording started.");
    }
}

std::string VideoManager::getNextScreenshotFilePath() {
    std::filesystem::path outputDir = std::filesystem::path(m_screenshotOutputDirPath);
    std::filesystem::create_directories(outputDir);

    for (;;) {
        std::ostringstream fileName;
        fileName << "screenshot_" << std::setfill('0') << std::setw(6) << m_nextScreenshotIndex
                 << ".png";
        std::filesystem::path candidatePath = outputDir / fileName.str();
        if (!std::filesystem::exists(candidatePath)) {
            ++m_nextScreenshotIndex;
            return candidatePath.string();
        }
        ++m_nextScreenshotIndex;
    }
}

bool VideoManager::captureScreenshot() {
    const auto &framebuffer = IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>(
        IREntity::getEntity("mainFramebuffer")
    );
    const ivec2 sourceResolution = framebuffer.getResolution();
    const std::size_t pixelCount =
        static_cast<std::size_t>(sourceResolution.x) * static_cast<std::size_t>(sourceResolution.y);
    std::vector<std::uint8_t> imageData(pixelCount * 4U);
    framebuffer.framebuffer_.second->getTextureColor().getSubImage2D(
        0,
        0,
        sourceResolution.x,
        sourceResolution.y,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        imageData.data()
    );

    // OpenGL texture origin is lower-left, flip for PNG output.
    const std::size_t rowBytes = static_cast<std::size_t>(sourceResolution.x) * 4U;
    std::vector<std::uint8_t> flippedImageData(pixelCount * 4U);
    for (int y = 0; y < sourceResolution.y; ++y) {
        const int flippedY = sourceResolution.y - 1 - y;
        std::memcpy(
            flippedImageData.data() + static_cast<std::size_t>(y) * rowBytes,
            imageData.data() + static_cast<std::size_t>(flippedY) * rowBytes,
            rowBytes
        );
    }

    const std::string outputPath = getNextScreenshotFilePath();
    IRRender::writePNG(
        outputPath.c_str(),
        sourceResolution.x,
        sourceResolution.y,
        4,
        flippedImageData.data()
    );
    IRE_LOG_INFO("Saved screenshot: {}", outputPath);
    return true;
}

bool VideoManager::captureFrame() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_VIDEO);
    const auto &framebuffer = IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>(
        IREntity::getEntity("mainFramebuffer")
    );

    if (!m_readbackPbos.empty()) {
        const int pboWrite = m_pboWriteIndex;
        const int pboRead = (m_pboWriteIndex + 1) % kReadbackPboCount;

        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPbos[pboWrite]);
        glGetTextureSubImage(
            framebuffer.framebuffer_.second->getTextureColor().getHandle(),
            0,
            0,
            0,
            0,
            m_sourceFrameWidth,
            m_sourceFrameHeight,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            static_cast<GLsizei>(m_sourceFrameBytes),
            nullptr
        );

        bool hasCpuFrame = false;
        if (m_pboPrimingFrames >= (kReadbackPboCount - 1)) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPbos[pboRead]);
            void *mappedPtr = glMapBufferRange(
                GL_PIXEL_PACK_BUFFER,
                0,
                static_cast<GLsizeiptr>(m_sourceFrameBytes),
                GL_MAP_READ_BIT
            );
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
        0,
        0,
        m_sourceFrameWidth,
        m_sourceFrameHeight,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        m_rawFrameBuffer.data()
    );

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
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_VIDEO);
    // Pass the raw source-resolution PBO data directly to the encoder queue.
    // The worker thread handles resize + flip + color conversion in one
    // sws_scale pass, so the main thread does zero image processing here.
    std::lock_guard<std::mutex> lock(m_recorderMutex);
    auto nextBuf = m_videoRecorder.acquireFrameBuffer(m_sourceFrameBytes);
    std::swap(nextBuf, m_rawFrameBuffer);
    return m_videoRecorder.submitVideoFrame(std::move(nextBuf));
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
        glBufferData(
            GL_PIXEL_PACK_BUFFER,
            static_cast<GLsizeiptr>(m_sourceFrameBytes),
            nullptr,
            GL_STREAM_READ
        );
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
