#include <gtest/gtest.h>

#include <irreden/audio/audio_capture_source.hpp>
#include <irreden/video/video_manager.hpp>

#include <utility>

namespace {

class FakeAudioCaptureSource final : public IRAudio::IAudioCaptureSource {
  public:
    bool acceptsStart_ = true;
    int deliveredSampleRate_ = 44'100;
    int requestedSampleRate_ = 0;
    bool monitorEnabled_ = false;
    std::string monitorDeviceName_;

    bool startCapture(
        const IRAudio::AudioCaptureConfig &config, IRAudio::AudioCaptureCallback callback
    ) override {
        requestedSampleRate_ = config.sample_rate_;
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

    [[nodiscard]] double getInputLatencyMs() const override {
        return 0.0;
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

TEST(VideoAudioCaptureRateTest, FailedArmKeepsTheConfiguredRate) {
    FakeAudioCaptureSource source;
    source.acceptsStart_ = false;
    IRVideo::VideoManager manager;

    configureAudioCapture(manager, source);

    EXPECT_EQ(source.requestedSampleRate_, 48'000);
    EXPECT_FALSE(manager.isAudioInputArmed());
    EXPECT_EQ(manager.recordingAudioSampleRate(), 48'000);
}

} // namespace
