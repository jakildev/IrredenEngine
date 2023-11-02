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

    IRMidiOut::IRMidiOut()
    :   m_rtMidiOut{},
        m_numberPorts(m_rtMidiOut.getPortCount()),
        m_portNames{},
        m_openPorts{}
    {

        IRProfile::engLogInfo("Descovered {} MIDI output sources", m_numberPorts);
        for(int i = 0; i < m_numberPorts; i++) {
            m_portNames.push_back(m_rtMidiOut.getPortName(i));
            IRProfile::engLogInfo("MIDI Output source {}: {}", i, m_portNames[i].c_str());
        }
        IRProfile::engLogInfo("Created IRMidiOut");
    }

    IRMidiOut::~IRMidiOut() {}

    void IRMidiOut::openPort(unsigned int portNumber) {
        m_rtMidiOut.openPort(portNumber);
        m_openPorts.insert(portNumber);
        IRProfile::engLogInfo("Opened MIDI Out port {}", portNumber);
    }

    void IRMidiOut::openPort(MidiOutInterface interface) {
        openPort(kMidiOutInterfaceNames[interface]);
    }

    void IRMidiOut::openPort(std::string portNameSubstring) {
        for(int i = 0; i < m_numberPorts; i++) {
            const std::string_view portName{m_portNames[i]};
            if(portName.find(portNameSubstring) != portName.npos) {
                m_rtMidiOut.openPort(i);
                m_openPorts.insert(i);
                IRProfile::engLogInfo("Opened MIDI Out port {}: {}", i, portName);
                return;
            }
        }
        IR_ASSERT(false, "Attempted to open non-existant MIDI Out port by name");
    }

    void IRMidiOut::sendMessage(const std::vector<unsigned char>& message) {
        m_rtMidiOut.sendMessage(&message);
        IRProfile::engLogDebug("Sent MIDI message status={}", message.at(0));

    }

} // namespace IRAudio
