/*
 * Project: Irreden Engine
 * File: audio_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <irreden/ir_audio.hpp>
#include <irreden/audio/audio.hpp>
#include <irreden/audio/midi_in.hpp>
#include <irreden/audio/midi_out.hpp>

namespace IRAudio {

    class AudioManager {
    public:
        AudioManager();
        ~AudioManager();

        inline IRMidiIn& getMidiIn() { return m_midiIn; }
        inline IRMidiOut& getMidiOut() { return m_midiOut; }

        void processMidiMessageQueue();

    private:
        Audio m_audio;
        IRMidiIn m_midiIn;
        IRMidiOut m_midiOut;

    };

} // namespace IRAudio

#endif /* AUDIO_MANAGER_H */
