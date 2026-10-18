#include <gtest/gtest.h>

#include <irreden/ir_audio.hpp>
#include <irreden/audio/audio.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/profile/logger_spd.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Input capture reports what the backend reported. RtAudio 6 signals failure
// through its return value, so every arm here drives Audio through a fake
// backend that returns a chosen status: no hardware, no audio thread. The fake
// can also be held inside a call, which is how the deadline arms stand in for
// a backend the OS never returns from.

namespace {

using IRAudio::Audio;
using IRAudio::AudioCaptureConfig;

constexpr unsigned int kFakeDeviceId = 7;
constexpr unsigned int kFakeOutputDeviceId = 8;
constexpr unsigned int kSecondInputDeviceId = 9;
constexpr const char *kFakeDeviceName = "Fake Capture Device";
constexpr const char *kFakeOutputDeviceName = "Fake Monitor Device";
constexpr const char *kSecondInputDeviceName = "Second Capture Device";
constexpr const char *kOpenErrorText = "FakeBackend::openStream: sample rate rejected";
constexpr const char *kStartErrorText = "FakeBackend::startStream: stream is closed";
constexpr const char *kStopErrorText = "FakeBackend::stopStream: device refused";
constexpr const char *kOpenedMessage = "Opened audio input stream";
constexpr const char *kPermissionHint = "check microphone permission";

// A deadline no fake call comes near: an arm under it must not time out.
constexpr std::chrono::milliseconds kPatientDeadline = std::chrono::seconds(30);
// For arms whose every call under the deadline is held.
constexpr std::chrono::milliseconds kShortDeadline{100};
// For arms where a call has to succeed under the same deadline another call
// then overruns; wide enough that a busy host does not time the first one out.
constexpr std::chrono::milliseconds kRoomyDeadline{500};
// How long a test waits for a signal it expects; only a failing run spends it.
constexpr std::chrono::milliseconds kSignalWait = std::chrono::seconds(10);
// Far above the injected deadlines and far below kPatientDeadline: a call that
// returned inside it gave up on the held backend instead of waiting it out.
constexpr std::chrono::milliseconds kBoundedReturn = std::chrono::seconds(5);

// Everything a test reads from or tells the fake backend. Shared, because the
// backend is destroyed on Audio's control thread, which can outlive Audio.
struct FakeBackendState {
    RtAudioErrorType openResult_ = RTAUDIO_NO_ERROR;
    RtAudioErrorType startResult_ = RTAUDIO_NO_ERROR;
    RtAudioErrorType stopResult_ = RTAUDIO_NO_ERROR;
    bool throwOpen_ = false;
    bool throwStart_ = false;

    std::atomic<int> deviceInfoCalls_ = 0;
    std::atomic<int> openCalls_ = 0;
    std::atomic<int> startCalls_ = 0;
    std::atomic<int> stopCalls_ = 0;
    std::atomic<int> closeCalls_ = 0;
    std::atomic<int> latencyCalls_ = 0;
    std::atomic<bool> destroyed_ = false;
    long streamLatencyFrames_ = 0;
    unsigned int reportedStreamSampleRate_ = 0;
    unsigned int openedDeviceId_ = 0;
    unsigned int openedChannels_ = 0;
    unsigned int openedSampleRate_ = 0;
    unsigned int openedBufferFrames_ = 0;
    unsigned int grantedBufferFrames_ = 0;
    unsigned int inputDeviceId_ = kFakeDeviceId;
    unsigned int outputDeviceId_ = kFakeOutputDeviceId;
    std::string inputDeviceName_ = kFakeDeviceName;
    std::string outputDeviceName_ = kFakeOutputDeviceName;
    unsigned int inputChannels_ = 2;
    unsigned int outputChannels_ = 2;
    bool hasSeparateOutputDevice_ = false;
    bool hasSecondInputDevice_ = false;
    bool hasDefaultOutput_ = true;
    std::vector<unsigned int> sampleRates_ = {44'100};
    std::thread::id enumerationThread_;
    std::thread::id openThread_;
    std::thread::id closeThread_;
    RtAudioCallback callback_;

    // A held call reports that it was entered, then stays inside the backend
    // until the test releases it.
    struct Hold {
        std::atomic<bool> held_ = false;
        std::atomic<bool> entered_ = false;
    };
    Hold holdOpen_;
    Hold holdStart_;
    Hold holdStop_;
    Hold holdClose_;

    void hold(Hold &hold) {
        hold.held_ = true;
    }

    void release(Hold &hold) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            hold.held_ = false;
        }
        m_changed.notify_all();
    }

    void releaseAll() {
        release(holdOpen_);
        release(holdStart_);
        release(holdStop_);
        release(holdClose_);
    }

    // Backend side.
    void pass(Hold &hold) {
        std::unique_lock<std::mutex> lock(m_mutex);
        hold.entered_ = true;
        m_changed.notify_all();
        m_changed.wait(lock, [&hold] { return !hold.held_; });
    }

    // Backend side, after any change a test may be waiting on.
    void notify() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
        }
        m_changed.notify_all();
    }

    template <typename Predicate> [[nodiscard]] bool waitUntil(Predicate predicate) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_changed.wait_for(lock, kSignalWait, predicate);
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
};

class FakeAudioInputBackend final : public IRAudio::detail::IAudioInputBackend {
  public:
    explicit FakeAudioInputBackend(std::shared_ptr<FakeBackendState> state)
        : m_state(std::move(state)) {}

    ~FakeAudioInputBackend() override {
        m_state->destroyed_ = true;
        m_state->notify();
    }

    std::vector<unsigned int> getDeviceIds() override {
        m_state->enumerationThread_ = std::this_thread::get_id();
        std::vector<unsigned int> ids{m_state->inputDeviceId_};
        if (m_state->hasSeparateOutputDevice_) {
            ids.push_back(m_state->outputDeviceId_);
        }
        if (m_state->hasSecondInputDevice_) {
            ids.push_back(kSecondInputDeviceId);
        }
        return ids;
    }

    RtAudio::DeviceInfo getDeviceInfo(unsigned int deviceId) override {
        ++m_state->deviceInfoCalls_;
        RtAudio::DeviceInfo info;
        info.ID = deviceId;
        if (deviceId == kSecondInputDeviceId) {
            info.name = kSecondInputDeviceName;
            info.inputChannels = m_state->inputChannels_;
            info.sampleRates = m_state->sampleRates_;
            info.preferredSampleRate = 44'100;
            return info;
        }
        if (deviceId == m_state->outputDeviceId_) {
            info.name = m_state->outputDeviceName_;
            info.outputChannels = m_state->outputChannels_;
            info.isDefaultOutput = m_state->hasDefaultOutput_;
            info.sampleRates = m_state->sampleRates_;
            info.preferredSampleRate = 44'100;
            return info;
        }
        info.name = m_state->inputDeviceName_;
        info.inputChannels = m_state->inputChannels_;
        info.outputChannels = m_state->outputChannels_;
        info.isDefaultInput = true;
        info.isDefaultOutput = m_state->hasDefaultOutput_ && !m_state->hasSeparateOutputDevice_;
        info.sampleRates = m_state->sampleRates_;
        info.preferredSampleRate = 44'100;
        return info;
    }

    RtAudioErrorType openOutputStream(
        RtAudio::StreamParameters &parameters,
        unsigned int sampleRate,
        unsigned int &bufferFrames,
        RtAudioCallback callback
    ) override {
        return openInputStream(parameters, sampleRate, bufferFrames, std::move(callback));
    }

    RtAudioErrorType openInputStream(
        RtAudio::StreamParameters &parameters,
        unsigned int sampleRate,
        unsigned int &bufferFrames,
        RtAudioCallback callback
    ) override {
        ++m_state->openCalls_;
        if (m_state->throwOpen_) {
            throw std::runtime_error("fake open throw");
        }
        m_state->openThread_ = std::this_thread::get_id();
        m_state->openedDeviceId_ = parameters.deviceId;
        m_state->openedChannels_ = parameters.nChannels;
        m_state->openedSampleRate_ = sampleRate;
        m_state->openedBufferFrames_ = bufferFrames;
        if (m_state->grantedBufferFrames_ > 0) {
            bufferFrames = m_state->grantedBufferFrames_;
        }
        if (m_state->openResult_ == RTAUDIO_NO_ERROR) {
            m_state->callback_ = std::move(callback);
        }
        m_state->pass(m_state->holdOpen_);
        return report(m_state->openResult_, kOpenErrorText);
    }

    RtAudioErrorType startStream() override {
        ++m_state->startCalls_;
        if (m_state->throwStart_) {
            throw std::runtime_error("fake start throw");
        }
        m_state->pass(m_state->holdStart_);
        return report(m_state->startResult_, kStartErrorText);
    }

    RtAudioErrorType stopStream() override {
        ++m_state->stopCalls_;
        m_state->pass(m_state->holdStop_);
        return report(m_state->stopResult_, kStopErrorText);
    }

    void closeStream() override {
        m_state->closeThread_ = std::this_thread::get_id();
        m_state->pass(m_state->holdClose_);
        m_state->callback_ = {};
        ++m_state->closeCalls_;
        m_state->notify();
    }

    const std::string &getErrorText() override {
        return m_errorText;
    }

    unsigned int getStreamSampleRate() override {
        return m_state->reportedStreamSampleRate_ == 0 ? m_state->openedSampleRate_
                                                       : m_state->reportedStreamSampleRate_;
    }

    long getStreamLatency() override {
        ++m_state->latencyCalls_;
        return m_state->streamLatencyFrames_;
    }

  private:
    std::shared_ptr<FakeBackendState> m_state;
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

AudioCaptureConfig fakeDeviceConfig() {
    AudioCaptureConfig config;
    config.device_name_ = kFakeDeviceName;
    return config;
}

AudioCaptureConfig monitoredFakeDeviceConfig() {
    AudioCaptureConfig config = fakeDeviceConfig();
    config.monitor_enabled_ = true;
    config.monitor_device_name_ = kFakeDeviceName;
    return config;
}

template <typename Call> std::chrono::steady_clock::duration elapsedOf(Call &&call) {
    const auto startedAt = std::chrono::steady_clock::now();
    call();
    return std::chrono::steady_clock::now() - startedAt;
}

int countOf(const std::string &text, const std::string &needle) {
    int count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

Audio::AudioInputCallback countDeliveredFrames(int &deliveredFrames) {
    return
        [&deliveredFrames](const float *, int frames, double, bool) { deliveredFrames += frames; };
}

void expectRecorderDelivery(const std::shared_ptr<FakeBackendState> &input, int &deliveredFrames) {
    ASSERT_TRUE(static_cast<bool>(input->callback_));
    std::array<float, 8> samples{};
    input->callback_(nullptr, samples.data(), 4, 0.0, 0, nullptr);
    EXPECT_EQ(deliveredFrames, 4);
}

std::string limitText(std::chrono::milliseconds limit) {
    return "within " + std::to_string(limit.count()) + " ms";
}

class AudioCaptureFixture : public ::testing::Test {
  protected:
    explicit AudioCaptureFixture(IRAudio::detail::AudioInputDeadlines deadlines)
        : m_backend{std::make_shared<FakeBackendState>()}
        , m_audio{
              std::make_unique<Audio>(std::make_unique<FakeAudioInputBackend>(m_backend), deadlines)
          } {}

    // Every arm ends with the control thread gone: held calls are released and
    // the backend's destruction is awaited, so no arm leaks a thread into the
    // next one.
    ~AudioCaptureFixture() override {
        m_backend->releaseAll();
        m_audio.reset();
        EXPECT_TRUE(m_backend->waitUntil([this] { return m_backend->destroyed_.load(); }));
    }

    std::shared_ptr<FakeBackendState> m_backend;
    std::unique_ptr<Audio> m_audio;
};

class AudioCaptureTest : public AudioCaptureFixture {
  protected:
    AudioCaptureTest()
        : AudioCaptureFixture({kPatientDeadline, kPatientDeadline}) {}
};

class AudioCaptureArmDeadlineTest : public AudioCaptureFixture {
  protected:
    AudioCaptureArmDeadlineTest()
        : AudioCaptureFixture({kShortDeadline, kPatientDeadline}) {}
};

class AudioCaptureRoomyArmDeadlineTest : public AudioCaptureFixture {
  protected:
    AudioCaptureRoomyArmDeadlineTest()
        : AudioCaptureFixture({kRoomyDeadline, kPatientDeadline}) {}
};

class AudioCaptureTeardownDeadlineTest : public AudioCaptureFixture {
  protected:
    AudioCaptureTeardownDeadlineTest()
        : AudioCaptureFixture({kPatientDeadline, kShortDeadline}) {}
};

TEST(AudioCaptureBackendTest, NullBackendAsserts) {
    EXPECT_THROW(Audio{std::unique_ptr<IRAudio::detail::IAudioInputBackend>{}}, std::runtime_error);
}

void expectSummary(
    const IRAudio::AudioDeviceSummary &actual, const IRAudio::AudioDeviceSummary &expected, int arm
) {
    EXPECT_EQ(actual.name_, expected.name_) << "arm " << arm;
    EXPECT_EQ(actual.inputChannels_, expected.inputChannels_) << "arm " << arm;
    EXPECT_EQ(actual.outputChannels_, expected.outputChannels_) << "arm " << arm;
    EXPECT_EQ(actual.defaultInput_, expected.defaultInput_) << "arm " << arm;
    EXPECT_EQ(actual.defaultOutput_, expected.defaultOutput_) << "arm " << arm;
    EXPECT_EQ(actual.preferredSampleRate_, expected.preferredSampleRate_) << "arm " << arm;
}

// Both arms enumerate the same IDs in the same order and swap the two names, so
// the device map iterates identically in both while the name order flips: only
// a sort by name passes both. Read through IRAudio::audioDevices(), the
// creation-facing surface, against the injected manager.
TEST(AudioDeviceSummaryTest, DeviceSummariesListTheEnumeratedDevicesByName) {
    for (int arm = 0; arm < 2; ++arm) {
        auto backend = std::make_shared<FakeBackendState>();
        backend->hasSeparateOutputDevice_ = true;
        if (arm == 1) {
            std::swap(backend->inputDeviceName_, backend->outputDeviceName_);
        }
        IRAudio::AudioManager audioManager{
            std::make_unique<FakeAudioInputBackend>(backend),
            nullptr
        };
        const IRAudio::AudioDeviceSummary
            input{backend->inputDeviceName_, 2, 2, true, false, 44'100};
        const IRAudio::AudioDeviceSummary
            output{backend->outputDeviceName_, 0, 2, false, true, 44'100};

        const std::vector<IRAudio::AudioDeviceSummary> devices = IRAudio::audioDevices();

        ASSERT_EQ(devices.size(), 2u) << "arm " << arm;
        expectSummary(devices[0], arm == 0 ? input : output, arm);
        expectSummary(devices[1], arm == 0 ? output : input, arm);
    }
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

    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
    // The close is the control thread's, after startCapture has returned.
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->closeCalls_ == 1; }));
    EXPECT_EQ(m_backend->openCalls_, 1);
    EXPECT_EQ(m_backend->startCalls_, 1);
    EXPECT_EQ(m_backend->stopCalls_, 0);
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

TEST_F(AudioCaptureTest, StartCaptureRunsEachBackendOperationOnceOffTheCallingThread) {
    std::atomic<int> deliveredFrames = 0;

    ASSERT_TRUE(
        m_audio->startCapture(fakeDeviceConfig(), [&](const float *, int frameCount, double, bool) {
            deliveredFrames += frameCount;
        })
    );

    EXPECT_TRUE(m_audio->isStreamInOpen());
    EXPECT_TRUE(m_audio->isCapturing());
    EXPECT_EQ(m_backend->openCalls_, 1);
    EXPECT_EQ(m_backend->startCalls_, 1);
    EXPECT_NE(m_backend->openThread_, std::this_thread::get_id());
    EXPECT_EQ(m_backend->openThread_, m_backend->enumerationThread_);

    const RtAudioCallback driverCallback = m_backend->callback_;
    std::array<float, 8> buffer{};
    driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr);
    EXPECT_EQ(deliveredFrames, 4);

    m_audio->stopCapture();

    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
    EXPECT_EQ(m_backend->openCalls_, 1);
    EXPECT_EQ(m_backend->startCalls_, 1);
    EXPECT_EQ(m_backend->stopCalls_, 1);
    EXPECT_EQ(m_backend->closeCalls_, 1);
    EXPECT_EQ(m_backend->closeThread_, m_backend->openThread_);
    driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr);
    EXPECT_EQ(deliveredFrames, 4);
}

TEST_F(AudioCaptureArmDeadlineTest, StartCaptureGivesUpOnAHeldOpen) {
    m_backend->hold(m_backend->holdOpen_);
    EngineLogCapture log;

    bool started = true;
    const auto elapsed =
        elapsedOf([&] { started = m_audio->startCapture(fakeDeviceConfig(), {}); });

    EXPECT_FALSE(started);
    EXPECT_GE(elapsed, kShortDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->holdOpen_.entered_.load(); }));
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
    const std::string text = log.text();
    EXPECT_TRUE(text.contains("Audio input unavailable")) << text;
    EXPECT_TRUE(text.contains("backend open did not return")) << text;
    EXPECT_TRUE(text.contains(limitText(kShortDeadline))) << text;
    EXPECT_TRUE(text.contains(kFakeDeviceName)) << text;
    EXPECT_TRUE(text.contains(kPermissionHint)) << text;
    EXPECT_FALSE(text.contains(kOpenedMessage)) << text;

    // The open succeeds late: the control thread closes the stream it got and
    // never starts it.
    m_backend->release(m_backend->holdOpen_);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->closeCalls_ == 1; }));
    EXPECT_EQ(m_backend->startCalls_, 0);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
}

TEST_F(AudioCaptureArmDeadlineTest, StartCaptureGivesUpOnAHeldStartAndDeliversNothingLate) {
    m_backend->hold(m_backend->holdStart_);
    std::atomic<int> deliveries = 0;
    EngineLogCapture log;

    bool started = true;
    const auto elapsed = elapsedOf([&] {
        started = m_audio->startCapture(fakeDeviceConfig(), [&](const float *, int, double, bool) {
            ++deliveries;
        });
    });

    EXPECT_FALSE(started);
    EXPECT_GE(elapsed, kShortDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->holdStart_.entered_.load(); }));
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
    const std::string text = log.text();
    EXPECT_TRUE(text.contains("backend start did not return")) << text;
    EXPECT_TRUE(text.contains(limitText(kShortDeadline))) << text;
    EXPECT_TRUE(text.contains(kPermissionHint)) << text;

    // The backend holds the callback from its open; the driver may still fire it.
    const RtAudioCallback driverCallback = m_backend->callback_;
    std::array<float, 8> buffer{};
    driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr);
    EXPECT_EQ(deliveries, 0);

    m_backend->release(m_backend->holdStart_);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->closeCalls_ == 1; }));
    EXPECT_EQ(m_backend->stopCalls_, 0);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
    driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr);
    EXPECT_EQ(deliveries, 0);
}

TEST_F(AudioCaptureArmDeadlineTest, ArmWhileAnEarlierCallIsPendingFailsWithoutTouchingTheBackend) {
    m_backend->hold(m_backend->holdOpen_);
    ASSERT_FALSE(m_audio->startCapture(fakeDeviceConfig(), {}));
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->holdOpen_.entered_.load(); }));
    const int deviceInfoCallsBefore = m_backend->deviceInfoCalls_;
    EngineLogCapture log;

    bool started = true;
    const auto elapsed =
        elapsedOf([&] { started = m_audio->startCapture(fakeDeviceConfig(), {}); });

    EXPECT_FALSE(started);
    EXPECT_LT(elapsed, kShortDeadline);
    EXPECT_FALSE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, {}));
    EXPECT_EQ(m_backend->openCalls_, 1);
    EXPECT_EQ(m_backend->deviceInfoCalls_, deviceInfoCallsBefore);
    EXPECT_EQ(m_audio->getInputLatencyMs(), 0.0);
    EXPECT_EQ(m_backend->latencyCalls_, 0);
    const std::string text = log.text();
    EXPECT_EQ(countOf(text, "an earlier backend call has not returned"), 2) << text;
    EXPECT_EQ(countOf(text, "did not return within"), 0) << text;
}

TEST_F(AudioCaptureRoomyArmDeadlineTest, StartStreamInGivesUpOnAHeldStartAndClosesTheStream) {
    auto held = std::make_shared<int>(0);
    ASSERT_TRUE(m_audio->openStreamIn(kFakeDeviceName, 44'100, 2, holdingCallback(held)));
    m_backend->hold(m_backend->holdStart_);
    EngineLogCapture log;

    bool started = true;
    const auto elapsed = elapsedOf([&] { started = m_audio->startStreamIn(); });

    EXPECT_FALSE(started);
    EXPECT_GE(elapsed, kRoomyDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());
    EXPECT_EQ(held.use_count(), 1);
    const std::string text = log.text();
    EXPECT_TRUE(text.contains("backend start did not return")) << text;
    EXPECT_TRUE(text.contains(limitText(kRoomyDeadline))) << text;
    EXPECT_TRUE(text.contains(kFakeDeviceName)) << text;

    m_backend->release(m_backend->holdStart_);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->closeCalls_ == 1; }));
}

TEST_F(AudioCaptureRoomyArmDeadlineTest, BackendIsArmableAgainOnceThePendingCallReturns) {
    m_backend->hold(m_backend->holdOpen_);
    ASSERT_FALSE(m_audio->startCapture(fakeDeviceConfig(), {}));
    m_backend->release(m_backend->holdOpen_);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->closeCalls_ == 1; }));

    // The control thread reopens for business just after that close returns.
    bool started = false;
    const auto giveUpAt = std::chrono::steady_clock::now() + kSignalWait;
    while (!started && std::chrono::steady_clock::now() < giveUpAt) {
        started = m_audio->startCapture(fakeDeviceConfig(), {});
        std::this_thread::yield();
    }

    ASSERT_TRUE(started);
    EXPECT_TRUE(m_audio->isCapturing());
    EXPECT_EQ(m_backend->openCalls_, 2);
    EXPECT_EQ(m_backend->startCalls_, 1);
}

TEST_F(AudioCaptureTeardownDeadlineTest, StopCaptureGivesUpOnAHeldClose) {
    ASSERT_TRUE(m_audio->startCapture(fakeDeviceConfig(), {}));
    m_backend->hold(m_backend->holdClose_);
    EngineLogCapture log;

    const auto elapsed = elapsedOf([&] { m_audio->stopCapture(); });

    EXPECT_GE(elapsed, kShortDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->holdClose_.entered_.load(); }));
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isCapturing());
    EXPECT_EQ(m_backend->stopCalls_, 1);
    const std::string text = log.text();
    EXPECT_TRUE(text.contains("backend close did not return")) << text;
    EXPECT_TRUE(text.contains(limitText(kShortDeadline))) << text;
    EXPECT_TRUE(text.contains(kFakeDeviceName)) << text;
    EXPECT_TRUE(text.contains(kPermissionHint)) << text;

    m_backend->release(m_backend->holdClose_);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->closeCalls_ == 1; }));
    EXPECT_FALSE(m_audio->isStreamInOpen());
}

TEST_F(AudioCaptureTeardownDeadlineTest, StopStreamInGivesUpOnAHeldStop) {
    ASSERT_TRUE(m_audio->startCapture(fakeDeviceConfig(), {}));
    m_backend->hold(m_backend->holdStop_);
    EngineLogCapture log;

    const auto elapsed = elapsedOf([&] { m_audio->stopStreamIn(); });

    EXPECT_GE(elapsed, kShortDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->isStreamInRunning());
    const std::string text = log.text();
    EXPECT_TRUE(text.contains("backend stop did not return")) << text;
    EXPECT_TRUE(text.contains(limitText(kShortDeadline))) << text;

    m_backend->release(m_backend->holdStop_);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->closeCalls_ == 1; }));
}

TEST_F(AudioCaptureTeardownDeadlineTest, DestructionReturnsWhileCloseIsHeld) {
    std::atomic<int> deliveries = 0;
    ASSERT_TRUE(m_audio->startCapture(fakeDeviceConfig(), [&](const float *, int, double, bool) {
        ++deliveries;
    }));
    const RtAudioCallback driverCallback = m_backend->callback_;
    m_backend->hold(m_backend->holdClose_);

    const auto elapsed = elapsedOf([&] { m_audio.reset(); });

    EXPECT_GE(elapsed, kShortDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    ASSERT_TRUE(m_backend->waitUntil([this] { return m_backend->holdClose_.entered_.load(); }));
    EXPECT_FALSE(m_backend->destroyed_);
    std::array<float, 8> buffer{};
    driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr);
    EXPECT_EQ(deliveries, 0);

    // The control thread still owns the backend and destroys it once the
    // close returns.
    m_backend->release(m_backend->holdClose_);
    EXPECT_TRUE(m_backend->waitUntil([this] { return m_backend->destroyed_.load(); }));
    EXPECT_EQ(m_backend->closeCalls_, 1);
}

TEST_F(AudioCaptureTest, StopCaptureWaitsOutADeliveryInFlight) {
    std::mutex receiverMutex;
    std::condition_variable receiverChanged;
    bool receiverEntered = false;
    bool receiverMayReturn = false;
    std::atomic<int> deliveries = 0;
    ASSERT_TRUE(m_audio->startCapture(fakeDeviceConfig(), [&](const float *, int, double, bool) {
        std::unique_lock<std::mutex> lock(receiverMutex);
        ++deliveries;
        receiverEntered = true;
        receiverChanged.notify_all();
        receiverChanged.wait(lock, [&] { return receiverMayReturn; });
    }));
    const RtAudioCallback driverCallback = m_backend->callback_;
    std::array<float, 8> buffer{};

    std::thread driver([&] { driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr); });
    {
        std::unique_lock<std::mutex> lock(receiverMutex);
        ASSERT_TRUE(receiverChanged.wait_for(lock, kSignalWait, [&] { return receiverEntered; }));
    }
    std::atomic<bool> stopReturned = false;
    std::thread stopper([&] {
        m_audio->stopCapture();
        stopReturned = true;
    });
    std::this_thread::sleep_for(kShortDeadline);
    EXPECT_FALSE(stopReturned);

    {
        std::lock_guard<std::mutex> lock(receiverMutex);
        receiverMayReturn = true;
    }
    receiverChanged.notify_all();
    driver.join();
    stopper.join();

    EXPECT_TRUE(stopReturned);
    driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr);
    EXPECT_EQ(deliveries, 1);
}

TEST_F(AudioCaptureTest, NoDeliveryReachesTheReceiverOnceStopCaptureReturns) {
    constexpr int kRounds = 50;
    for (int round = 0; round < kRounds; ++round) {
        std::atomic<int> deliveries = 0;
        std::atomic<bool> tornDown = false;
        std::atomic<int> deliveriesAfterTeardown = 0;
        ASSERT_TRUE(
            m_audio->startCapture(fakeDeviceConfig(), [&](const float *, int, double, bool) {
                if (tornDown) {
                    ++deliveriesAfterTeardown;
                }
                ++deliveries;
            })
        );
        const RtAudioCallback driverCallback = m_backend->callback_;
        std::atomic<bool> driverMayStop = false;
        std::atomic<int> callsAfterTeardown = 0;
        std::thread driver([&] {
            std::array<float, 8> buffer{};
            while (!driverMayStop) {
                driverCallback(nullptr, buffer.data(), 4, 0.0, 0, nullptr);
                if (tornDown) {
                    ++callsAfterTeardown;
                }
            }
        });
        while (deliveries == 0) {
            std::this_thread::yield();
        }

        m_audio->stopCapture();
        tornDown = true;
        const int deliveriesAtTeardown = deliveries;
        while (callsAfterTeardown < 1'000) {
            std::this_thread::yield();
        }
        driverMayStop = true;
        driver.join();

        EXPECT_EQ(deliveriesAfterTeardown, 0) << "round " << round;
        EXPECT_EQ(deliveries, deliveriesAtTeardown) << "round " << round;
    }
}

TEST(AudioMonitorTest, IndependentStreamsPreserveRecorderSamplesAndToggleWithoutBackendCalls) {
    auto input = std::make_shared<FakeBackendState>();
    auto output = std::make_shared<FakeBackendState>();
    input->grantedBufferFrames_ = 4;
    output->grantedBufferFrames_ = 4;
    input->streamLatencyFrames_ = 441;
    output->streamLatencyFrames_ = 220;
    Audio audio{
        std::make_unique<FakeAudioInputBackend>(input),
        std::make_unique<FakeAudioInputBackend>(output),
        {kPatientDeadline, kPatientDeadline}
    };
    AudioCaptureConfig config = monitoredFakeDeviceConfig();
    std::vector<float> recorded;
    EngineLogCapture log;

    ASSERT_TRUE(audio.startCapture(config, [&](const float *samples, int frames, double, bool) {
        recorded.assign(samples, samples + frames * 2);
    }));

    EXPECT_TRUE(audio.isInputMonitorEnabled());
    EXPECT_EQ(input->openCalls_, 1);
    EXPECT_EQ(output->openCalls_, 1);
    EXPECT_EQ(output->openedBufferFrames_, input->grantedBufferFrames_);
    std::array<float, 8> first{0.25f, -0.25f, 0.5f, -0.5f, 0.75f, -0.75f, 1.0f, -1.0f};
    ASSERT_TRUE(static_cast<bool>(input->callback_));
    ASSERT_TRUE(static_cast<bool>(output->callback_));
    input->callback_(nullptr, first.data(), 4, 2.0, 0, nullptr);
    std::array<float, 8> monitored{};
    output->callback_(monitored.data(), nullptr, 4, 0.0, 0, nullptr);
    EXPECT_EQ(recorded, std::vector<float>(first.begin(), first.end()));
    EXPECT_EQ(monitored, first);

    std::array<float, 8> stale{9.0f, -9.0f, 8.0f, -8.0f, 7.0f, -7.0f, 6.0f, -6.0f};
    input->callback_(nullptr, stale.data(), 4, 2.5, 0, nullptr);
    audio.setInputMonitorEnabled(false);
    EXPECT_FALSE(audio.isInputMonitorEnabled());
    std::array<float, 8> muted;
    muted.fill(1.0f);
    output->callback_(muted.data(), nullptr, 4, 0.0, 0, nullptr);
    const std::array<float, 8> silence{};
    EXPECT_EQ(muted, silence);

    audio.setInputMonitorEnabled(true);
    EXPECT_TRUE(audio.isInputMonitorEnabled());
    output->callback_(muted.data(), nullptr, 4, 0.0, 0, nullptr);
    std::array<float, 8> fresh{-1.0f, 1.0f, -0.75f, 0.75f, -0.5f, 0.5f, -0.25f, 0.25f};
    input->callback_(nullptr, fresh.data(), 4, 3.0, 0, nullptr);
    output->callback_(monitored.data(), nullptr, 4, 0.0, 0, nullptr);
    EXPECT_EQ(monitored, fresh);
    EXPECT_EQ(input->openCalls_, 1);
    EXPECT_EQ(output->openCalls_, 1);
    EXPECT_EQ(input->startCalls_, 1);
    EXPECT_EQ(output->startCalls_, 1);
    EXPECT_EQ(input->stopCalls_, 0);
    EXPECT_EQ(output->stopCalls_, 0);

    const std::string text = log.text();
    EXPECT_TRUE(text.contains("inputLatencyMs=10.000")) << text;
    EXPECT_TRUE(text.contains("outputLatencyMs=4.989")) << text;
    EXPECT_TRUE(text.contains("deviceRoundTripMs=14.989")) << text;
    audio.stopCapture();
    EXPECT_EQ(input->stopCalls_, 1);
    EXPECT_EQ(output->stopCalls_, 1);
}

TEST(AudioMonitorTest, ReturnedAndThrownOutputOpenFailuresKeepInputLive) {
    for (int arm = 0; arm < 2; ++arm) {
        auto input = std::make_shared<FakeBackendState>();
        auto output = std::make_shared<FakeBackendState>();
        output->openResult_ = arm == 0 ? RTAUDIO_SYSTEM_ERROR : RTAUDIO_NO_ERROR;
        output->throwOpen_ = arm == 1;
        Audio audio{
            std::make_unique<FakeAudioInputBackend>(input),
            std::make_unique<FakeAudioInputBackend>(output),
            {kPatientDeadline, kPatientDeadline}
        };
        int deliveredFrames = 0;

        ASSERT_TRUE(
            audio.startCapture(monitoredFakeDeviceConfig(), countDeliveredFrames(deliveredFrames))
        ) << "arm "
          << arm;

        EXPECT_TRUE(audio.isCapturing()) << "arm " << arm;
        EXPECT_FALSE(audio.isInputMonitorEnabled()) << "arm " << arm;
        EXPECT_EQ(input->openCalls_, 1) << "arm " << arm;
        EXPECT_EQ(output->openCalls_, 1) << "arm " << arm;
        expectRecorderDelivery(input, deliveredFrames);
    }
}

TEST(AudioMonitorTest, SeparateOutputRequestIsBoundedToTenMilliseconds) {
    auto input = std::make_shared<FakeBackendState>();
    auto output = std::make_shared<FakeBackendState>();
    input->hasSeparateOutputDevice_ = true;
    output->hasSeparateOutputDevice_ = true;
    input->grantedBufferFrames_ = 1024;
    Audio audio{
        std::make_unique<FakeAudioInputBackend>(input),
        std::make_unique<FakeAudioInputBackend>(output)
    };
    AudioCaptureConfig config = monitoredFakeDeviceConfig();
    config.monitor_device_name_ = kFakeOutputDeviceName;

    ASSERT_TRUE(audio.startCapture(config, {}));

    EXPECT_TRUE(audio.isInputMonitorEnabled());
    EXPECT_EQ(output->openedDeviceId_, kFakeOutputDeviceId);
    EXPECT_EQ(output->openedBufferFrames_, 441u);
}

TEST(AudioMonitorTest, OutputDeviceIsResolvedWithinItsOwnBackend) {
    auto input = std::make_shared<FakeBackendState>();
    auto output = std::make_shared<FakeBackendState>();
    input->hasSeparateOutputDevice_ = true;
    output->hasSeparateOutputDevice_ = true;
    output->outputDeviceId_ = 19;
    Audio audio{
        std::make_unique<FakeAudioInputBackend>(input),
        std::make_unique<FakeAudioInputBackend>(output)
    };
    AudioCaptureConfig config = monitoredFakeDeviceConfig();
    config.monitor_device_name_ = kFakeOutputDeviceName;
    int deliveredFrames = 0;

    ASSERT_TRUE(audio.startCapture(config, countDeliveredFrames(deliveredFrames)));

    EXPECT_TRUE(audio.isInputMonitorEnabled());
    EXPECT_EQ(output->openedDeviceId_, 19u);
    EXPECT_NE(output->openedDeviceId_, input->outputDeviceId_);
    expectRecorderDelivery(input, deliveredFrames);
}

TEST(AudioMonitorTest, MissingNamedAndDefaultOutputsDegradeWithoutOpening) {
    for (int arm = 0; arm < 2; ++arm) {
        auto input = std::make_shared<FakeBackendState>();
        auto output = std::make_shared<FakeBackendState>();
        output->hasDefaultOutput_ = arm == 0;
        Audio audio{
            std::make_unique<FakeAudioInputBackend>(input),
            std::make_unique<FakeAudioInputBackend>(output)
        };
        AudioCaptureConfig config = monitoredFakeDeviceConfig();
        config.monitor_device_name_ = arm == 0 ? "Missing Output" : "";
        int deliveredFrames = 0;

        EXPECT_TRUE(audio.startCapture(config, countDeliveredFrames(deliveredFrames)))
            << "arm " << arm;

        EXPECT_TRUE(audio.isCapturing()) << "arm " << arm;
        EXPECT_FALSE(audio.isInputMonitorEnabled()) << "arm " << arm;
        EXPECT_EQ(output->deviceInfoCalls_, 1) << "arm " << arm;
        EXPECT_EQ(output->openCalls_, 0) << "arm " << arm;
        expectRecorderDelivery(input, deliveredFrames);
    }
}

TEST(AudioMonitorTest, OutputCapabilitiesAndAdjustedRateDegradeIndependently) {
    for (int arm = 0; arm < 3; ++arm) {
        auto input = std::make_shared<FakeBackendState>();
        auto output = std::make_shared<FakeBackendState>();
        if (arm == 0) {
            output->outputChannels_ = 1;
        } else if (arm == 1) {
            output->sampleRates_ = {48'000};
        } else {
            output->reportedStreamSampleRate_ = 48'000;
        }
        Audio audio{
            std::make_unique<FakeAudioInputBackend>(input),
            std::make_unique<FakeAudioInputBackend>(output)
        };
        int deliveredFrames = 0;

        EXPECT_TRUE(
            audio.startCapture(monitoredFakeDeviceConfig(), countDeliveredFrames(deliveredFrames))
        ) << "arm "
          << arm;

        EXPECT_TRUE(audio.isCapturing()) << "arm " << arm;
        EXPECT_FALSE(audio.isInputMonitorEnabled()) << "arm " << arm;
        if (arm < 2) {
            EXPECT_EQ(output->openCalls_, 0) << "arm " << arm;
        } else {
            EXPECT_EQ(output->openCalls_, 1) << "arm " << arm;
            EXPECT_EQ(output->closeCalls_, 1) << "arm " << arm;
        }
        expectRecorderDelivery(input, deliveredFrames);
    }
}

TEST(AudioMonitorTest, ReturnedAndThrownOutputStartFailuresKeepInputLive) {
    for (int arm = 0; arm < 2; ++arm) {
        auto input = std::make_shared<FakeBackendState>();
        auto output = std::make_shared<FakeBackendState>();
        output->startResult_ = arm == 0 ? RTAUDIO_SYSTEM_ERROR : RTAUDIO_NO_ERROR;
        output->throwStart_ = arm == 1;
        Audio audio{
            std::make_unique<FakeAudioInputBackend>(input),
            std::make_unique<FakeAudioInputBackend>(output)
        };
        int deliveredFrames = 0;

        EXPECT_TRUE(
            audio.startCapture(monitoredFakeDeviceConfig(), countDeliveredFrames(deliveredFrames))
        ) << "arm "
          << arm;

        EXPECT_TRUE(audio.isCapturing()) << "arm " << arm;
        EXPECT_FALSE(audio.isInputMonitorEnabled()) << "arm " << arm;
        EXPECT_EQ(output->openCalls_, 1) << "arm " << arm;
        EXPECT_EQ(output->startCalls_, 1) << "arm " << arm;
        EXPECT_EQ(output->closeCalls_, 1) << "arm " << arm;
        expectRecorderDelivery(input, deliveredFrames);
    }
}

TEST(AudioMonitorTest, HeldOutputOpenUsesTheRemainingArmDeadlineAndKeepsInputLive) {
    auto input = std::make_shared<FakeBackendState>();
    auto output = std::make_shared<FakeBackendState>();
    output->hold(output->holdOpen_);
    auto audio = std::make_unique<Audio>(
        std::make_unique<FakeAudioInputBackend>(input),
        std::make_unique<FakeAudioInputBackend>(output),
        IRAudio::detail::AudioInputDeadlines{kShortDeadline, kPatientDeadline}
    );

    int deliveredFrames = 0;
    bool started = false;
    const auto elapsed = elapsedOf([&] {
        started =
            audio->startCapture(monitoredFakeDeviceConfig(), countDeliveredFrames(deliveredFrames));
    });

    EXPECT_TRUE(started);
    EXPECT_GE(elapsed, kShortDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    EXPECT_TRUE(audio->isCapturing());
    EXPECT_FALSE(audio->isInputMonitorEnabled());
    expectRecorderDelivery(input, deliveredFrames);
    ASSERT_TRUE(output->waitUntil([&] { return output->holdOpen_.entered_.load(); }));
    output->release(output->holdOpen_);
    ASSERT_TRUE(output->waitUntil([&] { return output->closeCalls_ == 1; }));
    audio->stopCapture();
    audio.reset();
    EXPECT_TRUE(output->waitUntil([&] { return output->destroyed_.load(); }));
}

TEST(AudioMonitorTest, HeldOutputStartIsQuarantinedWithoutASecondDeadline) {
    auto input = std::make_shared<FakeBackendState>();
    auto output = std::make_shared<FakeBackendState>();
    output->hold(output->holdStart_);
    auto audio = std::make_unique<Audio>(
        std::make_unique<FakeAudioInputBackend>(input),
        std::make_unique<FakeAudioInputBackend>(output),
        IRAudio::detail::AudioInputDeadlines{kShortDeadline, kPatientDeadline}
    );

    int deliveredFrames = 0;
    bool started = false;
    const auto elapsed = elapsedOf([&] {
        started =
            audio->startCapture(monitoredFakeDeviceConfig(), countDeliveredFrames(deliveredFrames));
    });

    EXPECT_TRUE(started);
    EXPECT_GE(elapsed, kShortDeadline);
    EXPECT_LT(elapsed, kBoundedReturn);
    EXPECT_TRUE(audio->isCapturing());
    EXPECT_FALSE(audio->isInputMonitorEnabled());
    expectRecorderDelivery(input, deliveredFrames);
    ASSERT_TRUE(output->waitUntil([&] { return output->holdStart_.entered_.load(); }));
    output->release(output->holdStart_);
    ASSERT_TRUE(output->waitUntil([&] { return output->closeCalls_ == 1; }));
    audio->stopCapture();
    audio.reset();
    EXPECT_TRUE(output->waitUntil([&] { return output->destroyed_.load(); }));
}

TEST(AudioMonitorTest, EnablingWithoutAnOutputWarnsPerRequestAndRemainsFalse) {
    auto backend = std::make_shared<FakeBackendState>();
    Audio audio{std::make_unique<FakeAudioInputBackend>(backend)};
    EngineLogCapture log;

    audio.setInputMonitorEnabled(true);
    audio.setInputMonitorEnabled(true);

    EXPECT_FALSE(audio.isInputMonitorEnabled());
    EXPECT_EQ(countOf(log.text(), "no monitor output stream is active"), 2);
}

// The fake backend delivers at 44.1 kHz, so one meter window is 4410 frames
// and a 100 Hz sine completes ten whole cycles in it.
constexpr int kMeterRate = 44'100;
constexpr int kMeterWindowFrames = 4'410;
constexpr float kSineHz = 100.0f;
constexpr float kHalfScalePeakDbfs = -6.02f;
constexpr float kHalfScaleSineRmsDbfs = -9.03f;
constexpr float kQuarterScalePeakDbfs = -12.04f;
constexpr float kQuarterScaleSineRmsDbfs = -15.05f;
constexpr float kLevelTolerance = 0.5f;

// Feeds `frames` stereo frames of a sine with per-channel amplitudes, its
// phase continuing from `startFrame`, in driver-sized buffers.
void deliverStereoSine(
    const RtAudioCallback &callback, int startFrame, int frames, float left, float right
) {
    constexpr int kBufferFrames = 1'000;
    std::vector<float> buffer;
    for (int delivered = 0; delivered < frames;) {
        const int count = IRMath::min(kBufferFrames, frames - delivered);
        buffer.assign(static_cast<std::size_t>(count) * 2, 0.0f);
        for (int i = 0; i < count; ++i) {
            const float time = static_cast<float>(startFrame + delivered + i) / kMeterRate;
            const float sample = IRMath::sin(IRMath::kTwoPi * kSineHz * time);
            buffer[static_cast<std::size_t>(i) * 2] = left * sample;
            buffer[static_cast<std::size_t>(i) * 2 + 1] = right * sample;
        }
        callback(nullptr, buffer.data(), static_cast<unsigned int>(count), 0.0, 0, nullptr);
        delivered += count;
    }
}

void expectSilent(const IRAudio::AudioInputLevels &levels) {
    for (unsigned int channel = 0; channel < levels.channelCount_; ++channel) {
        EXPECT_EQ(levels.channels_[channel].peakDbfs_, IRAudio::kAudioInputSilenceDbfs);
        EXPECT_EQ(levels.channels_[channel].rmsDbfs_, IRAudio::kAudioInputSilenceDbfs);
        EXPECT_EQ(levels.channels_[channel].clippedSamples_, 0u);
    }
}

void expectHalfAndQuarterScaleSine(const IRAudio::AudioInputLevels &levels) {
    ASSERT_EQ(levels.channelCount_, 2u);
    EXPECT_NEAR(levels.channels_[0].peakDbfs_, kHalfScalePeakDbfs, kLevelTolerance);
    EXPECT_NEAR(levels.channels_[0].rmsDbfs_, kHalfScaleSineRmsDbfs, kLevelTolerance);
    EXPECT_NEAR(levels.channels_[1].peakDbfs_, kQuarterScalePeakDbfs, kLevelTolerance);
    EXPECT_NEAR(levels.channels_[1].rmsDbfs_, kQuarterScaleSineRmsDbfs, kLevelTolerance);
}

TEST_F(AudioCaptureTest, ArmInputOpensOnceAndEquivalentArmsMakeNoBackendCall) {
    const IRAudio::AudioInputArmResult first = m_audio->armInput(kFakeDeviceName, 48'000);

    ASSERT_TRUE(first.armed_) << first.reason_;
    EXPECT_TRUE(first.reason_.empty());
    EXPECT_TRUE(m_audio->isInputArmed());
    EXPECT_EQ(m_backend->openedChannels_, 2u);
    EXPECT_EQ(m_backend->openedSampleRate_, 44'100u);

    // The same request, the rate the first was substituted to, and the
    // default input, which is the same device.
    EXPECT_TRUE(m_audio->armInput(kFakeDeviceName, 48'000).armed_);
    EXPECT_TRUE(m_audio->armInput(kFakeDeviceName, 44'100).armed_);
    EXPECT_TRUE(m_audio->armInput("", 44'100).armed_);
    EXPECT_EQ(m_backend->openCalls_, 1);
    EXPECT_EQ(m_backend->startCalls_, 1);
    EXPECT_EQ(m_backend->stopCalls_, 0);

    m_audio->disarmInput();

    EXPECT_FALSE(m_audio->isInputArmed());
    EXPECT_FALSE(m_audio->isStreamInOpen());
    EXPECT_FALSE(m_audio->getInputLevels().has_value());
    EXPECT_EQ(m_backend->stopCalls_, 1);
    EXPECT_EQ(m_backend->closeCalls_, 1);

    m_audio->disarmInput();

    EXPECT_EQ(m_backend->stopCalls_, 1);
    EXPECT_EQ(m_backend->closeCalls_, 1);
}

TEST_F(AudioCaptureTest, ArmInputFailuresReturnReasonsAndLeaveNoHold) {
    const IRAudio::AudioInputArmResult missing = m_audio->armInput("Missing Device", 44'100);
    EXPECT_FALSE(missing.armed_);
    EXPECT_TRUE(missing.reason_.contains("not found")) << missing.reason_;
    EXPECT_TRUE(missing.reason_.contains("Missing Device")) << missing.reason_;
    EXPECT_EQ(m_backend->openCalls_, 0);

    m_backend->openResult_ = RTAUDIO_SYSTEM_ERROR;
    const IRAudio::AudioInputArmResult openFailed = m_audio->armInput(kFakeDeviceName, 44'100);
    EXPECT_FALSE(openFailed.armed_);
    EXPECT_TRUE(openFailed.reason_.contains(kFakeDeviceName)) << openFailed.reason_;
    EXPECT_TRUE(openFailed.reason_.contains(kOpenErrorText)) << openFailed.reason_;

    m_backend->openResult_ = RTAUDIO_NO_ERROR;
    m_backend->startResult_ = RTAUDIO_SYSTEM_ERROR;
    const IRAudio::AudioInputArmResult startFailed = m_audio->armInput(kFakeDeviceName, 44'100);
    EXPECT_FALSE(startFailed.armed_);
    EXPECT_TRUE(startFailed.reason_.contains(kStartErrorText)) << startFailed.reason_;

    EXPECT_FALSE(m_audio->isInputArmed());
    EXPECT_FALSE(m_audio->getInputLevels().has_value());
}

TEST(AudioInputArmTest, MeterReportsEachChannelsWindowAndASwitchClearsIt) {
    auto backend = std::make_shared<FakeBackendState>();
    backend->hasSecondInputDevice_ = true;
    Audio audio{std::make_unique<FakeAudioInputBackend>(backend), {kPatientDeadline, kPatientDeadline}};
    ASSERT_TRUE(audio.armInput(kFakeDeviceName, kMeterRate).armed_);

    std::optional<IRAudio::AudioInputLevels> levels = audio.getInputLevels();
    ASSERT_TRUE(levels.has_value());
    EXPECT_EQ(levels->channelCount_, 2u);
    EXPECT_EQ(levels->sampleRate_, static_cast<unsigned int>(kMeterRate));
    EXPECT_EQ(levels->generation_, 0u);
    expectSilent(*levels);

    // One complete window, then 590 frames into the next: the buffer that
    // crosses the boundary splits between them.
    deliverStereoSine(backend->callback_, 0, 5'000, 0.5f, 0.25f);
    levels = audio.getInputLevels();
    ASSERT_TRUE(levels.has_value());
    EXPECT_EQ(levels->generation_, 1u);
    expectHalfAndQuarterScaleSine(*levels);
    EXPECT_EQ(levels->channels_[0].clippedSamples_, 0u);
    EXPECT_EQ(levels->channels_[1].clippedSamples_, 0u);

    // Silence completes the second window, carrying one full-scale sample on
    // the right channel only.
    const int rest = kMeterWindowFrames - (5'000 - kMeterWindowFrames);
    std::vector<float> silence(static_cast<std::size_t>(rest) * 2, 0.0f);
    silence[1] = 1.0f;
    backend->callback_(nullptr, silence.data(), static_cast<unsigned int>(rest), 0.0, 0, nullptr);
    levels = audio.getInputLevels();
    ASSERT_TRUE(levels.has_value());
    EXPECT_EQ(levels->generation_, 2u);
    EXPECT_EQ(levels->channels_[0].clippedSamples_, 0u);
    EXPECT_EQ(levels->channels_[1].clippedSamples_, 1u);
    EXPECT_NEAR(levels->channels_[1].peakDbfs_, 0.0f, 0.01f);

    const IRAudio::AudioInputArmResult switched = audio.armInput(kSecondInputDeviceName, kMeterRate);
    ASSERT_TRUE(switched.armed_) << switched.reason_;
    EXPECT_EQ(backend->openCalls_, 2);
    EXPECT_EQ(backend->openedDeviceId_, kSecondInputDeviceId);
    levels = audio.getInputLevels();
    ASSERT_TRUE(levels.has_value());
    EXPECT_EQ(levels->generation_, 0u);
    expectSilent(*levels);
}

TEST(AudioInputArmTest, MonoDeviceArmsAndMetersOneChannel) {
    auto backend = std::make_shared<FakeBackendState>();
    backend->inputChannels_ = 1;
    Audio audio{std::make_unique<FakeAudioInputBackend>(backend), {kPatientDeadline, kPatientDeadline}};

    ASSERT_TRUE(audio.armInput(kFakeDeviceName, kMeterRate).armed_);

    EXPECT_EQ(backend->openedChannels_, 1u);
    EXPECT_EQ(audio.getCaptureChannels(), 1);
    std::vector<float> fullScale(kMeterWindowFrames, 0.5f);
    backend->callback_(
        nullptr,
        fullScale.data(),
        static_cast<unsigned int>(kMeterWindowFrames),
        0.0,
        0,
        nullptr
    );
    const std::optional<IRAudio::AudioInputLevels> levels = audio.getInputLevels();
    ASSERT_TRUE(levels.has_value());
    EXPECT_EQ(levels->channelCount_, 1u);
    EXPECT_EQ(levels->generation_, 1u);
    EXPECT_NEAR(levels->channels_[0].peakDbfs_, kHalfScalePeakDbfs, kLevelTolerance);
    EXPECT_NEAR(levels->channels_[0].rmsDbfs_, kHalfScalePeakDbfs, kLevelTolerance);
}

TEST(AudioInputArmTest, CaptureSharesAnArmedInputAndEachHoldReleasesOnlyItself) {
    auto input = std::make_shared<FakeBackendState>();
    auto output = std::make_shared<FakeBackendState>();
    Audio audio{
        std::make_unique<FakeAudioInputBackend>(input),
        std::make_unique<FakeAudioInputBackend>(output),
        {kPatientDeadline, kPatientDeadline}
    };
    ASSERT_TRUE(audio.armInput(kFakeDeviceName, kMeterRate).armed_);
    int recordedFrames = 0;

    // The recorder asks for 48 kHz, which this device substitutes to the
    // armed stream's rate.
    ASSERT_TRUE(audio.startCapture(fakeDeviceConfig(), countDeliveredFrames(recordedFrames)));

    EXPECT_EQ(input->openCalls_, 1);
    EXPECT_EQ(input->startCalls_, 1);
    EXPECT_EQ(audio.getCaptureSampleRate(), kMeterRate);
    EXPECT_EQ(audio.getCaptureChannels(), 2);
    deliverStereoSine(input->callback_, 0, kMeterWindowFrames, 0.5f, 0.25f);
    EXPECT_EQ(recordedFrames, kMeterWindowFrames);
    ASSERT_TRUE(audio.getInputLevels().has_value());
    EXPECT_EQ(audio.getInputLevels()->generation_, 1u);

    audio.stopCapture();

    EXPECT_EQ(input->stopCalls_, 0);
    EXPECT_EQ(input->closeCalls_, 0);
    EXPECT_TRUE(audio.isInputArmed());
    deliverStereoSine(input->callback_, kMeterWindowFrames, kMeterWindowFrames, 0.5f, 0.25f);
    EXPECT_EQ(recordedFrames, kMeterWindowFrames);
    ASSERT_TRUE(audio.getInputLevels().has_value());
    EXPECT_EQ(audio.getInputLevels()->generation_, 2u);

    audio.disarmInput();

    EXPECT_EQ(input->stopCalls_, 1);
    EXPECT_EQ(input->closeCalls_, 1);
    EXPECT_FALSE(audio.getInputLevels().has_value());
    EXPECT_EQ(output->openCalls_, 0);
    EXPECT_EQ(output->startCalls_, 0);
}

TEST(AudioInputArmTest, SwitchingDevicesOutsideATakeCarriesTheCaptureCallback) {
    auto input = std::make_shared<FakeBackendState>();
    auto output = std::make_shared<FakeBackendState>();
    input->hasSecondInputDevice_ = true;
    Audio audio{
        std::make_unique<FakeAudioInputBackend>(input),
        std::make_unique<FakeAudioInputBackend>(output),
        {kPatientDeadline, kPatientDeadline}
    };
    int recordedFrames = 0;
    ASSERT_TRUE(audio.startCapture(fakeDeviceConfig(), countDeliveredFrames(recordedFrames)));
    ASSERT_TRUE(audio.armInput(kFakeDeviceName, 48'000).armed_);
    EXPECT_EQ(input->openCalls_, 1);

    const IRAudio::AudioInputArmResult switched = audio.armInput(kSecondInputDeviceName, 48'000);

    ASSERT_TRUE(switched.armed_) << switched.reason_;
    EXPECT_EQ(input->stopCalls_, 1);
    EXPECT_EQ(input->closeCalls_, 1);
    EXPECT_EQ(input->openCalls_, 2);
    EXPECT_EQ(input->startCalls_, 2);
    EXPECT_EQ(input->openedDeviceId_, kSecondInputDeviceId);
    expectRecorderDelivery(input, recordedFrames);
    EXPECT_EQ(output->openCalls_, 0);
    EXPECT_EQ(output->startCalls_, 0);
    EXPECT_EQ(output->stopCalls_, 0);
    EXPECT_EQ(output->closeCalls_, 0);
}

TEST(AudioInputArmTest, SwitchingDevicesDuringATakeIsRefused) {
    auto backend = std::make_shared<FakeBackendState>();
    backend->hasSecondInputDevice_ = true;
    Audio audio{std::make_unique<FakeAudioInputBackend>(backend), {kPatientDeadline, kPatientDeadline}};
    ASSERT_TRUE(audio.startCapture(fakeDeviceConfig(), {}));
    audio.setCaptureRecording(true);

    const IRAudio::AudioInputArmResult refused = audio.armInput(kSecondInputDeviceName, 48'000);

    EXPECT_FALSE(refused.armed_);
    EXPECT_TRUE(refused.reason_.contains("recording")) << refused.reason_;
    EXPECT_FALSE(audio.isInputArmed());
    EXPECT_TRUE(audio.isCapturing());
    EXPECT_EQ(backend->openCalls_, 1);
    EXPECT_EQ(backend->closeCalls_, 0);

    EXPECT_TRUE(audio.armInput(kFakeDeviceName, 48'000).armed_);
    audio.setCaptureRecording(false);
    EXPECT_TRUE(audio.armInput(kSecondInputDeviceName, 48'000).armed_);
    EXPECT_EQ(backend->openCalls_, 2);
}

TEST(AudioInputFacadeTest, ArmReadAndDisarmThroughTheManager) {
    auto backend = std::make_shared<FakeBackendState>();
    IRAudio::AudioManager audioManager{std::make_unique<FakeAudioInputBackend>(backend), nullptr};
    EXPECT_FALSE(IRAudio::audioInputLevels().has_value());

    const IRAudio::AudioInputArmResult armed = IRAudio::armAudioInput(kFakeDeviceName, kMeterRate);

    ASSERT_TRUE(armed.armed_) << armed.reason_;
    deliverStereoSine(backend->callback_, 0, kMeterWindowFrames, 0.5f, 0.25f);
    const std::optional<IRAudio::AudioInputLevels> levels = IRAudio::audioInputLevels();
    ASSERT_TRUE(levels.has_value());
    EXPECT_EQ(levels->generation_, 1u);
    expectHalfAndQuarterScaleSine(*levels);
    EXPECT_FALSE(IRAudio::armAudioInput("Missing Device", kMeterRate).armed_);

    IRAudio::disarmAudioInput();

    EXPECT_FALSE(IRAudio::audioInputLevels().has_value());
    EXPECT_EQ(backend->openCalls_, 1);
    EXPECT_EQ(backend->closeCalls_, 1);
}

} // namespace
