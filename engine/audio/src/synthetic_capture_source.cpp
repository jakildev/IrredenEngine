#include <irreden/audio/synthetic_capture_source.hpp>

#include <irreden/ir_math.hpp>

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace IRAudio {

namespace {

constexpr int kBufferFrames = 1024;
constexpr float kToneHz = 440.0f;
constexpr float kAmplitude = 0.1f;

} // namespace

SyntheticAudioCaptureSource::~SyntheticAudioCaptureSource() {
    stopCapture();
}

bool SyntheticAudioCaptureSource::startCapture(
    const AudioCaptureConfig &config, AudioCaptureCallback callback
) {
    if (m_capturing || config.sample_rate_ <= 0 || config.channels_ <= 0 || !callback) {
        return false;
    }
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
    m_captureSampleRate = config.sample_rate_;
    m_capturing = true;
    m_captureThread =
        std::thread(&SyntheticAudioCaptureSource::captureLoop, this, config, std::move(callback));
    return true;
}

void SyntheticAudioCaptureSource::stopCapture() {
    m_capturing = false;
    m_wakeCondition.notify_all();
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
    m_captureSampleRate = 0;
}

bool SyntheticAudioCaptureSource::isCapturing() const {
    return m_capturing;
}

int SyntheticAudioCaptureSource::getCaptureSampleRate() const {
    return m_captureSampleRate;
}

double SyntheticAudioCaptureSource::getInputLatencyMs() const {
    return 0.0;
}

void SyntheticAudioCaptureSource::captureLoop(
    AudioCaptureConfig config, AudioCaptureCallback callback
) {
    using Clock = std::chrono::steady_clock;
    const auto startedAt = Clock::now();
    std::int64_t deliveredFrames = 0;
    std::vector<float> samples(static_cast<std::size_t>(kBufferFrames * config.channels_));

    while (m_capturing) {
        const std::int64_t nextDeliveredFrames = deliveredFrames + kBufferFrames;
        const auto deadline =
            startedAt +
            std::chrono::nanoseconds(nextDeliveredFrames * 1'000'000'000LL / config.sample_rate_);
        std::unique_lock<std::mutex> lock(m_wakeMutex);
        if (m_wakeCondition.wait_until(lock, deadline, [this]() { return !m_capturing; })) {
            break;
        }
        lock.unlock();

        for (int frame = 0; frame < kBufferFrames; ++frame) {
            const float time = static_cast<float>(deliveredFrames + frame) /
                               static_cast<float>(config.sample_rate_);
            const float sample = kAmplitude * IRMath::sin(IRMath::kTwoPi * kToneHz * time);
            for (int channel = 0; channel < config.channels_; ++channel) {
                samples[static_cast<std::size_t>(frame * config.channels_ + channel)] = sample;
            }
        }
        const double streamTime = static_cast<double>(deliveredFrames) / config.sample_rate_;
        callback(samples.data(), kBufferFrames, streamTime, false);
        deliveredFrames = nextDeliveredFrames;
    }
}

} // namespace IRAudio
