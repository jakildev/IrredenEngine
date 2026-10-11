#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/audio/audio_manager.hpp>
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
    int openCalls_ = 0;
    int startCalls_ = 0;
    int stopCalls_ = 0;
    int closeCalls_ = 0;
};

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
        m_state->callback_ = std::move(callback);
        ++m_state->openCalls_;
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

} // namespace
