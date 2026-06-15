#ifndef LUA_AUDIO_BINDINGS_H
#define LUA_AUDIO_BINDINGS_H

#include <irreden/ir_audio.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <stdexcept>
#include <string>

namespace IRScript::detail {

// IRAudio file-playback Lua bindings (engine #1813).
//
// Exposes the miniaudio-backed file playback substrate as the `IRAudio` Lua
// table: load + play `.wav`/`.ogg` one-shots and streamed music through
// per-category buses, with master / per-bus volume, looping, and fades. Every
// binding is a thin forward to an `IRAudio::` free function — no playback
// logic lives here. Bound by `bindLuaDrivenEcs()` like the render glue / sim
// surfaces, so every Lua-first creation gets it without per-creation wiring.
//
// `IRAudio.Bus.{Creature,Environment,Ability,UI,Music}` is the category enum
// as an integer table (the `cpp-lua-enums.md` convention) — pass the value,
// never a bus-name string.

// `sol::object` bus arg → `AudioBus`, validated. Rejects strings explicitly so
// `bus = "UI"` gets a message pointing at `IRAudio.Bus.UI` rather than a
// silent miscast (the cpp-lua-enums contract).
inline IRAudio::AudioBus audioBusFromLua(sol::object busObj) {
    if (busObj.get_type() == sol::type::string) {
        throw std::runtime_error("IRAudio bus must be an IRAudio.Bus.* value, not a string");
    }
    if (!busObj.is<lua_Integer>()) {
        throw std::runtime_error("IRAudio bus must be an IRAudio.Bus.* integer");
    }
    const lua_Integer raw = busObj.as<lua_Integer>();
    if (raw < 0 || raw >= static_cast<lua_Integer>(IRAudio::kNumAudioBuses)) {
        throw std::runtime_error("IRAudio bus out of range — use IRAudio.Bus.*");
    }
    return static_cast<IRAudio::AudioBus>(raw);
}

inline void bindAudioApi(LuaScript &script) {
    sol::state &lua = script.lua();

    // Extend (never replace) so a creation that adds its own IRAudio entries
    // keeps them.
    if (!lua["IRAudio"].valid()) {
        lua["IRAudio"] = lua.create_table();
    }
    sol::table audio = lua["IRAudio"];

    // Bus category enum as an integer table.
    sol::table bus = lua.create_table();
#define IR_BIND_AUDIO_BUS(name) bus[#name] = static_cast<lua_Integer>(IRAudio::AudioBus::name)
    IR_BIND_AUDIO_BUS(CREATURE);
    IR_BIND_AUDIO_BUS(ENVIRONMENT);
    IR_BIND_AUDIO_BUS(ABILITY);
    IR_BIND_AUDIO_BUS(UI);
    IR_BIND_AUDIO_BUS(MUSIC);
#undef IR_BIND_AUDIO_BUS
    audio["Bus"] = bus;

    audio["playSound"] =
        [](const std::string &path, sol::object busObj, sol::optional<float> volume, sol::optional<bool> loop)
        -> lua_Integer {
        return static_cast<lua_Integer>(
            IRAudio::playSound(path, audioBusFromLua(busObj), volume.value_or(1.0f), loop.value_or(false))
        );
    };
    audio["playMusic"] =
        [](const std::string &path, sol::optional<float> volume, sol::optional<bool> loop) -> lua_Integer {
        return static_cast<lua_Integer>(IRAudio::playMusic(path, volume.value_or(1.0f), loop.value_or(true)));
    };
    audio["playSoundAt"] =
        [](const std::string &path, sol::object busObj, sol::object position, sol::optional<float> volume,
           sol::optional<bool> loop) -> lua_Integer {
        return static_cast<lua_Integer>(IRAudio::playSoundAt(
            path, audioBusFromLua(busObj), vec3FromLua(position), volume.value_or(1.0f), loop.value_or(false)
        ));
    };

    audio["stop"] = [](lua_Integer handle) {
        IRAudio::stopSound(static_cast<IRAudio::SoundHandle>(handle));
    };
    audio["setSoundVolume"] = [](lua_Integer handle, float volume) {
        IRAudio::setSoundVolume(static_cast<IRAudio::SoundHandle>(handle), volume);
    };
    audio["fadeIn"] = [](lua_Integer handle, lua_Integer milliseconds) {
        IRAudio::fadeInSound(static_cast<IRAudio::SoundHandle>(handle), static_cast<unsigned int>(milliseconds));
    };
    audio["fadeOut"] = [](lua_Integer handle, lua_Integer milliseconds) {
        IRAudio::fadeOutSound(static_cast<IRAudio::SoundHandle>(handle), static_cast<unsigned int>(milliseconds));
    };

    audio["setBusVolume"] = [](sol::object busObj, float volume) {
        IRAudio::setBusVolume(audioBusFromLua(busObj), volume);
    };
    audio["setMasterVolume"] = [](float volume) {
        IRAudio::setMasterVolume(volume);
    };
    audio["setListenerPosition"] = [](sol::object position) {
        IRAudio::setListenerPosition(vec3FromLua(position));
    };
}

} // namespace IRScript::detail

#endif /* LUA_AUDIO_BINDINGS_H */
