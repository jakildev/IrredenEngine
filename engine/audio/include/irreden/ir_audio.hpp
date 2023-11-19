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

#include <irreden/audio/components/component_midi_message.hpp>

#include <vector>

namespace IRAudio {

    class AudioManager;
    extern AudioManager* g_audioManager;
    AudioManager& getAudioManager();

    void openPortMidiIn(MidiInInterfaces midiInInterface);
    void openPortMidiOut(MidiOutInterfaces midiOutInterface);
    void sendMidiMessage(const std::vector<unsigned char>& message);

    CCData checkCCMessage(int device, CCMessage ccMessage);
    const std::vector<IRComponents::C_MidiMessage>& getMidiNotesOnThisFrame(
        int device
    );
    const std::vector<IRComponents::C_MidiMessage>& getMidiNotesOffThisFrame(
        int device
    );

    void insertNoteOffMessage(
        MidiChannel channel,
        const IRComponents::C_MidiMessage& message
    );
    void insertNoteOnMessage(
        MidiChannel channel,
        const IRComponents::C_MidiMessage& message
    );
    void insertCCMessage(
        MidiChannel channel,
        const IRComponents::C_MidiMessage& message
    );

} // namespace IRAudio

#endif /* IR_AUDIO_H */
