/*
 * Project: Irreden Engine
 * File: midi_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/audio/midi_out.hpp>

namespace IRAudio {

    MidiOut::MidiOut()
    :   m_rtMidiOut{},
        m_numberPorts(m_rtMidiOut.getPortCount()),
        m_portNames{},
        m_openPorts{}
    {

        IRE_LOG_INFO("Descovered {} MIDI output sources", m_numberPorts);
        for(int i = 0; i < m_numberPorts; i++) {
            m_portNames.push_back(m_rtMidiOut.getPortName(i));
            IRE_LOG_INFO("MIDI Output source {}: {}", i, m_portNames[i].c_str());
        }
        IRE_LOG_INFO("Created MidiOut");
    }

    MidiOut::~MidiOut() {}

    int MidiOut::openPort(MidiOutInterfaces interface) {
        return openPort(kMidiOutInterfaceNames[interface]);
    }

    int MidiOut::openPort(std::string portNameSubstring) {
        for(int i = 0; i < m_numberPorts; i++) {
            const std::string_view portName{m_portNames[i]};
            if(portName.find(portNameSubstring) != portName.npos) {
                m_rtMidiOut.openPort(i);
                m_openPorts.insert(i);
                IRE_LOG_INFO("Opened MIDI Out port {}: {}", i, portName);
                return i;
            }
        }
        IR_ASSERT(false, "Attempted to open non-existant MIDI Out port by name");
        return -1;
    }

    void MidiOut::sendMessage(const std::vector<unsigned char>& message) {
        m_rtMidiOut.sendMessage(&message);
        IRE_LOG_DEBUG("Sent MIDI message status={}", message.at(0));

    }

} // namespace IRAudio
