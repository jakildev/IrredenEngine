#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr unsigned int kDeviceId = 7;

struct MonitorBackendState {
    RtAudioCallback callback_;
    RtAudioErrorType openResult_ = RTAUDIO_NO_ERROR;
    int openCalls_ = 0;
    int startCalls_ = 0;
    int stopCalls_ = 0;
    int closeCalls_ = 0;
};

constexpr const char *kOpenErrorText = "MonitorBackend: device busy";

class MonitorBackend final : public IRAudio::detail::IAudioInputBackend {
  public:
    explicit MonitorBackend(std::shared_ptr<MonitorBackendState> state)
        : m_state(std::move(state)) {}

    std::vector<unsigned int> getDeviceIds() override {
        return {kDeviceId};
    }

    RtAudio::DeviceInfo getDeviceInfo(unsigned int deviceId) override {
        RtAudio::DeviceInfo info;
        info.ID = deviceId;
        info.name = "Lua Monitor Device";
        info.inputChannels = 2;
        info.outputChannels = 2;
        info.isDefaultInput = true;
        info.isDefaultOutput = true;
        info.sampleRates = {44'100};
        return info;
    }

    RtAudioErrorType openInputStream(
        RtAudio::StreamParameters &,
        unsigned int,
        unsigned int &bufferFrames,
        RtAudioCallback callback
    ) override {
        bufferFrames = 4;
        ++m_state->openCalls_;
        if (m_state->openResult_ != RTAUDIO_NO_ERROR) {
            m_error = kOpenErrorText;
            return m_state->openResult_;
        }
        m_state->callback_ = std::move(callback);
        return RTAUDIO_NO_ERROR;
    }

    RtAudioErrorType openOutputStream(
        RtAudio::StreamParameters &parameters,
        unsigned int sampleRate,
        unsigned int &bufferFrames,
        RtAudioCallback callback
    ) override {
        return openInputStream(parameters, sampleRate, bufferFrames, std::move(callback));
    }

    RtAudioErrorType startStream() override {
        ++m_state->startCalls_;
        return RTAUDIO_NO_ERROR;
    }

    RtAudioErrorType stopStream() override {
        ++m_state->stopCalls_;
        return RTAUDIO_NO_ERROR;
    }

    void closeStream() override {
        ++m_state->closeCalls_;
        m_state->callback_ = {};
    }

    const std::string &getErrorText() override {
        return m_error;
    }

    unsigned int getStreamSampleRate() override {
        return 44'100;
    }

    long getStreamLatency() override {
        return 0;
    }

  private:
    std::shared_ptr<MonitorBackendState> m_state;
    std::string m_error;
};

class LuaAudioMonitorTest : public testing::Test {
  protected:
    LuaAudioMonitorTest()
        : m_input(std::make_shared<MonitorBackendState>())
        , m_output(std::make_shared<MonitorBackendState>())
        , m_lua{}
        , m_entityManager{}
        , m_systemManager{}
        , m_audioManager{
              std::make_unique<MonitorBackend>(m_input), std::make_unique<MonitorBackend>(m_output)
          } {
        m_lua.bindLuaDrivenEcs();
    }

    std::shared_ptr<MonitorBackendState> m_input;
    std::shared_ptr<MonitorBackendState> m_output;
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IRAudio::AudioManager m_audioManager;
};

TEST_F(LuaAudioMonitorTest, BindingTogglesTheEffectiveGateWithoutBackendCalls) {
    IRAudio::AudioCaptureConfig config;
    config.device_name_ = "Lua Monitor Device";
    config.sample_rate_ = 44'100;
    config.channels_ = 2;
    config.monitor_enabled_ = true;
    config.monitor_device_name_ = "Lua Monitor Device";
    ASSERT_TRUE(m_audioManager.getAudio().startCapture(config, {}));
    const int inputOpenCalls = m_input->openCalls_;
    const int outputOpenCalls = m_output->openCalls_;

    sol::protected_function_result result = m_lua.lua().safe_script(
        R"lua(
            assert(type(IRAudio.setInputMonitorEnabled) == "function")
            assert(type(IRAudio.isInputMonitorEnabled) == "function")
            local initiallyEnabled = IRAudio.isInputMonitorEnabled()
            IRAudio.setInputMonitorEnabled(false)
            local disabled = not IRAudio.isInputMonitorEnabled()
            IRAudio.setInputMonitorEnabled(true)
            return initiallyEnabled, disabled, IRAudio.isInputMonitorEnabled()
        )lua",
        sol::script_pass_on_error
    );

    ASSERT_TRUE(result.valid());
    const auto [initiallyEnabled, disabled, reenabled] = result.get<std::tuple<bool, bool, bool>>();
    EXPECT_TRUE(initiallyEnabled);
    EXPECT_TRUE(disabled);
    EXPECT_TRUE(reenabled);
    EXPECT_EQ(m_input->openCalls_, inputOpenCalls);
    EXPECT_EQ(m_output->openCalls_, outputOpenCalls);
    EXPECT_EQ(m_input->stopCalls_, 0);
    EXPECT_EQ(m_output->stopCalls_, 0);
}

TEST_F(LuaAudioMonitorTest, ArmIsIdempotentAndFailuresReturnAReason) {
    sol::protected_function_result armed = m_lua.lua().safe_script(
        R"lua(
            local ok, reason = IRAudio.armAudioInput("Lua Monitor Device", 44100)
            local again, againReason = IRAudio.armAudioInput("Lua Monitor Device", 44100)
            local missing, missingReason = IRAudio.armAudioInput("No Such Device", 44100)
            return ok, reason == nil, again, againReason == nil,
                missing, type(missingReason) == "string" and #missingReason > 0,
                IRAudio.audioInputLevels() ~= nil
        )lua",
        sol::script_pass_on_error
    );

    ASSERT_TRUE(armed.valid()) << sol::error(armed).what();
    const auto [ok, noReason, again, noAgainReason, missing, missingReason, levelsLive] =
        armed.get<std::tuple<bool, bool, bool, bool, bool, bool, bool>>();
    EXPECT_TRUE(ok);
    EXPECT_TRUE(noReason);
    EXPECT_TRUE(again);
    EXPECT_TRUE(noAgainReason);
    EXPECT_FALSE(missing);
    EXPECT_TRUE(missingReason);
    EXPECT_TRUE(levelsLive);
    EXPECT_EQ(m_input->openCalls_, 1);
    EXPECT_EQ(m_input->startCalls_, 1);
    EXPECT_EQ(m_output->openCalls_, 0);

    m_input->openResult_ = RTAUDIO_SYSTEM_ERROR;
    sol::protected_function_result disarmed = m_lua.lua().safe_script(
        R"lua(
            IRAudio.disarmAudioInput()
            local closed = IRAudio.audioInputLevels() == nil
            local failed, reason = IRAudio.armAudioInput("Lua Monitor Device", 44100)
            IRAudio.disarmAudioInput()
            IRAudio.disarmAudioInput()
            return closed, failed, reason, IRAudio.audioInputLevels() == nil
        )lua",
        sol::script_pass_on_error
    );

    ASSERT_TRUE(disarmed.valid()) << sol::error(disarmed).what();
    const auto [closed, failed, reason, stillClosed] =
        disarmed.get<std::tuple<bool, bool, std::string, bool>>();
    EXPECT_TRUE(closed);
    EXPECT_FALSE(failed);
    EXPECT_TRUE(reason.contains(kOpenErrorText)) << reason;
    EXPECT_TRUE(stillClosed);
    EXPECT_EQ(m_input->openCalls_, 2);
    EXPECT_EQ(m_input->stopCalls_, 1);
    EXPECT_EQ(m_input->closeCalls_, 1);
}

TEST_F(LuaAudioMonitorTest, LevelsReadBackAKnownSinePerChannel) {
    sol::protected_function_result armed = m_lua.lua().safe_script(
        R"lua(return IRAudio.armAudioInput("Lua Monitor Device", 44100))lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(armed.valid()) << sol::error(armed).what();
    ASSERT_TRUE(armed.get<bool>());

    // One 4410-frame window at 44.1 kHz: ten whole cycles of a 100 Hz sine,
    // half scale on the left channel and quarter scale on the right.
    constexpr int kWindowFrames = 4'410;
    std::vector<float> samples(static_cast<std::size_t>(kWindowFrames) * 2);
    for (int frame = 0; frame < kWindowFrames; ++frame) {
        const float sample =
            IRMath::sin(IRMath::kTwoPi * 100.0f * static_cast<float>(frame) / 44'100.0f);
        samples[static_cast<std::size_t>(frame) * 2] = 0.5f * sample;
        samples[static_cast<std::size_t>(frame) * 2 + 1] = 0.25f * sample;
    }
    ASSERT_TRUE(static_cast<bool>(m_input->callback_));
    m_input->callback_(nullptr, samples.data(), kWindowFrames, 0.0, 0, nullptr);

    sol::protected_function_result read = m_lua.lua().safe_script(
        R"lua(
            local levels = IRAudio.audioInputLevels()
            return #levels, levels.sampleRate, levels.generation,
                levels[1].peakDbfs, levels[1].rmsDbfs, levels[1].clippedSamples,
                levels[2].peakDbfs, levels[2].rmsDbfs, levels[2].clippedSamples
        )lua",
        sol::script_pass_on_error
    );

    ASSERT_TRUE(read.valid()) << sol::error(read).what();
    const auto [channels, sampleRate, generation, leftPeak, leftRms, leftClipped, rightPeak, rightRms, rightClipped] =
        read.get<std::tuple<int, int, int, float, float, int, float, float, int>>();
    EXPECT_EQ(channels, 2);
    EXPECT_EQ(sampleRate, 44'100);
    EXPECT_EQ(generation, 1);
    EXPECT_NEAR(leftPeak, -6.02f, 0.5f);
    EXPECT_NEAR(leftRms, -9.03f, 0.5f);
    EXPECT_NEAR(rightPeak, -12.04f, 0.5f);
    EXPECT_NEAR(rightRms, -15.05f, 0.5f);
    EXPECT_EQ(leftClipped, 0);
    EXPECT_EQ(rightClipped, 0);
}

} // namespace
