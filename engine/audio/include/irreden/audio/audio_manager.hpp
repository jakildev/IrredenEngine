#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/audio.hpp>
#include <irreden/audio/midi_in.hpp>
#include <irreden/audio/midi_out.hpp>

namespace IRAudio {

class AudioManager {
  public:
    AudioManager();
    ~AudioManager();

    inline MidiIn &getMidiIn() {
        return m_midiIn;
    }
    inline MidiOut &getMidiOut() {
        return m_midiOut;
    }

  private:
    Audio m_audio;
    MidiIn m_midiIn;
    MidiOut m_midiOut;
};

} // namespace IRAudio

#endif /* AUDIO_MANAGER_H */
