#include <gtest/gtest.h>

#include <irreden/audio/audio.hpp>
#include <irreden/audio/audio_capture_source.hpp>
#include <irreden/profile/logger_spd.hpp>
#include <irreden/video/video_manager.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeAudioCaptureSource final : public IRAudio::IAudioCaptureSource {
  public:
    bool acceptsStart_ = true;
    int deliveredSampleRate_ = 44'100;
    int deliveredChannels_ = 2;
    int requestedSampleRate_ = 0;
    int requestedChannels_ = 0;
    bool monitorEnabled_ = false;
    std::string monitorDeviceName_;

    bool startCapture(
        const IRAudio::AudioCaptureConfig &config, IRAudio::AudioCaptureCallback callback
    ) override {
        requestedSampleRate_ = config.sample_rate_;
        requestedChannels_ = config.channels_;
        monitorEnabled_ = config.monitor_enabled_;
        monitorDeviceName_ = config.monitor_device_name_;
        m_callback = std::move(callback);
        m_capturing = acceptsStart_;
        return m_capturing;
    }

    void stopCapture() override {
        m_capturing = false;
        m_callback = {};
    }

    [[nodiscard]] bool isCapturing() const override {
        return m_capturing;
    }

    [[nodiscard]] int getCaptureSampleRate() const override {
        return m_capturing ? deliveredSampleRate_ : 0;
    }

    [[nodiscard]] int getCaptureChannels() const override {
        return m_capturing ? deliveredChannels_ : 0;
    }

    [[nodiscard]] double getInputLatencyMs() const override {
        return 0.0;
    }

    void setCaptureRecording(bool) override {}

    [[nodiscard]] std::optional<IRAudio::AudioInputLevels> getInputLevels() const override {
        return std::nullopt;
    }

  private:
    bool m_capturing = false;
    IRAudio::AudioCaptureCallback m_callback;
};

void configureAudioCapture(IRVideo::VideoManager &manager, IRAudio::IAudioCaptureSource &source) {
    manager.configureCapture(
        "capture.mp4",
        60,
        10'000'000,
        true,
        "Fake Capture Device",
        true,
        "Fake Monitor Device",
        48'000,
        2,
        320'000,
        true,
        true,
        0.0,
        &source
    );
}

TEST(VideoAudioCaptureRateTest, SuccessfulArmUsesTheSourcesDeliveredRate) {
    FakeAudioCaptureSource source;
    IRVideo::VideoManager manager;

    configureAudioCapture(manager, source);

    EXPECT_EQ(source.requestedSampleRate_, 48'000);
    EXPECT_TRUE(source.monitorEnabled_);
    EXPECT_EQ(source.monitorDeviceName_, "Fake Monitor Device");
    EXPECT_TRUE(manager.isAudioInputArmed());
    EXPECT_EQ(manager.recordingAudioSampleRate(), 44'100);

    configureAudioCapture(manager, source);
    EXPECT_EQ(manager.recordingAudioSampleRate(), 44'100);
}

TEST(VideoAudioCaptureRateTest, SuccessfulArmAdoptsTheSourcesDeliveredChannels) {
    FakeAudioCaptureSource source;
    source.deliveredChannels_ = 1;
    IRVideo::VideoManager manager;

    configureAudioCapture(manager, source);

    EXPECT_EQ(source.requestedChannels_, 2);
    EXPECT_EQ(manager.recordingAudioChannels(), 1);
}

TEST(VideoAudioCaptureRateTest, FailedArmKeepsTheConfiguredRate) {
    FakeAudioCaptureSource source;
    source.acceptsStart_ = false;
    IRVideo::VideoManager manager;

    configureAudioCapture(manager, source);

    EXPECT_EQ(source.requestedSampleRate_, 48'000);
    EXPECT_FALSE(manager.isAudioInputArmed());
    EXPECT_EQ(manager.recordingAudioSampleRate(), 48'000);
    EXPECT_EQ(manager.recordingAudioChannels(), 2);
}

constexpr unsigned int kLevelDeviceId = 3;
constexpr const char *kLevelDeviceName = "Level Device";
constexpr int kLevelRate = 44'100;
constexpr int kLevelWindowFrames = 4'410;

// A stereo input that hands the test its driver callback.
class LevelBackend final : public IRAudio::detail::IAudioInputBackend {
  public:
    explicit LevelBackend(std::shared_ptr<RtAudioCallback> callback)
        : m_callback(std::move(callback)) {}

    std::vector<unsigned int> getDeviceIds() override {
        return {kLevelDeviceId};
    }

    RtAudio::DeviceInfo getDeviceInfo(unsigned int deviceId) override {
        RtAudio::DeviceInfo info;
        info.ID = deviceId;
        info.name = kLevelDeviceName;
        info.inputChannels = 2;
        info.isDefaultInput = true;
        info.sampleRates = {static_cast<unsigned int>(kLevelRate)};
        return info;
    }

    RtAudioErrorType openInputStream(
        RtAudio::StreamParameters &, unsigned int, unsigned int &, RtAudioCallback callback
    ) override {
        *m_callback = std::move(callback);
        return RTAUDIO_NO_ERROR;
    }

    RtAudioErrorType openOutputStream(
        RtAudio::StreamParameters &, unsigned int, unsigned int &, RtAudioCallback
    ) override {
        return RTAUDIO_SYSTEM_ERROR;
    }

    RtAudioErrorType startStream() override {
        return RTAUDIO_NO_ERROR;
    }

    RtAudioErrorType stopStream() override {
        return RTAUDIO_NO_ERROR;
    }

    void closeStream() override {
        *m_callback = {};
    }

    const std::string &getErrorText() override {
        return m_error;
    }

    unsigned int getStreamSampleRate() override {
        return kLevelRate;
    }

    long getStreamLatency() override {
        return 0;
    }

  private:
    std::shared_ptr<RtAudioCallback> m_callback;
    std::string m_error;
};

// LoggerSpd is a leaked process-global, so the sink is erased on destruction
// rather than left to dangle on the stream it writes to.
class EngineLogCapture {
  public:
    EngineLogCapture()
        : m_logger{LoggerSpd::instance()->getEngineLogger()}
        , m_sink{std::make_shared<spdlog::sinks::ostream_sink_st>(m_text)} {
        m_logger->sinks().push_back(m_sink);
    }

    ~EngineLogCapture() {
        auto &sinks = m_logger->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    std::string text() {
        m_logger->flush();
        return m_text.str();
    }

  private:
    std::ostringstream m_text;
    spdlog::logger *m_logger;
    std::shared_ptr<spdlog::sinks::sink> m_sink;
};

std::string displayed(float value) {
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << value;
    return text.str();
}

// The recorder logs what the meter published: the line VideoManager's
// capture callback produces carries the snapshot the input reports for the
// same window, after display rounding.
TEST(VideoAudioLevelLogTest, LogsTheMeteredWindowTheInputReports) {
    auto callback = std::make_shared<RtAudioCallback>();
    IRAudio::Audio audio{std::make_unique<LevelBackend>(callback)};
    ASSERT_TRUE(audio.armInput(kLevelDeviceName, kLevelRate).armed_);

    std::vector<float> samples(static_cast<std::size_t>(kLevelWindowFrames) * 2, 0.0f);
    for (int frame = 0; frame < kLevelWindowFrames; ++frame) {
        samples[static_cast<std::size_t>(frame) * 2] = (frame % 2 == 0) ? 0.5f : -0.5f;
        samples[static_cast<std::size_t>(frame) * 2 + 1] = (frame % 2 == 0) ? 0.1f : -0.1f;
    }
    samples[1] = 1.0f;
    ASSERT_TRUE(static_cast<bool>(*callback));
    (*callback)(nullptr, samples.data(), kLevelWindowFrames, 0.0, 0, nullptr);
    const std::optional<IRAudio::AudioInputLevels> levels = audio.getInputLevels();
    ASSERT_TRUE(levels.has_value());
    ASSERT_EQ(levels->generation_, 1u);
    IRVideo::AudioLevelLog levelLog;
    EngineLogCapture log;

    EXPECT_FALSE(levelLog.observe(0.0, audio.getInputLevels()));
    EXPECT_FALSE(
        levelLog.observe(IRVideo::AudioLevelLog::kIntervalSeconds - 0.1, audio.getInputLevels())
    );
    EXPECT_TRUE(levelLog.observe(IRVideo::AudioLevelLog::kIntervalSeconds, audio.getInputLevels()));

    const IRAudio::AudioChannelLevel &left = levels->channels_[0];
    const IRAudio::AudioChannelLevel &right = levels->channels_[1];
    const std::string expected =
        "Audio levels: peak=" + displayed(left.peakDbfs_) + "/" + displayed(right.peakDbfs_) +
        " dBFS, RMS=" + displayed(left.rmsDbfs_) + "/" + displayed(right.rmsDbfs_) +
        " dBFS, clipped=" + std::to_string(left.clippedSamples_) + "/" +
        std::to_string(right.clippedSamples_) + " samples, generation=1";
    const std::string text = log.text();
    EXPECT_TRUE(text.contains(expected)) << "expected: " << expected << "\nlog: " << text;
    EXPECT_EQ(right.clippedSamples_, 1u);
    EXPECT_TRUE(text.contains("Audio CLIPPING detected! 1 samples clipped")) << text;
    EXPECT_FALSE(levelLog.observe(IRVideo::AudioLevelLog::kIntervalSeconds + 1.0, levels));
}

} // namespace
