/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\audio\midi_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "midi_out.hpp"

namespace IRAudio {

    IRMidiOut::IRMidiOut()
    :   m_rtMidiOut{},
        m_numberPorts(m_rtMidiOut.getPortCount()),
        m_portNames{},
        m_openPorts{}
    {

        ENG_LOG_INFO("Descovered {} MIDI output sources", m_numberPorts);
        for(int i = 0; i < m_numberPorts; i++) {
            m_portNames.push_back(m_rtMidiOut.getPortName(i));
            ENG_LOG_INFO("MIDI Output source {}: {}", i, m_portNames[i].c_str());
        }
        ENG_LOG_INFO("Created IRMidiOut");
    }

    IRMidiOut::~IRMidiOut() {}

    void IRMidiOut::openPort(unsigned int portNumber) {
        m_rtMidiOut.openPort(portNumber);
        m_openPorts.insert(portNumber);
        ENG_LOG_INFO("Opened MIDI Out port {}", portNumber);
    }

    void IRMidiOut::openPort(std::string portNameSubstring) {
        for(int i = 0; i < m_numberPorts; i++) {
            const std::string_view portName{m_portNames[i]};
            if(portName.find(portNameSubstring) != portName.npos) {
                m_rtMidiOut.openPort(i);
                m_openPorts.insert(i);
                ENG_LOG_INFO("Opened MIDI Out port {}: {}", i, portName);
                return;
            }
        }
        ENG_ASSERT(false, "Attempted to open non-existant MIDI Out port by name");
    }

    void IRMidiOut::sendMessage(const std::vector<unsigned char>& message) {
        m_rtMidiOut.sendMessage(&message);
        ENG_LOG_DEBUG("Sent MIDI message status={}", message.at(0));

    }

} // namespace IRAudio
