#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/audio.hpp>
#include <irreden/audio/audio_playback.hpp>
#include <irreden/audio/midi_in.hpp>
#include <irreden/audio/midi_out.hpp>

#include <irreden/audio/components/component_midi_message.hpp>

#include <functional>
#include <utility>

namespace IRAudio {

class AudioManager {
  public:
    // Storage type for the outbound-MIDI observer; mirrors the namespace-scoped
    // IRAudio::OutboundMidiCallback alias in ir_audio.hpp (same underlying
    // std::function), the same split the inbound Audio::AudioInputCallback uses.
    using OutboundMidiObserver = std::function<void(const IRComponents::C_MidiMessage &, int)>;

    AudioManager();
    ~AudioManager();

    inline MidiIn &getMidiIn() {
        return m_midiIn;
    }
    inline MidiOut &getMidiOut() {
        return m_midiOut;
    }
    inline Audio &getAudio() {
        return m_audio;
    }
    inline AudioPlayback &getAudioPlayback() {
        return m_audioPlayback;
    }

    inline void setOutboundMidiObserver(OutboundMidiObserver observer) {
        m_outboundMidiObserver = std::move(observer);
    }
    inline void clearOutboundMidiObserver() {
        m_outboundMidiObserver = nullptr;
    }
    inline const OutboundMidiObserver &getOutboundMidiObserver() const {
        return m_outboundMidiObserver;
    }

  private:
    Audio m_audio;
    MidiIn m_midiIn;
    MidiOut m_midiOut;
    AudioPlayback m_audioPlayback;
    // Single observer fired on every outbound sendMidiMessage. Lives for this
    // AudioManager's lifetime; a captured sol::function inside it must outlive
    // any send, so the owning World declares m_audioManager AFTER m_lua (it
    // then destructs first, dropping the function while the sol::state is still
    // alive). See ir_audio.cpp / engine/audio/CLAUDE.md.
    OutboundMidiObserver m_outboundMidiObserver;
};

} // namespace IRAudio

#endif /* AUDIO_MANAGER_H */
