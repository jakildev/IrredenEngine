/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\audio\midi_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#include <RtMidi/RtMidi.h>
#include "ir_audio.hpp"
#include "midi_messages.hpp"

namespace IRAudio {

    class IRMidiOut {
        public:
            IRMidiOut()
            :   m_rtMidiOut{},
                m_numberPorts(m_rtMidiOut.getPortCount()),
                m_portNames{},
                m_openPorts{}
            {
                // MidiMessage<kMidiStatus_CONTROL_CHANGE> ccChange{};
                MidiMessage<kMidiStatus_NOTE_OFF> noteOffChange{
                    0,
                    34,
                    2
                };


                ENG_LOG_INFO("Descovered {} MIDI output sources", m_numberPorts);
                for(int i = 0; i < m_numberPorts; i++) {
                    m_portNames.push_back(m_rtMidiOut.getPortName(i));
                    ENG_LOG_INFO("MIDI Output source {}: {}", i, m_portNames[i].c_str());
                }
            }

            ~IRMidiOut() {}

            void openPort(unsigned int portNumber) {
                m_rtMidiOut.openPort(portNumber);
                m_openPorts.insert(portNumber);
                ENG_LOG_INFO("Opened MIDI Out port {}", portNumber);
            }

            // Opens the first device that matches substring name
            void openPort(std::string portNameSubstring) {
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

            void sendMessage(const std::vector<unsigned char>& message) {
                m_rtMidiOut.sendMessage(&message);
                ENG_LOG_DEBUG("Sent MIDI message status={}", message.at(0));

            }

        private:
            RtMidiOut m_rtMidiOut;
            unsigned int m_numberPorts;
            std::vector<std::string> m_portNames;
            std::set<unsigned int> m_openPorts;
            std::string m_portName;
    };

} // namespace IRAudio

#endif /* MIDI_OUT_H */
