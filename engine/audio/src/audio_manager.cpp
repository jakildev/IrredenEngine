#include <irreden/ir_audio.hpp>

#include <irreden/audio/audio_manager.hpp>

namespace IRAudio {
AudioManager::AudioManager()
    : m_audio{}
    , m_midiIn{}
    , m_midiOut{} {
    // for(auto& midiInInterface : midiInInterfaces) {
    //     m_midiIn.openPort(midiInInterface);
    // }

    // for(auto& midiOutInterface : midiOutInterfaces) {
    //     m_midiOut.openPort(kMidiOutInterfaceNames[midiOutInterface]);
    // }
    g_audioManager = this;
    IRE_LOG_INFO("Created AudioManager");
}

AudioManager::~AudioManager() {
    m_midiOut.sendAllNotesOff();
    if (g_audioManager == this) {
        g_audioManager = nullptr;
    }
    IRE_LOG_DEBUG("Destroyed AudioManager");
}

} // namespace IRAudio