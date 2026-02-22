#ifndef COMPONENT_MIDI_SEQUENCE_LUA_H
#define COMPONENT_MIDI_SEQUENCE_LUA_H

#include <irreden/audio/components/component_midi_sequence.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_MidiSequence> = true;

template <> inline void bindLuaType<IRComponents::C_MidiSequence>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_MidiSequence,
                           IRComponents::C_MidiSequence(float, int, int, int, bool),
                           IRComponents::C_MidiSequence(float, int, int, int)>(
        "C_MidiSequence", "bpm", &IRComponents::C_MidiSequence::bpm_, "looping",
        &IRComponents::C_MidiSequence::looping_, "insertNote",
        &IRComponents::C_MidiSequence::insertNote, "getMeasureLengthSeconds",
        &IRComponents::C_MidiSequence::getMeasureLengthSeconds, "getSequenceLengthSeconds",
        &IRComponents::C_MidiSequence::getSequenceLengthSeconds);
}
} // namespace IRScript

#endif /* COMPONENT_MIDI_SEQUENCE_LUA_H */
