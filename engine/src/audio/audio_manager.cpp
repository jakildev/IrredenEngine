/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\audio\audio_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */


#include "audio_manager.hpp"
namespace IRAudio {

    AudioManager::AudioManager(
        std::vector<MidiInInterface> midiInInterfaces,
        std::vector<MidiOutInterfaces> midiOutInterfaces
    )
    :   m_audio{},
        m_midiIn{},
        m_midiOut{}
    {
        for(auto& midiInInterface : midiInInterfaces) {
            m_midiIn.openPort(midiInInterface);
        }

        for(auto& midiOutInterface : midiOutInterfaces) {
            m_midiOut.openPort(kMidiOutInterfaceNames[midiOutInterface]);
        }

        ENG_LOG_INFO("Created AudioManager");

    }

    AudioManager::~AudioManager() {
        ENG_LOG_DEBUG("Destroyed AudioManager");
    }

    void AudioManager::processMidiMessageQueue() {
        m_midiIn.processMidiMessageQueue();
    }

} // namespace IRAudio
