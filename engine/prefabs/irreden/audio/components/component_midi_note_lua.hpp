#ifndef COMPONENT_MIDI_NOTE_LUA_H
#define COMPONENT_MIDI_NOTE_LUA_H

#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_MidiNote> = true;

template <> inline void bindLuaType<IRComponents::C_MidiNote>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_MidiNote,
        IRComponents::C_MidiNote(unsigned char, unsigned char, unsigned char, float),
        IRComponents::C_MidiNote(unsigned char, unsigned char)>(
        "C_MidiNote",
        "note",
        &IRComponents::C_MidiNote::note_,
        "velocity",
        &IRComponents::C_MidiNote::velocity_,
        "channel",
        &IRComponents::C_MidiNote::channel_,
        "holdSeconds",
        &IRComponents::C_MidiNote::holdSeconds_
    );
}
} // namespace IRScript

#endif /* COMPONENT_MIDI_NOTE_LUA_H */
