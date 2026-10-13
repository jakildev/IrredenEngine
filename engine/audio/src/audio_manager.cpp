#include <irreden/ir_audio.hpp>

#include <irreden/audio/audio_manager.hpp>

namespace IRAudio {

namespace {

// The clock's writer: every status straight to the port; START and STOP
// (sent on the caller's thread) also to the outbound observer, the pulses
// (sent on the clock thread, where Lua must not run) never.
MidiClock::RealtimeSend midiClockSend(AudioManager &manager) {
    return [&manager](int portIndex, unsigned char status) {
        manager.getMidiOut().sendMessage(portIndex, std::vector<unsigned char>{status});
        if (status != kMidiStatus_TIMING_CLOCK) {
            manager.fireOutboundMidiObserver(IRComponents::C_MidiMessage{status, 0, 0}, portIndex);
        }
    };
}

} // namespace

AudioManager::AudioManager()
    : m_audio{}
    , m_midiIn{}
    , m_midiOut{}
    , m_midiClock{midiClockSend(*this)}
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
    , m_midiClock{midiClockSend(*this)}
    , m_audioPlayback{} {
    g_audioManager = this;
    IRE_LOG_INFO("Created AudioManager");
}

AudioManager::~AudioManager() {
    m_midiClock.stop();
    m_midiOut.sendAllNotesOff();
    if (g_audioManager == this) {
        g_audioManager = nullptr;
    }
    IRE_LOG_DEBUG("Destroyed AudioManager");
}

} // namespace IRAudio
