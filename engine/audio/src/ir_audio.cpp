/*
 * Project: Irreden Engine
 * File: ir_audio.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_audio.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/audio/midi_out.hpp>

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

} // namespace IRAudio