#include <irreden/ir_audio.hpp>

#include <irreden/audio/audio_manager.hpp>

namespace IRAudio {

namespace {

// The clock's port writer. The pulses reach it on the clock thread, so it
// touches the port and nothing else.
MidiClock::RealtimeSend midiClockWrite(AudioManager &manager) {
    return [&manager](int portIndex, unsigned char status) {
        manager.getMidiOut().sendMessage(portIndex, std::vector<unsigned char>{status});
    };
}

// START and STOP reach the outbound observer ahead of the port, the order
// IRAudio::sendMidiMessage gives every other outbound message.
MidiClock::RealtimeSend midiClockObserveTransport(AudioManager &manager) {
    return [&manager](int portIndex, unsigned char status) {
        manager.fireOutboundMidiObserver(IRComponents::C_MidiMessage{status, 0, 0}, portIndex);
    };
}

} // namespace

AudioManager::AudioManager()
    : m_audio{}
    , m_midiIn{}
    , m_midiOut{}
    , m_midiClock{midiClockWrite(*this), midiClockObserveTransport(*this)}
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
    , m_midiClock{midiClockWrite(*this), midiClockObserveTransport(*this)}
    , m_audioPlayback{} {
    g_audioManager = this;
    IRE_LOG_INFO("Created AudioManager");
}

AudioManager::AudioManager(
    detail::MidiInBackendFactory midiInBackendFactory,
    detail::MidiOutBackendFactory midiOutBackendFactory
)
    : m_audio{}
    , m_midiIn{std::move(midiInBackendFactory)}
    , m_midiOut{std::move(midiOutBackendFactory)}
    , m_midiClock{midiClockWrite(*this), midiClockObserveTransport(*this)}
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

void AudioManager::releaseMidiPorts() {
    m_midiClock.stop();
    m_midiOut.sendAllNotesOff();
    m_midiIn.releasePorts();
    m_midiOut.releasePorts();
}

std::vector<std::string> AudioManager::restoreMidiPorts() {
    std::vector<std::string> lost = m_midiIn.restorePorts();
    std::vector<std::string> outputLost = m_midiOut.restorePorts();
    lost.insert(lost.end(), outputLost.begin(), outputLost.end());
    return lost;
}

} // namespace IRAudio
