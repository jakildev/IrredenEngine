#include <irreden/video/video_manager.hpp>

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/audio/audio_capture_source.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_utility.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>

#include "video_backend.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
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
    int audioChannels,
    int audioBitrate,
    bool audioMuxEnabled,
    bool audioWavEnabled,
    double audioSyncOffsetMs,
    IRAudio::IAudioCaptureSource *audioCaptureSource
) {
    m_outputFilePath = outputFilePath;
    m_targetFps = std::max(targetFps, 1);
    m_videoBitrate = std::max(videoBitrate, 250000);
    m_captureAudioInput = captureAudioInput;
    m_audioInputDeviceName = audioInputDeviceName;
    m_audioSampleRate = std::max(audioSampleRate, 8'000);
    m_audioChannels = std::clamp(audioChannels, 1, 2);
    m_audioBitrate = std::max(audioBitrate, 64'000);
    m_audioMuxEnabled = audioMuxEnabled;
    m_audioWavEnabled = audioWavEnabled;
    m_audioSyncOffsetMs = audioSyncOffsetMs;
    m_audioCaptureSource = audioCaptureSource;
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
    m_pendingScreenshotShotLabel.clear();
    m_pendingScreenshotCrops.clear();
}

void VideoManager::requestScreenshotWithCrops(
    const char *shotLabel,
    const RoiCrop *crops,
    int numCrops
) {
    m_screenshotRequested = true;
    m_pendingScreenshotShotLabel = (shotLabel != nullptr) ? shotLabel : "";
    m_pendingScreenshotCrops.clear();
    if (crops == nullptr || numCrops <= 0) {
        return;
    }
    m_pendingScreenshotCrops.reserve(static_cast<std::size_t>(numCrops));
    for (int i = 0; i < numCrops; ++i) {
        const RoiCrop &c = crops[i];
        PendingCrop pc;
        pc.x_ = c.x_;
        pc.y_ = c.y_;
        pc.w_ = c.w_;
        pc.h_ = c.h_;
        pc.label_ = (c.label_ != nullptr) ? c.label_ : "crop";
        m_pendingScreenshotCrops.push_back(std::move(pc));
    }
}

void VideoManager::requestCanvasScreenshot() {
    m_canvasScreenshotRequested = true;
}

void VideoManager::notifyFixedUpdate() {
    if (m_captureEnabled) {
        ++m_totalFixedUpdates;
    }
}

void VideoManager::render() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_VIDEO);
    joinFinalizeThreadIfDone();

    if (m_screenshotRequested) {
        m_screenshotRequested = false;
        captureScreenshot();
    }

    if (m_canvasScreenshotRequested) {
        m_canvasScreenshotRequested = false;
        captureCanvasScreenshot();
    }

    if (m_toggleRequested) {
        m_toggleRequested = false;
        toggleCapture();
    }

    if (!m_captureEnabled) {
        return;
    }

    const int64_t expectedFrames =
        m_totalFixedUpdates * static_cast<int64_t>(m_targetFps) /
        static_cast<int64_t>(IRConstants::kFPS);
    const int framesToCapture =
        static_cast<int>(expectedFrames - m_totalCapturedFrames);

    if (framesToCapture <= 0) {
        return;
    }

    if (!captureFrame()) {
        stopRecording();
        m_captureEnabled = false;
        return;
    }
    ++m_totalCapturedFrames;

    static constexpr int kMaxCatchupFrames = 4;
    const int duplicates = std::min(framesToCapture - 1, kMaxCatchupFrames);
    if (duplicates > 0) {
        IRE_LOG_WARN(
            "Video capture: {} frames due, submitting {} duplicate(s) for catchup",
            framesToCapture, duplicates
        );
    }
    for (int i = 0; i < duplicates; ++i) {
        if (!encodeCurrentRawFrame()) {
            stopRecording();
            m_captureEnabled = false;
            return;
        }
        ++m_totalCapturedFrames;
    }
    if (framesToCapture > duplicates + 1) {
        m_totalCapturedFrames += (framesToCapture - duplicates - 1);
    }
}

void VideoManager::shutdown() {
    const bool wasRecording = m_captureEnabled.exchange(false);
    if (wasRecording) {
        IRE_LOG_INFO("VideoManager::shutdown() -- recording was active, finalizing.");
    }

    if (m_audioInputArmed) {
        if (m_audioCaptureSource != nullptr) {
            m_audioCaptureSource->stopCapture();
        } else {
            IRAudio::stopAudioInputCapture();
        }
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
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_AUDIO);
    if (m_audioInputArmed) {
        return;
    }
    IRAudio::AudioCaptureCallback audioCallback = [this](
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

    bool started = false;
    if (m_audioCaptureSource != nullptr) {
        IRAudio::AudioCaptureConfig captureConfig;
        captureConfig.device_name_ = m_audioInputDeviceName;
        captureConfig.sample_rate_ = m_audioSampleRate;
        captureConfig.channels_ = m_audioChannels;
        started = m_audioCaptureSource->startCapture(captureConfig, std::move(audioCallback));
    } else {
        started = IRAudio::startAudioInputCapture(
            m_audioInputDeviceName,
            m_audioSampleRate,
            m_audioChannels,
            std::move(audioCallback)
        );
    }

    if (started) {
        m_audioInputArmed = true;
        IRE_LOG_INFO("Audio input armed (device='{}', rate={}, ch={})",
                     m_audioInputDeviceName.c_str(),
                     m_audioSampleRate,
                     m_audioChannels);
    } else {
        IRE_LOG_WARN(
            "Audio input capture unavailable; video recording will continue without audio. "
            "On macOS, also verify microphone permission for the host app."
        );
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
    config.audio_bitrate_ = m_audioBitrate;
    config.audio_mux_enabled_ = m_audioMuxEnabled;
    config.audio_wav_enabled_ = m_audioWavEnabled;

    {
        double audioDeviceLatencyMs = 0.0;
        if (m_audioCaptureSource != nullptr && m_audioInputArmed) {
            audioDeviceLatencyMs = m_audioCaptureSource->getInputLatencyMs();
        }
        const double autoOffsetMs = -audioDeviceLatencyMs;
        const double totalOffsetMs = autoOffsetMs + m_audioSyncOffsetMs;
        config.audio_sync_offset_ms_ = totalOffsetMs;
        IRE_LOG_INFO(
            "A/V sync: audio device latency={:.1f}ms, "
            "auto offset={:.1f}ms, manual trim={:.1f}ms, total={:.1f}ms",
            audioDeviceLatencyMs, autoOffsetMs, m_audioSyncOffsetMs, totalOffsetMs
        );
    }

    if (!startRecording(config)) {
        return;
    }

    m_sourceFrameWidth = sourceResolution.x;
    m_sourceFrameHeight = sourceResolution.y;
    m_sourceFrameBytes = static_cast<std::size_t>(m_sourceFrameWidth) *
                         static_cast<std::size_t>(m_sourceFrameHeight) * 4U;
    m_rawFrameBuffer.assign(m_sourceFrameBytes, 0);
    initReadbackPbos();
    m_totalFixedUpdates = 0;
    m_totalCapturedFrames = 0;
    m_captureEnabled = true;
    if (m_audioInputArmed) {
        IRE_LOG_INFO("Audio capture recording started.");
    }
}

std::uint64_t VideoManager::reserveNextScreenshotIndex() {
    std::filesystem::create_directories(m_screenshotOutputDirPath);
    for (;;) {
        const std::uint64_t idx = m_nextScreenshotIndex;
        ++m_nextScreenshotIndex;
        if (!std::filesystem::exists(screenshotFilePathForIndex(idx))) {
            return idx;
        }
    }
}

std::string VideoManager::screenshotFilePathForIndex(std::uint64_t index) const {
    return (std::filesystem::path(m_screenshotOutputDirPath) /
            IRUtility::formatNumberedFilename("screenshot_", index, 6, ".png"))
        .string();
}

void VideoManager::writePendingRoiCrops(
    const std::uint8_t *frameData,
    int frameWidth,
    int frameHeight,
    std::uint64_t shotIndex
) {
    if (m_pendingScreenshotCrops.empty()) {
        return;
    }
    const std::string indexStr = IRUtility::formatNumberedFilename("", shotIndex, 6, "");
    const std::filesystem::path outputDir(m_screenshotOutputDirPath);
    for (const PendingCrop &crop : m_pendingScreenshotCrops) {
        const int x0 = std::max(0, crop.x_);
        const int y0 = std::max(0, crop.y_);
        const int x1 = std::min(frameWidth, crop.x_ + crop.w_);
        const int y1 = std::min(frameHeight, crop.y_ + crop.h_);
        const int outW = x1 - x0;
        const int outH = y1 - y0;
        if (outW <= 0 || outH <= 0) {
            IRE_LOG_WARN(
                "ROI crop '{}' for shot '{}' degenerate after clamp (rect={},{} {}x{} -> {}x{}); skipping",
                crop.label_, m_pendingScreenshotShotLabel,
                crop.x_, crop.y_, crop.w_, crop.h_, outW, outH
            );
            continue;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(outW) * 4U;
        std::vector<std::uint8_t> cropData(rowBytes * static_cast<std::size_t>(outH));
        for (int y = 0; y < outH; ++y) {
            const std::uint8_t *src =
                frameData +
                (static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(frameWidth) +
                 static_cast<std::size_t>(x0)) *
                    4U;
            std::memcpy(cropData.data() + static_cast<std::size_t>(y) * rowBytes, src, rowBytes);
        }
        const std::string filename =
            "screenshot_" + indexStr + "_" + m_pendingScreenshotShotLabel +
            "__crop_" + crop.label_ + ".png";
        const std::string path = (outputDir / filename).string();
        IRRender::writePNG(path.c_str(), outW, outH, 4, cropData.data());
        IRE_LOG_INFO("Saved ROI crop: {}", path);
    }
}

bool VideoManager::captureScreenshot() {
    // Read from the default framebuffer (final screen output) so we capture
    // the framebuffer-to-screen pass, debug overlay, HUD, and all other layers.
    ivec2 sourceResolution{0, 0};
    IRWindow::getFramebufferSize(sourceResolution);
    if (sourceResolution.x <= 0 || sourceResolution.y <= 0) {
        IRE_LOG_WARN("Screenshot skipped: framebuffer has zero or negative dimensions");
        return false;
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(sourceResolution.x) * static_cast<std::size_t>(sourceResolution.y);
    std::vector<std::uint8_t> imageData(pixelCount * 4U);
    const bool didCapture = IRRender::readDefaultFramebuffer(
        0,
        0,
        sourceResolution.x,
        sourceResolution.y,
        imageData.data()
    );
    if (!didCapture) {
        IRE_LOG_WARN("Default framebuffer screenshot readback is not available for the active backend.");
        return false;
    }

    const std::uint64_t shotIndex = reserveNextScreenshotIndex();
    const std::string outputPath = screenshotFilePathForIndex(shotIndex);
    IRRender::writePNG(
        outputPath.c_str(),
        sourceResolution.x,
        sourceResolution.y,
        4,
        imageData.data()
    );
    IRE_LOG_INFO("Saved screenshot: {}", outputPath);

    writePendingRoiCrops(
        imageData.data(),
        sourceResolution.x,
        sourceResolution.y,
        shotIndex
    );
    m_pendingScreenshotShotLabel.clear();
    m_pendingScreenshotCrops.clear();
    return true;
}

bool VideoManager::captureCanvasScreenshot() {
    // Read directly from the main canvas framebuffer texture (no debug overlay, no HUD).
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
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
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

    const std::string outputPath = screenshotFilePathForIndex(reserveNextScreenshotIndex());
    IRRender::writePNG(
        outputPath.c_str(),
        sourceResolution.x,
        sourceResolution.y,
        4,
        flippedImageData.data()
    );
    IRE_LOG_INFO("Saved canvas screenshot: {}", outputPath);
    return true;
}

bool VideoManager::captureFrame() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_VIDEO);
    const auto &framebuffer = IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>(
        IREntity::getEntity("mainFramebuffer")
    );

    const CaptureFrameStatus captureStatus = IRVideo::captureFrame(
        framebuffer.framebuffer_.second->getTextureColor(),
        m_sourceFrameWidth,
        m_sourceFrameHeight,
        m_sourceFrameBytes,
        m_rawFrameBuffer,
        m_readbackPbos,
        m_pboWriteIndex,
        m_pboPrimingFrames
    );
    if (captureStatus == CaptureFrameStatus::FAILED) {
        return false;
    }
    if (captureStatus == CaptureFrameStatus::PRIMING) {
        return true;
    }
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
    std::lock_guard<std::mutex> lock(m_recorderMutex);
    auto buf = m_videoRecorder.acquireFrameBuffer(m_sourceFrameBytes);
    std::memcpy(buf.data(), m_rawFrameBuffer.data(), m_sourceFrameBytes);
    return m_videoRecorder.submitVideoFrame(std::move(buf));
}

void VideoManager::initReadbackPbos() {
    IRVideo::initReadback(m_sourceFrameBytes, m_readbackPbos, m_pboWriteIndex, m_pboPrimingFrames);
}

void VideoManager::releaseReadbackPbos() {
    IRVideo::releaseReadback(m_readbackPbos, m_pboWriteIndex, m_pboPrimingFrames);
}

} // namespace IRVideo
