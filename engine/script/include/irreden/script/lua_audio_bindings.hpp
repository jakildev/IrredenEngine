#ifndef LUA_AUDIO_BINDINGS_H
#define LUA_AUDIO_BINDINGS_H

#include <irreden/ir_audio.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace IRScript::detail {

// IRAudio file-playback Lua bindings.
//
// Exposes the miniaudio-backed file playback substrate as the `IRAudio` Lua
// table: load + play `.wav`/`.ogg` one-shots and streamed music through
// per-category buses, with master / per-bus volume, looping, and fades. Every
// binding is a thin forward to an `IRAudio::` free function — no playback
// logic lives here. Bound by `bindLuaDrivenEcs()` like the render glue / sim
// surfaces, so every Lua-first creation gets it without per-creation wiring.
//
// `IRAudio.Bus.{CREATURE,ENVIRONMENT,ABILITY,UI,MUSIC}` is the category enum
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

    audio["playSound"] = [](const std::string &path,
                            sol::object busObj,
                            sol::optional<float> volume,
                            sol::optional<bool> loop) -> lua_Integer {
        return static_cast<lua_Integer>(IRAudio::playSound(
            path,
            audioBusFromLua(busObj),
            volume.value_or(1.0f),
            loop.value_or(false)
        ));
    };
    audio["playMusic"] = [](const std::string &path,
                            sol::optional<float> volume,
                            sol::optional<bool> loop) -> lua_Integer {
        return static_cast<lua_Integer>(
            IRAudio::playMusic(path, volume.value_or(1.0f), loop.value_or(true))
        );
    };
    audio["playSoundAt"] = [](const std::string &path,
                              sol::object busObj,
                              sol::object position,
                              sol::optional<float> volume,
                              sol::optional<bool> loop) -> lua_Integer {
        return static_cast<lua_Integer>(IRAudio::playSoundAt(
            path,
            audioBusFromLua(busObj),
            vec3FromLua(position),
            volume.value_or(1.0f),
            loop.value_or(false)
        ));
    };

    audio["stop"] = [](lua_Integer handle) {
        IRAudio::stopSound(static_cast<IRAudio::SoundHandle>(handle));
    };
    audio["setSoundVolume"] = [](lua_Integer handle, float volume) {
        IRAudio::setSoundVolume(static_cast<IRAudio::SoundHandle>(handle), volume);
    };
    audio["fadeIn"] = [](lua_Integer handle, lua_Integer milliseconds) {
        IRAudio::fadeInSound(
            static_cast<IRAudio::SoundHandle>(handle),
            static_cast<unsigned int>(milliseconds)
        );
    };
    audio["fadeOut"] = [](lua_Integer handle, lua_Integer milliseconds) {
        IRAudio::fadeOutSound(
            static_cast<IRAudio::SoundHandle>(handle),
            static_cast<unsigned int>(milliseconds)
        );
    };

    audio["setBusVolume"] = [](sol::object busObj, float volume) {
        IRAudio::setBusVolume(audioBusFromLua(busObj), volume);
    };
    audio["setMasterVolume"] = [](float volume) { IRAudio::setMasterVolume(volume); };
    audio["setListenerPosition"] = [](sol::object position) {
        IRAudio::setListenerPosition(vec3FromLua(position));
    };
    audio["setInputMonitorEnabled"] = [](bool enabled) {
        IRAudio::setInputMonitorEnabled(enabled);
    };
    audio["isInputMonitorEnabled"] = []() { return IRAudio::isInputMonitorEnabled(); };

    // Input level metering (IRAudio::armAudioInput). armAudioInput(deviceName?,
    // sampleRate?) returns `true` or `false, reason`; deviceName defaults to
    // the default input and sampleRate to the recorder's default.
    // audioInputLevels() returns nil while no input is open, else
    // `{ {peakDbfs, rmsDbfs, clippedSamples}, ..., sampleRate, generation }`
    // with one entry per channel in device order.
    audio["armAudioInput"] =
        [](sol::optional<std::string> deviceName,
           sol::optional<int> sampleRate) -> std::tuple<bool, sol::optional<std::string>> {
        IRAudio::AudioInputArmResult result = IRAudio::armAudioInput(
            deviceName.value_or(""),
            sampleRate.value_or(IRAudio::AudioCaptureConfig{}.sample_rate_)
        );
        if (result.armed_) {
            return {true, sol::nullopt};
        }
        return {false, std::move(result.reason_)};
    };
    audio["disarmAudioInput"] = []() { IRAudio::disarmAudioInput(); };
    audio["audioInputLevels"] = [](sol::this_state state) -> sol::object {
        sol::state_view lua(state);
        const std::optional<IRAudio::AudioInputLevels> levels = IRAudio::audioInputLevels();
        if (!levels) {
            return sol::make_object(lua, sol::lua_nil);
        }
        sol::table out = lua.create_table(static_cast<int>(levels->channelCount_), 2);
        for (unsigned int channel = 0; channel < levels->channelCount_; ++channel) {
            const IRAudio::AudioChannelLevel &level = levels->channels_[channel];
            out[channel + 1] = lua.create_table_with(
                "peakDbfs",
                level.peakDbfs_,
                "rmsDbfs",
                level.rmsDbfs_,
                "clippedSamples",
                static_cast<lua_Integer>(level.clippedSamples_)
            );
        }
        out["sampleRate"] = static_cast<lua_Integer>(levels->sampleRate_);
        out["generation"] = static_cast<lua_Integer>(levels->generation_);
        return out;
    };

    // Outbound-MIDI observer. A Lua monitor sees EVERY outbound
    // message, including ones the C++ ECS audio path emits — not just what Lua
    // sent. `IRAudio.MidiStatus.*` is the message-type byte as an integer table
    // (the cpp-lua-enums.md convention) so a handler compares the status arg
    // against named values, not magic numbers.
    sol::table midiStatus = lua.create_table();
#define IR_BIND_MIDI_STATUS(name)                                                                  \
    midiStatus[#name] = static_cast<lua_Integer>(IRAudio::kMidiStatus_##name)
    IR_BIND_MIDI_STATUS(NOTE_OFF);
    IR_BIND_MIDI_STATUS(NOTE_ON);
    IR_BIND_MIDI_STATUS(POLYPHONIC_KEY_PRESSURE);
    IR_BIND_MIDI_STATUS(CONTROL_CHANGE);
    IR_BIND_MIDI_STATUS(PROGRAM_CHANGE);
    IR_BIND_MIDI_STATUS(CHANNEL_PRESSURE);
    IR_BIND_MIDI_STATUS(PITCH_BEND);
    IR_BIND_MIDI_STATUS(TIMING_CLOCK);
    IR_BIND_MIDI_STATUS(START);
    IR_BIND_MIDI_STATUS(CONTINUE);
    IR_BIND_MIDI_STATUS(STOP);
#undef IR_BIND_MIDI_STATUS
    audio["MidiStatus"] = midiStatus;

    // IRAudio.onMidiSent(fn): fn(status, channel, data1, data2, portIndex) — all
    // integers. `status` is the message-type byte (matches IRAudio.MidiStatus.*);
    // `channel` is 0-15; `portIndex` is -1 for the default-port send. A system
    // message (START, STOP, ...) carries no channel: `status` is its whole byte
    // and `channel` is -1. Last registration wins (single observer, like the
    // inbound AudioInputCallback); multiplex in Lua if you need fan-out. A
    // handler error is logged and swallowed so one bad Lua callback can't
    // abort the C++ send loop.
    audio["onMidiSent"] = [](sol::protected_function fn) {
        IRAudio::setOutboundMidiObserver(
            [fn = std::move(fn)](const IRComponents::C_MidiMessage &message, int portIndex) {
                const bool system = IRAudio::isMidiSystemStatus(message.status_);
                sol::protected_function_result result =
                    fn(static_cast<lua_Integer>(system ? message.status_ : message.getStatusBits()),
                       system ? static_cast<lua_Integer>(-1)
                              : static_cast<lua_Integer>(message.getChannelBits()),
                       static_cast<lua_Integer>(message.data1_),
                       static_cast<lua_Integer>(message.data2_),
                       static_cast<lua_Integer>(portIndex));
                if (!result.valid()) {
                    sol::error err = result;
                    IRE_LOG_ERROR("IRAudio.onMidiSent handler error: {}", err.what());
                }
            }
        );
    };
    audio["clearMidiObserver"] = []() { IRAudio::clearOutboundMidiObserver(); };

    // MIDI beat clock on one open output port (IRAudio::startMidiClock):
    // START, pulses from an engine thread, STOP.
    audio["startMidiClock"] = [](lua_Integer portIndex, double bpm) {
        return IRAudio::startMidiClock(static_cast<int>(portIndex), bpm);
    };
    audio["setMidiClockBpm"] = [](double bpm) { return IRAudio::setMidiClockBpm(bpm); };
    audio["stopMidiClock"] = []() { IRAudio::stopMidiClock(); };
    audio["isMidiClockRunning"] = []() { return IRAudio::isMidiClockRunning(); };
    audio["midiClockPulses"] = []() {
        return static_cast<lua_Integer>(IRAudio::midiClockPulses());
    };
}

} // namespace IRScript::detail

#endif /* LUA_AUDIO_BINDINGS_H */
