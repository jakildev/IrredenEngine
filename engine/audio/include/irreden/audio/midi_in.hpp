/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\audio\midi_in.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef MIDI_IN_H
#define MIDI_IN_H

#include <RtMidi.h>
#include <irreden/ir_profiling.hpp>
#include <irreden/audio/midi_messages.hpp>
#include <set>
#include <queue>
#include <unordered_map>

#include <irreden/audio/components/component_midi_message.hpp>

namespace IRAudio {

    struct MidiMessageQueues {
        std::queue<MidiMessage<kMidiStatus_NOTE_OFF>>
            m_messageQueueNoteOff;
        std::queue<MidiMessage<kMidiStatus_NOTE_ON>>
            m_messageQueueNoteOn;
        std::queue<MidiMessage<kMidiStatus_POLYPHONIC_KEY_PRESSURE>>
            m_messagePolyKeyPressure;
        std::queue<MidiMessage<kMidiStatus_CONTROL_CHANGE>>
            m_messageQueueControlChange;
        std::queue<MidiMessage<kMidiStatus_PROGRAM_CHANGE>>
            m_messageQueueProgramChange;
        std::queue<MidiMessage<kMidiStatus_CHANNEL_PRESSURE>>
            m_messageQueueChannelPressure;
        std::queue<MidiMessage<kMidiStatus_PITCH_BEND>>
            m_messageQueuePitchBend;
    };



    class IRMidiIn {
        public:
            IRMidiIn();
            ~IRMidiIn();

            // void openPort(unsigned int portNumber);

            void openPort(MidiInInterface midiInInterface);

            void processMidiMessageQueue();

        private:
            RtMidiIn m_rtMidiIn;
            std::unordered_map<MidiInInterface, RtMidiIn> m_rtMidiInMap;
            unsigned int m_numberPorts;
            std::vector<std::string> m_portNames;
            std::vector<unsigned int> m_openPorts;
            std::string m_portName;
            MidiMessageQueues m_messageQueues;
            std::queue<IRComponents::C_MidiMessage> m_messageQueue;

            void setCallback(
                RtMidiIn& rtMidiIn,
                void(*midiInputCallback)(
                    double timeStamp,
                    std::vector<unsigned char> *message,
                    void *userData
                )
            );
    };

    void readMessageTestCallbackNew(
        double deltaTime,
        std::vector<unsigned char> *message,
        void* userdata
    );


} // namespace IRAudio

#endif /* MIDI_IN_H */
