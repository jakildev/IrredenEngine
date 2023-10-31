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

namespace IRAudio {

    AudioManager* g_audioManager = nullptr;
    AudioManager& getAudioManager() {
        IR_ENG_ASSERT(
            g_audioManager != nullptr,
            "AudioManager not initialized"
        );
        return *g_audioManager;
    }

    void openPortMidiIn(MidiInInterface port) {
        getAudioManager().getMidiIn().openPort(port);
    }

    void openPortMidiOut(MidiOutInterface port) {
        getAudioManager().getMidiOut().openPort(port);
    }

} // namespace IRAudio