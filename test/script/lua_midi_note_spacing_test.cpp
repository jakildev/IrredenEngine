#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>
#include <irreden/audio/systems/system_midi_note_spacing.hpp>
#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <string>

// `IRAudio.setNoteSpacing` / `noteSpacing` over the singleton the triggers
// and `MIDI_NOTE_SPACING` share: every key checked before any write, and
// enabling refused until the system that flushes the queue is registered.
class LuaMidiNoteSpacingTest : public testing::Test {
  protected:
    LuaMidiNoteSpacingTest() {
        m_lua.bindLuaDrivenEcs();
    }

    bool scriptSucceeds(const std::string &source) {
        const sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        return result.valid();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(LuaMidiNoteSpacingTest, ReadsBackWhatItSetsAndRejectsBadInput) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local s = IRAudio.noteSpacing()
        assert(s.enabled == false and s.staggerFrames == 1 and s.maxVoices == 0 and s.maxDelayFrames == 0)
        IRAudio.setNoteSpacing({ staggerFrames = 3, maxVoices = 6, maxDelayFrames = 24 })
        s = IRAudio.noteSpacing()
        assert(s.staggerFrames == 3 and s.maxVoices == 6 and s.maxDelayFrames == 24 and s.enabled == false)
    )lua"));
    const IRComponents::C_MidiNoteSpacing &spacing = IRPrefab::MidiNotes::spacing();
    EXPECT_EQ(spacing.staggerFrames_, 3);
    EXPECT_EQ(spacing.maxVoices_, 6);
    EXPECT_FALSE(scriptSucceeds("IRAudio.setNoteSpacing({ stagger = 3 })"));
    EXPECT_FALSE(scriptSucceeds("IRAudio.setNoteSpacing({ staggerFrames = 0 })"));
    EXPECT_FALSE(scriptSucceeds("IRAudio.setNoteSpacing({ maxVoices = -1 })"));
    EXPECT_FALSE(scriptSucceeds("IRAudio.setNoteSpacing({ enabled = 1 })"));
    // A refused table writes nothing.
    EXPECT_EQ(spacing.staggerFrames_, 3);
}

TEST_F(LuaMidiNoteSpacingTest, EnablingNeedsTheFlushSystem) {
    EXPECT_FALSE(scriptSucceeds("IRAudio.setNoteSpacing({ enabled = true })"));
    EXPECT_FALSE(IRPrefab::MidiNotes::spacing().enabled_);
    IRSystem::createSystem<IRSystem::MIDI_NOTE_SPACING>();
    EXPECT_TRUE(scriptSucceeds("IRAudio.setNoteSpacing({ enabled = true, staggerFrames = 2 })"));
    EXPECT_TRUE(IRPrefab::MidiNotes::spacing().enabled_);
    EXPECT_TRUE(scriptSucceeds("assert(IRAudio.noteSpacing().enabled == true)"));
    EXPECT_TRUE(scriptSucceeds("IRAudio.setNoteSpacing({ enabled = false })"));
    EXPECT_FALSE(IRPrefab::MidiNotes::spacing().enabled_);
}
