#include <gtest/gtest.h>

#include <irreden/audio/audio.hpp>
#include <irreden/profile/logger_spd.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Input capture reports what the backend reported. RtAudio 6 signals failure
// through its return value, so every arm here drives Audio through a fake
// backend that returns a chosen status: no hardware, no audio thread.

namespace {

using IRAudio::Audio;
using IRAudio::AudioCaptureConfig;

constexpr unsigned int kFakeDeviceId = 7;
constexpr const char *kFakeDeviceName = "Fake Capture Device";
constexpr const char *kOpenErrorText = "FakeBackend::openStream: sample rate rejected";
constexpr const char *kStartErrorText = "FakeBackend::startStream: stream is closed";
constexpr const char *kStopErrorText = "FakeBackend::stopStream: device refused";
constexpr const char *kOpenedMessage = "Opened audio input stream";

class FakeAudioInputBackend final : public IRAudio::detail::IAudioInputBackend {
  public:
    RtAudioErrorType openResult_ = RTAUDIO_NO_ERROR;
    RtAudioErrorType startResult_ = RTAUDIO_NO_ERROR;
    RtAudioErrorType stopResult_ = RTAUDIO_NO_ERROR;

    int openCalls_ = 0;
    int startCalls_ = 0;
    int stopCalls_ = 0;
    int closeCalls_ = 0;
    long streamLatencyFrames_ = 0;
    unsigned int reportedStreamSampleRate_ = 0;
    unsigned int openedDeviceId_ = 0;
    unsigned int openedChannels_ = 0;
    unsigned int openedSampleRate_ = 0;
    std::vector<unsigned int> sampleRates_ = {44'100};
    RtAudioCallback callback_;

    std::vector<unsigned int> getDeviceIds() override {
        return {kFakeDeviceId};
    }

    RtAudio::DeviceInfo getDeviceInfo(unsigned int deviceId) override {
        RtAudio::DeviceInfo info;
        info.ID = deviceId;
        info.name = kFakeDeviceName;
        info.inputChannels = 2;
        info.isDefaultInput = true;
        info.sampleRates = sampleRates_;
        info.preferredSampleRate = 44'100;
        return info;
    }

    RtAudioErrorType openInputStream(
        RtAudio::StreamParameters &parameters,
        unsigned int sampleRate,
        unsigned int &,
        RtAudioCallback callback
    ) override {
        ++openCalls_;
        openedDeviceId_ = parameters.deviceId;
        openedChannels_ = parameters.nChannels;
        openedSampleRate_ = sampleRate;
        if (openResult_ == RTAUDIO_NO_ERROR) {
            callback_ = std::move(callback);
        }
        return report(openResult_, kOpenErrorText);
    }

    RtAudioErrorType startStream() override {
        ++startCalls_;
        return report(startResult_, kStartErrorText);
    }

    RtAudioErrorType stopStream() override {
        ++stopCalls_;
        return report(stopResult_, kStopErrorText);
    }

    void closeStream() override {
        ++closeCalls_;
        callback_ = {};
    }

    const std::string &getErrorText() override {
        return m_errorText;
    }

    unsigned int getStreamSampleRate() override {
        return reportedStreamSampleRate_ == 0 ? openedSampleRate_ : reportedStreamSampleRate_;
    }

    long getStreamLatency() override {
        return streamLatencyFrames_;
    }

  private:
    std::string m_errorText;

    RtAudioErrorType report(RtAudioErrorType result, const char *errorText) {
        if (result != RTAUDIO_NO_ERROR) {
            m_errorText = errorText;
        }
        return result;
    }
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

// A callback whose only observable property is that it keeps `held` alive:
// use_count() falling back to 1 proves Audio released the callback.
Audio::AudioInputCallback holdingCallback(std::shared_ptr<int> held) {
    return [held = std::move(held)](const float *, int, double, bool) {};
}

class AudioCaptureTest : public ::testing::Test {
  protected:
    AudioCaptureTest() {
        auto backend = std::make_unique<FakeAudioInputBackend>();
        m_backend = backend.get();
        m_audio = std::make_unique<Audio>(std::move(backend));
    }

    FakeAudioInputBackend *m_backend = nullptr;
    std::unique_ptr<Audio> m_audio;
};

TEST(AudioCaptureBackendTest, NullBackendAsserts) {
    EXPECT_THROW(Audio{std::unique_ptr<IRAudio::detail::IAudioInputBackend>{}}, std::runtime_error);
}

TEST_F(AudioCaptureTest, UnlistedRateUsesNearestListedRateAndWarnsOnce) {
    EngineLogCapture log;

    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 48'000, 2, {}));

    EXPECT_EQ(m_backend->openedSampleRate_, 44'100u);
    EXPECT_EQ(m_audio->getCaptureSampleRate(), 44'100);
    const std::string text = log.text();
    const std::string warning = "Audio input sample rate substituted";
    const std::size_t first = text.find(warning);
    ASSERT_NE(first, std::string::npos) << text;
    EXPECT_EQ(text.find(warning, first + warning.size()), std::string::npos) << text;
    EXPECT_TRUE(text.contains(kFakeDeviceName)) << text;
    EXPECT_TRUE(text.contains("requestedRate=48000")) << text;
    EXPECT_TRUE(text.contains("attemptedRate=44100")) << text;
}

TEST_F(AudioCaptureTest, NearestListedRateBreaksATieTowardTheHigherRate) {
    m_backend->sampleRates_ = {44'100, 48'000};

    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 46'050, 2, {}));

    EXPECT_EQ(m_backend->openedSampleRate_, 48'000u);
}

TEST_F(AudioCaptureTest, BackendReportedRateControlsCaptureRateAndLatency) {
    m_backend->sampleRates_ = {48'000};
    m_backend->reportedStreamSampleRate_ = 44'100;
    m_backend->streamLatencyFrames_ = 441;
    EngineLogCapture log;

    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 48'000, 2, {}));

    EXPECT_EQ(m_audio->getCaptureSampleRate(), 44'100);
    EXPECT_DOUBLE_EQ(m_audio->getInputLatencyMs(), 10.0);
    const std::string text = log.text();
    EXPECT_TRUE(text.contains("Audio input backend adjusted sample rate")) << text;
    EXPECT_TRUE(text.contains("requestedRate=48000")) << text;
    EXPECT_TRUE(text.contains("actualRate=44100")) << text;
}

TEST_F(AudioCaptureTest, SupportedOrUnknownRatesOpenWithoutSubstitution) {
    {
        EngineLogCapture log;
        ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, {}));
        EXPECT_EQ(m_backend->openedSampleRate_, 44'100u);
        EXPECT_FALSE(log.text().contains("Audio input sample rate substituted"));
    }

    m_backend->sampleRates_.clear();
    EngineLogCapture log;
    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 48'000, 2, {}));
    EXPECT_EQ(m_backend->openedSampleRate_, 48'000u);
    EXPECT_FALSE(log.text().contains("Audio input sample rate substituted"));
}

TEST_F(AudioCaptureTest, CaptureRateIsZeroUnlessAStreamIsOpen) {
    EXPECT_EQ(m_audio->getCaptureSampleRate(), 0);

    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, {}));
    EXPECT_EQ(m_audio->getCaptureSampleRate(), 44'100);
    m_audio->closeStreamIn();
    EXPECT_EQ(m_audio->getCaptureSampleRate(), 0);

    m_backend->openResult_ = RTAUDIO_SYSTEM_ERROR;
    EXPECT_FALSE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, {}));
    EXPECT_EQ(m_audio->getCaptureSampleRate(), 0);
}

TEST_F(AudioCaptureTest, OpenErrorReturnsFalseAndNamesDeviceRateAndBackendText) {
    m_backend->openResult_ = RTAUDIO_SYSTEM_ERROR;
    EngineLogCapture log;

    EXPECT_FALSE(m_audio->openStreamIn(kFakeDeviceName, 48'000, 2, {}));

    EXPECT_EQ(m_backend->openCalls_, 1);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());
    const std::string text = log.text();
    EXPECT_TRUE(text.contains(kFakeDeviceName)) << text;
    EXPECT_TRUE(text.contains("requestedRate=48000")) << text;
    EXPECT_TRUE(text.contains("attemptedRate=44100")) << text;
    EXPECT_TRUE(text.contains(kOpenErrorText)) << text;
    EXPECT_FALSE(text.contains(kOpenedMessage)) << text;
}

TEST_F(AudioCaptureTest, OpenErrorDropsTheRejectedCallback) {
    m_backend->openResult_ = RTAUDIO_SYSTEM_ERROR;
    auto held = std::make_shared<int>(0);

    EXPECT_FALSE(m_audio->openStreamIn(kFakeDeviceName, 48'000, 2, holdingCallback(held)));

    EXPECT_EQ(held.use_count(), 1);
}

TEST_F(AudioCaptureTest, OpenWarningStatusIsNotSuccess) {
    m_backend->openResult_ = RTAUDIO_WARNING;

    EXPECT_FALSE(m_audio->openStreamIn(kFakeDeviceName, 48'000, 2, {}));

    EXPECT_FALSE(m_audio->isStreamInOpen());
}

TEST_F(AudioCaptureTest, StartErrorReturnsFalseAndStreamStaysOpenNotRunning) {
    m_backend->startResult_ = RTAUDIO_SYSTEM_ERROR;
    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 48'000, 2, {}));
    EngineLogCapture log;

    EXPECT_FALSE(m_audio->startStreamIn());

    EXPECT_EQ(m_backend->startCalls_, 1);
    EXPECT_TRUE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());
    const std::string text = log.text();
    EXPECT_TRUE(text.contains(kStartErrorText)) << text;
}

TEST_F(AudioCaptureTest, StartCaptureClosesTheStreamWhenStartFails) {
    m_backend->startResult_ = RTAUDIO_SYSTEM_ERROR;
    AudioCaptureConfig config;
    config.device_name_ = kFakeDeviceName;

    EXPECT_FALSE(m_audio->startCapture(config, {}));

    EXPECT_EQ(m_backend->openCalls_, 1);
    EXPECT_EQ(m_backend->startCalls_, 1);
    EXPECT_EQ(m_backend->closeCalls_, 1);
    EXPECT_EQ(m_backend->stopCalls_, 0);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
}

TEST_F(AudioCaptureTest, StartCaptureReportsOpenFailure) {
    m_backend->openResult_ = RTAUDIO_SYSTEM_ERROR;
    AudioCaptureConfig config;
    config.device_name_ = kFakeDeviceName;

    EXPECT_FALSE(m_audio->startCapture(config, {}));

    EXPECT_EQ(m_backend->startCalls_, 0);
    EXPECT_FALSE(m_audio->isCapturing());
}

TEST_F(AudioCaptureTest, SuccessOpensStartsAndDeliversSamples) {
    int deliveredFrames = 0;
    float deliveredFirstSample = 0.0f;
    double deliveredStreamTime = 0.0;
    bool deliveredOverflow = false;
    EngineLogCapture log;

    ASSERT_TRUE(m_audio->openStreamIn(
        kFakeDeviceName,
        44'100,
        2,
        [&](const float *samples, int frameCount, double streamTime, bool overflow) {
            deliveredFrames = frameCount;
            deliveredFirstSample = samples[0];
            deliveredStreamTime = streamTime;
            deliveredOverflow = overflow;
        }
    ));

    EXPECT_EQ(m_backend->openedDeviceId_, kFakeDeviceId);
    EXPECT_EQ(m_backend->openedSampleRate_, 44'100u);
    EXPECT_EQ(m_backend->openedChannels_, 2u);
    EXPECT_TRUE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());
    EXPECT_TRUE(log.text().contains(kOpenedMessage)) << log.text();

    ASSERT_TRUE(m_audio->startStreamIn());
    EXPECT_TRUE(m_audio->isStreamInRunning());
    EXPECT_TRUE(m_audio->isCapturing());

    ASSERT_TRUE(static_cast<bool>(m_backend->callback_));
    std::array<float, 8> buffer{0.25f, -0.25f, 0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
    EXPECT_EQ(
        m_backend->callback_(nullptr, buffer.data(), 4, 1.5, RTAUDIO_INPUT_OVERFLOW, nullptr),
        0
    );
    EXPECT_EQ(deliveredFrames, 4);
    EXPECT_FLOAT_EQ(deliveredFirstSample, 0.25f);
    EXPECT_DOUBLE_EQ(deliveredStreamTime, 1.5);
    EXPECT_TRUE(deliveredOverflow);
}

TEST_F(AudioCaptureTest, StopThenCloseRestoreIdleState) {
    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, {}));
    ASSERT_TRUE(m_audio->startStreamIn());

    m_audio->stopStreamIn();

    EXPECT_EQ(m_backend->stopCalls_, 1);
    EXPECT_TRUE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());

    m_audio->closeStreamIn();

    EXPECT_EQ(m_backend->stopCalls_, 1);
    EXPECT_EQ(m_backend->closeCalls_, 1);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());
}

TEST_F(AudioCaptureTest, StopErrorLogsBackendTextAndStreamStaysRunning) {
    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, {}));
    ASSERT_TRUE(m_audio->startStreamIn());
    m_backend->stopResult_ = RTAUDIO_SYSTEM_ERROR;
    EngineLogCapture log;

    m_audio->stopStreamIn();

    EXPECT_EQ(m_backend->stopCalls_, 1);
    EXPECT_TRUE(m_audio->isStreamInRunning());
    EXPECT_TRUE(m_audio->isCapturing());
    const std::string text = log.text();
    EXPECT_TRUE(text.contains(kStopErrorText)) << text;
}

TEST_F(AudioCaptureTest, CloseAfterAFailedStopStillReleasesTheStream) {
    auto held = std::make_shared<int>(0);
    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, holdingCallback(held)));
    ASSERT_TRUE(m_audio->startStreamIn());
    m_backend->stopResult_ = RTAUDIO_SYSTEM_ERROR;

    m_audio->closeStreamIn();

    EXPECT_EQ(m_backend->closeCalls_, 1);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());
    EXPECT_EQ(held.use_count(), 1);
}

} // namespace
