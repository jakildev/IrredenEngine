/*
 * Project: Irreden Engine
 * File: midi_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/midi_messages.hpp>

#include <RtMidi.h>

#include <set>

namespace IRAudio {

    class MidiOut {
        public:
            MidiOut();
            ~MidiOut();

            void openPort(unsigned int portNumber);
            // Opens the first device that matches substring name
            int openPort(MidiOutInterfaces midiInInterface);

            int openPort(std::string portNameSubstring);
            void sendMessage(const std::vector<unsigned char>& message);
        private:
            RtMidiOut m_rtMidiOut;
            unsigned int m_numberPorts;
            std::vector<std::string> m_portNames;
            std::set<unsigned int> m_openPorts;
            std::string m_portName;
    };

} // namespace IRAudio

#endif /* MIDI_OUT_H */
