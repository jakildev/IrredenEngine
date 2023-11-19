/*
 * Project: Irreden Engine
 * File: audio_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_audio.hpp>

#include <irreden/audio/audio_manager.hpp>

namespace IRAudio {
    AudioManager::AudioManager()
    :   m_audio{},
        m_midiIn{},
        m_midiOut{}
    {
        // for(auto& midiInInterface : midiInInterfaces) {
        //     m_midiIn.openPort(midiInInterface);
        // }

        // for(auto& midiOutInterface : midiOutInterfaces) {
        //     m_midiOut.openPort(kMidiOutInterfaceNames[midiOutInterface]);
        // }
        g_audioManager = this;
        IRProfile::engLogInfo("Created AudioManager");
    }

    AudioManager::~AudioManager() {
        IRProfile::engLogDebug("Destroyed AudioManager");
    }

} // namespace IRAudio