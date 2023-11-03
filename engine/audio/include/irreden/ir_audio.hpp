/*
 * Project: Irreden Engine
 * File: ir_audio.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_AUDIO_H
#define IR_AUDIO_H

#include <irreden/audio/ir_audio_types.hpp>

#include <vector>

namespace IRAudio {

    class AudioManager;
    extern AudioManager* g_audioManager;
    AudioManager& getAudioManager();

    void openPortMidiIn(MidiInInterfaces midiInInterface);
    void openPortMidiOut(MidiOutInterfaces midiOutInterface);
    void sendMidiMessage(const std::vector<unsigned char>& message);

} // namespace IRAudio

#endif /* IR_AUDIO_H */
