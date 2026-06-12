#include <irreden/audio/midi_out.hpp>

namespace IRAudio {

MidiOut::MidiOut()
    : m_rtMidiOut{}
    , m_numberPorts(m_rtMidiOut.getPortCount())
    , m_portNames{}
    , m_ports{} {

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
    if (m_ports.empty()) {
        return;
    }
    for (auto &port : m_ports) {
        for (unsigned char ch = 0; ch < kNumMidiChannels; ++ch) {
            std::vector<unsigned char> msg =
                {buildMidiStatus(kMidiStatus_CONTROL_CHANGE, ch), kMidiCC_ALL_NOTES_OFF, 0};
            port->rtMidiOut_->sendMessage(&msg);
        }
    }
    IRE_LOG_INFO("Sent All Notes Off on all channels of all open output ports");
}

int MidiOut::openPort(MidiOutInterfaces interface) {
    return openPort(kMidiOutInterfaceNames[midiOutInterfaceIndex(interface)]);
}

int MidiOut::openPort(std::string portNameSubstring) {
    for (int i = 0; i < m_numberPorts; i++) {
        const std::string_view portName{m_portNames[i]};
        if (portName.find(portNameSubstring) == portName.npos) {
            continue;
        }
        for (const auto &port : m_ports) {
            if (port->portIndex_ == i) {
                IRE_LOG_INFO("MIDI Out port {} ({}) already open", i, portName);
                return i;
            }
        }
        auto port = std::make_unique<MidiOutPort>();
        port->portIndex_ = i;
        port->name_ = m_portNames[i];
        port->rtMidiOut_ = std::make_unique<RtMidiOut>();
        port->rtMidiOut_->openPort(i);
        m_ports.push_back(std::move(port));
        IRE_LOG_INFO("Opened MIDI Out port {}: {}", i, portName);
        return i;
    }
    IRE_LOG_WARN(
        "No MIDI output port matching '{}' — {} port(s) available",
        portNameSubstring,
        m_numberPorts
    );
    return -1;
}

const std::vector<std::string> &MidiOut::getPortNames() const {
    return m_portNames;
}

std::vector<int> MidiOut::getOpenPortIndices() const {
    std::vector<int> indices;
    indices.reserve(m_ports.size());
    for (const auto &port : m_ports) {
        indices.push_back(port->portIndex_);
    }
    return indices;
}

void MidiOut::sendMessage(const std::vector<unsigned char> &message) {
    if (m_ports.empty()) {
        IRE_LOG_WARN("sendMessage called with no MIDI output port open");
        return;
    }
    m_ports.front()->rtMidiOut_->sendMessage(&message);
    IRE_LOG_DEBUG("Sent MIDI message status={}", message.at(0));
}

void MidiOut::sendMessage(int portIndex, const std::vector<unsigned char> &message) {
    for (auto &port : m_ports) {
        if (port->portIndex_ == portIndex) {
            port->rtMidiOut_->sendMessage(&message);
            IRE_LOG_DEBUG("Sent MIDI message status={} to port {}", message.at(0), portIndex);
            return;
        }
    }
    IRE_LOG_WARN("sendMessage targeted unopened MIDI output port {}", portIndex);
}

} // namespace IRAudio
