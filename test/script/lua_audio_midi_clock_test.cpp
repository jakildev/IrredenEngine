#include <gtest/gtest.h>

#include <irreden/ir_audio.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>
#include <vector>

// The Lua MIDI-clock surface, headless: with no output port open every call
// refuses or no-ops (never starts a thread against a missing port), the
// system status bytes are bound by name, and a system message reaches a Lua
// observer with its whole status byte and no channel.

namespace {

using IRAudio::kMidiStatus_START;
using IRAudio::kMidiStatus_STOP;
using IRAudio::kMidiStatus_TIMING_CLOCK;

// Same member order as the observer test: m_audio_manager last so it destructs
// first, dropping a captured sol::function while the sol::state is alive.
class LuaAudioMidiClockTest : public ::testing::Test {
  protected:
    LuaAudioMidiClockTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{}
        , m_audio_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    ~LuaAudioMidiClockTest() override {
        IRAudio::clearOutboundMidiObserver();
    }

    bool luaBool(const char *expr) {
        auto r = m_lua.lua().safe_script(std::string("return ") + expr, sol::script_pass_on_error);
        return r.valid() && r.get<bool>();
    }

    int luaInt(const char *expr) {
        auto r = m_lua.lua().safe_script(std::string("return ") + expr, sol::script_pass_on_error);
        return r.valid() ? r.get<int>() : -1;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRAudio::AudioManager m_audio_manager;
};

TEST_F(LuaAudioMidiClockTest, ClockFunctionsAndSystemStatusesAreBound) {
    EXPECT_TRUE(luaBool("type(IRAudio.startMidiClock) == 'function'"));
    EXPECT_TRUE(luaBool("type(IRAudio.setMidiClockBpm) == 'function'"));
    EXPECT_TRUE(luaBool("type(IRAudio.stopMidiClock) == 'function'"));
    EXPECT_TRUE(luaBool("type(IRAudio.isMidiClockRunning) == 'function'"));
    EXPECT_TRUE(luaBool("type(IRAudio.midiClockPulses) == 'function'"));
    EXPECT_EQ(luaInt("IRAudio.MidiStatus.TIMING_CLOCK"), kMidiStatus_TIMING_CLOCK);
    EXPECT_EQ(luaInt("IRAudio.MidiStatus.START"), kMidiStatus_START);
    EXPECT_EQ(luaInt("IRAudio.MidiStatus.CONTINUE"), IRAudio::kMidiStatus_CONTINUE);
    EXPECT_EQ(luaInt("IRAudio.MidiStatus.STOP"), kMidiStatus_STOP);
}

TEST_F(LuaAudioMidiClockTest, HeadlessStartIsRefusedAndTheRestNoOps) {
    ASSERT_TRUE(IRAudio::midiOutOpenPorts().empty()); // headless: no device

    auto r = m_lua.lua().safe_script(
        R"(
            observed = 0
            IRAudio.onMidiSent(function() observed = observed + 1 end)
            started = IRAudio.startMidiClock(0, 120)
            retuned = IRAudio.setMidiClockBpm(140)
            IRAudio.stopMidiClock()
            running = IRAudio.isMidiClockRunning()
            pulses = IRAudio.midiClockPulses()
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(r.valid());
    EXPECT_FALSE(luaBool("started"));
    EXPECT_FALSE(luaBool("retuned"));
    EXPECT_FALSE(luaBool("running"));
    EXPECT_EQ(luaInt("pulses"), 0);
    EXPECT_EQ(luaInt("observed"), 0); // nothing left, not even a START
    EXPECT_FALSE(IRAudio::isMidiClockRunning());
}

TEST_F(LuaAudioMidiClockTest, SystemMessageReachesTheObserverWholeWithNoChannel) {
    auto r = m_lua.lua().safe_script(
        R"(
            captured = nil
            IRAudio.onMidiSent(function(status, channel, data1, data2, port)
                captured = { status = status, channel = channel, port = port }
            end)
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(r.valid());

    IRAudio::sendMidiMessage(3, std::vector<unsigned char>{kMidiStatus_START});

    sol::table captured = m_lua.lua()["captured"];
    ASSERT_TRUE(captured.valid());
    EXPECT_EQ(captured["status"].get<int>(), kMidiStatus_START); // the whole byte
    EXPECT_EQ(captured["channel"].get<int>(), -1);
    EXPECT_EQ(captured["port"].get<int>(), 3);
}

} // namespace
