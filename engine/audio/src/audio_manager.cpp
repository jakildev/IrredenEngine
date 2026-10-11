#include <irreden/ir_audio.hpp>

#include <irreden/audio/audio_manager.hpp>

namespace IRAudio {
AudioManager::AudioManager()
    : m_audio{}
    , m_midiIn{}
    , m_midiOut{}
    , m_audioPlayback{} {
    // for(auto& midiInInterface : midiInInterfaces) {
    //     m_midiIn.openPort(midiInInterface);
    // }

    // for(auto& midiOutInterface : midiOutInterfaces) {
    //     m_midiOut.openPort(kMidiOutInterfaceNames[midiOutInterface]);
    // }
    g_audioManager = this;
    IRE_LOG_INFO("Created AudioManager");
}

AudioManager::AudioManager(
    std::unique_ptr<detail::IAudioInputBackend> inputBackend,
    std::unique_ptr<detail::IAudioInputBackend> outputBackend,
    detail::AudioInputDeadlines deadlines
)
    : m_audio{std::move(inputBackend), std::move(outputBackend), deadlines}
    , m_midiIn{}
    , m_midiOut{}
    , m_audioPlayback{} {
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
