#include <gtest/gtest.h>

#include <irreden/ir_audio.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <vector>

// Lua-observable outbound MIDI hook (engine #1869). The observer is registered
// at the C++ `IRAudio::sendMidiMessage` choke point, so a Lua handler sees ALL
// outbound traffic — including messages the C++ ECS audio path emits, not just
// what Lua sent. These cases drive sends through the C++ free function (the
// real choke point) and assert the Lua handler captured the reconciled fields.
// No MIDI device is opened: the observer must fire regardless (headless monitor).

namespace {

using IRAudio::buildMidiStatus;
using IRAudio::kMidiStatus_CONTROL_CHANGE;
using IRAudio::kMidiStatus_NOTE_ON;
using IRAudio::kMidiStatus_PROGRAM_CHANGE;

// Owns an AudioManager (its ctor sets IRAudio::g_audioManager, so the
// sendMidiMessage free functions resolve) plus a LuaScript with the IRAudio
// bindings. m_audio_manager is declared LAST so it destructs FIRST — dropping
// any captured sol::function while the sol::state is still alive (the same
// member-order contract World uses; see ir_audio.cpp).
class LuaAudioMidiObserverTest : public ::testing::Test {
  protected:
    LuaAudioMidiObserverTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{}
        , m_audio_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    ~LuaAudioMidiObserverTest() override {
        // Explicit even though member order already guarantees safety: clear the
        // observer (a captured sol::function) before the sol::state tears down.
        IRAudio::clearOutboundMidiObserver();
    }

    // Registers a Lua observer that records the last message into `captured`
    // and counts invocations in `callCount`.
    void registerCapturingObserver() {
        auto result = m_lua.lua().safe_script(
            R"(
                captured = nil
                callCount = 0
                IRAudio.onMidiSent(function(status, channel, data1, data2, port)
                    callCount = callCount + 1
                    captured = {
                        status = status, channel = channel,
                        data1 = data1, data2 = data2, port = port,
                    }
                end)
            )",
            sol::script_pass_on_error
        );
        ASSERT_TRUE(result.valid());
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRAudio::AudioManager m_audio_manager;
};

TEST_F(LuaAudioMidiObserverTest, OnMidiSentAndMidiStatusBound) {
    auto isFunction = [&](const char *expr) {
        auto r = m_lua.lua().safe_script(
            std::string("return type(") + expr + ") == 'function'", sol::script_pass_on_error
        );
        return r.valid() && r.get<bool>();
    };
    EXPECT_TRUE(isFunction("IRAudio.onMidiSent"));
    EXPECT_TRUE(isFunction("IRAudio.clearMidiObserver"));
    // The status table mirrors the kMidiStatus_* constants so a handler compares
    // against named values, not magic bytes.
    EXPECT_EQ(m_lua.lua()["IRAudio"]["MidiStatus"]["NOTE_ON"].get<int>(), kMidiStatus_NOTE_ON);
    EXPECT_EQ(
        m_lua.lua()["IRAudio"]["MidiStatus"]["CONTROL_CHANGE"].get<int>(), kMidiStatus_CONTROL_CHANGE
    );
    EXPECT_EQ(
        m_lua.lua()["IRAudio"]["MidiStatus"]["PITCH_BEND"].get<int>(),
        static_cast<int>(IRAudio::kMidiStatus_PITCH_BEND)
    );
}

TEST_F(LuaAudioMidiObserverTest, CapturesNoteOnDrivenThroughCppPath) {
    registerCapturingObserver();

    // NOTE_ON, channel 5, note 60, velocity 100 — emitted through the C++ choke
    // point exactly as a prefab audio system would.
    const std::vector<unsigned char> message{buildMidiStatus(kMidiStatus_NOTE_ON, 5), 60, 100};
    IRAudio::sendMidiMessage(message);

    sol::table captured = m_lua.lua()["captured"];
    ASSERT_TRUE(captured.valid());
    EXPECT_EQ(captured["status"].get<int>(), kMidiStatus_NOTE_ON); // type byte, channel stripped
    EXPECT_EQ(captured["channel"].get<int>(), 5);
    EXPECT_EQ(captured["data1"].get<int>(), 60);
    EXPECT_EQ(captured["data2"].get<int>(), 100);
    EXPECT_EQ(captured["port"].get<int>(), -1); // default-port overload
    EXPECT_EQ(m_lua.lua()["callCount"].get<int>(), 1);
}

TEST_F(LuaAudioMidiObserverTest, FiresWithNoOutputPortOpen) {
    registerCapturingObserver();
    ASSERT_TRUE(IRAudio::midiOutOpenPorts().empty()); // headless: no device

    IRAudio::sendMidiMessage({buildMidiStatus(kMidiStatus_CONTROL_CHANGE, 0), 7, 99});

    // The observer fires even though the hardware send no-ops — the whole point
    // of the headless-monitor use case.
    EXPECT_EQ(m_lua.lua()["callCount"].get<int>(), 1);
    sol::table captured = m_lua.lua()["captured"];
    ASSERT_TRUE(captured.valid());
    EXPECT_EQ(captured["status"].get<int>(), kMidiStatus_CONTROL_CHANGE);
    EXPECT_EQ(captured["data1"].get<int>(), 7);
    EXPECT_EQ(captured["data2"].get<int>(), 99);
}

TEST_F(LuaAudioMidiObserverTest, PortIndexOverloadForwardsPort) {
    registerCapturingObserver();

    // The port-targeted overload forwards the port even when it isn't open.
    IRAudio::sendMidiMessage(7, {buildMidiStatus(kMidiStatus_NOTE_ON, 2), 64, 80});

    sol::table captured = m_lua.lua()["captured"];
    ASSERT_TRUE(captured.valid());
    EXPECT_EQ(captured["port"].get<int>(), 7);
    EXPECT_EQ(captured["channel"].get<int>(), 2);
}

TEST_F(LuaAudioMidiObserverTest, TwoByteMessageLeavesData2Zero) {
    registerCapturingObserver();

    // PROGRAM_CHANGE serializes to 2 bytes; data2 must reconstruct to 0 without
    // reading past the vector.
    IRAudio::sendMidiMessage({buildMidiStatus(kMidiStatus_PROGRAM_CHANGE, 3), 42});

    sol::table captured = m_lua.lua()["captured"];
    ASSERT_TRUE(captured.valid());
    EXPECT_EQ(captured["status"].get<int>(), static_cast<int>(kMidiStatus_PROGRAM_CHANGE));
    EXPECT_EQ(captured["data1"].get<int>(), 42);
    EXPECT_EQ(captured["data2"].get<int>(), 0);
}

TEST_F(LuaAudioMidiObserverTest, ClearMidiObserverStopsCallbacks) {
    registerCapturingObserver();
    IRAudio::sendMidiMessage({buildMidiStatus(kMidiStatus_NOTE_ON, 0), 60, 100});
    ASSERT_EQ(m_lua.lua()["callCount"].get<int>(), 1);

    auto cleared = m_lua.lua().safe_script("IRAudio.clearMidiObserver()", sol::script_pass_on_error);
    ASSERT_TRUE(cleared.valid());

    IRAudio::sendMidiMessage({buildMidiStatus(kMidiStatus_NOTE_ON, 0), 62, 100});
    EXPECT_EQ(m_lua.lua()["callCount"].get<int>(), 1); // no further callbacks
}

TEST_F(LuaAudioMidiObserverTest, LastRegistrationWins) {
    registerCapturingObserver();
    // A second registration replaces the first (single observer, like the
    // inbound AudioInputCallback).
    auto result = m_lua.lua().safe_script(
        R"(
            secondCount = 0
            IRAudio.onMidiSent(function() secondCount = secondCount + 1 end)
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid());

    IRAudio::sendMidiMessage({buildMidiStatus(kMidiStatus_NOTE_ON, 0), 60, 100});

    EXPECT_EQ(m_lua.lua()["callCount"].get<int>(), 0);  // first observer detached
    EXPECT_EQ(m_lua.lua()["secondCount"].get<int>(), 1); // second observer fires
}

} // namespace
