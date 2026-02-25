#include <irreden/audio/midi_out.hpp>

namespace IRAudio {

MidiOut::MidiOut()
    : m_rtMidiOut{}
    , m_numberPorts(m_rtMidiOut.getPortCount())
    , m_portNames{}
    , m_openPorts{} {

    IRE_LOG_INFO("Descovered {} MIDI output sources", m_numberPorts);
    for (int i = 0; i < m_numberPorts; i++) {
        m_portNames.push_back(m_rtMidiOut.getPortName(i));
        IRE_LOG_INFO("MIDI Output source {}: {}", i, m_portNames[i].c_str());
    }
    IRE_LOG_INFO("Created MidiOut");
}

MidiOut::~MidiOut() {
    sendAllNotesOff();
}

void MidiOut::sendAllNotesOff() {
    if (m_openPorts.empty()) {
        return;
    }
    for (unsigned char ch = 0; ch < kNumMidiChannels; ++ch) {
        std::vector<unsigned char> msg = {
            buildMidiStatus(kMidiStatus_CONTROL_CHANGE, ch),
            kMidiCC_ALL_NOTES_OFF,
            0
        };
        m_rtMidiOut.sendMessage(&msg);
    }
    IRE_LOG_INFO("Sent All Notes Off on all channels");
}

int MidiOut::openPort(MidiOutInterfaces interface) {
    return openPort(kMidiOutInterfaceNames[interface]);
}

int MidiOut::openPort(std::string portNameSubstring) {
    for (int i = 0; i < m_numberPorts; i++) {
        const std::string_view portName{m_portNames[i]};
        if (portName.find(portNameSubstring) != portName.npos) {
            m_rtMidiOut.openPort(i);
            m_openPorts.insert(i);
            IRE_LOG_INFO("Opened MIDI Out port {}: {}", i, portName);
            return i;
        }
    }
    IR_ASSERT(false, "Attempted to open non-existant MIDI Out port by name");
    return -1;
}

void MidiOut::sendMessage(const std::vector<unsigned char> &message) {
    m_rtMidiOut.sendMessage(&message);
    IRE_LOG_DEBUG("Sent MIDI message status={}", message.at(0));
}

} // namespace IRAudio
