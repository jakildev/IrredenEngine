/*
 * Project: Irreden Engine
 * File: ir_audio.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_audio.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/audio/audio_manager.hpp>
#include <irreden/audio/midi_out.hpp>

#include <irreden/audio/systems/system_audio_midi_message_in.hpp>

namespace IRAudio {

    AudioManager* g_audioManager = nullptr;
    AudioManager& getAudioManager() {
        IR_ASSERT(
            g_audioManager != nullptr,
            "AudioManager not initialized"
        );
        return *g_audioManager;
    }

    void openPortMidiIn(MidiInInterfaces port) {
        getAudioManager().getMidiIn().openPort(port);
    }

    void openPortMidiOut(MidiOutInterfaces port) {
        getAudioManager().getMidiOut().openPort(port);
    }

    void sendMidiMessage(const std::vector<unsigned char>& message) {
        getAudioManager().getMidiOut().sendMessage(message);
    }

    CCData checkCCMessage(int device, CCMessage ccMessage) {
        return IRECS::getEngineSystem<IRECS::INPUT_MIDI_MESSAGE_IN>().
            checkCCMessageReceived(device, ccMessage);
    }

    const std::vector<IRComponents::C_MidiMessage>& getMidiNotesOnThisFrame(
        int device
    )
    {
        return IRECS::getEngineSystem<IRECS::INPUT_MIDI_MESSAGE_IN>().
            getMidiNotesOnThisFrame(device);
    }
    const std::vector<IRComponents::C_MidiMessage>& getMidiNotesOffThisFrame(
        int device
    )
    {
        return IRECS::getEngineSystem<IRECS::INPUT_MIDI_MESSAGE_IN>().
            getMidiNotesOffThisFrame(device);
    }



} // namespace IRAudio