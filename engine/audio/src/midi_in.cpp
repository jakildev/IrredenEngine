/*
 * Project: Irreden Engine
 * File: midi_in.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_audio.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/audio/midi_in.hpp>
#include <irreden/update/components/component_lifetime.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
using namespace IRECS;

namespace IRAudio {

    MidiIn::MidiIn()
    :   m_rtMidiIn{},
        m_numberPorts(m_rtMidiIn.getPortCount()),
        m_portNames{},
        m_openPorts{}
    {
        IRProfile::engLogInfo("Descovered {} MIDI input sources", m_numberPorts);
        for(int i = 0; i < m_numberPorts; i++) {
            m_portNames.push_back(m_rtMidiIn.getPortName(i));
            IRProfile::engLogInfo("MIDI input source {}: {}", i, m_portNames[i].c_str());
        }
        IRProfile::engLogInfo("Created MidiIn");
    }

    MidiIn::~MidiIn() {

    }

    // void MidiIn::openPort(unsigned int portNumber) {
    //     IRProfile::engLogInfo("Opening MIDI In port {}", portNumber);
    //     m_rtMidiIn.openPort(portNumber);
    //     m_openPorts.push_back(portNumber);
    // }

    // // Opens the first device that matches substring name
    // void MidiIn::openPort(std::string portNameSubstring) {
    //     for(int i = 0; i < m_numberPorts; i++) {
    //         const std::string_view portName{m_portNames[i]};
    //         if(portName.find(portNameSubstring) != portName.npos) {
    //             m_rtMidiIn.openPort(i);
    //             m_openPorts.push_back(i);
    //             IRProfile::engLogInfo("Opened MIDI In port {}: {}", i, portName);
    //             return;
    //         }
    //     }
    //     IR_ASSERT(false, "Attempted to open non-existant MIDI In port by name");
    // }

    void MidiIn::openPort(MidiInInterfaces interface) {
        for(int i = 0; i < m_numberPorts; i++) {
            const std::string_view portName{m_portNames[i]};
            if(portName.find(kMidiInInterfaceNames[interface]) != portName.npos) {
                m_rtMidiInMap.emplace(interface, RtMidiIn{});
                setCallback(m_rtMidiInMap[interface], IRAudio::readMessageTestCallbackNew);
                m_rtMidiInMap[interface].openPort(i);
                IRProfile::engLogInfo("Opened MIDI In port {}: {}", i, portName);
                return;
            }
        }
        IR_ASSERT(false, "Attempted to open non-existant MIDI In port by name");
    }

    void MidiIn::processMidiMessageQueue() {

        while(!m_messageQueue.empty()) {
            const C_MidiMessage& message = m_messageQueue.front();
            EntityHandle midiMessageIn{};
            midiMessageIn.set(message);
            midiMessageIn.set(C_MidiIn{});
            midiMessageIn.set(C_Lifetime{1});
            m_messageQueue.pop();
        }
    }

    void MidiIn::setCallback(
        RtMidiIn& rtMidiIn,
        void(*midiInputCallback)(
            double timeStamp,
            std::vector<unsigned char> *message,
            void *userData
        )
    ) {
        rtMidiIn.setCallback(midiInputCallback, &m_messageQueue);
    }

//-----------Callback---------//

    void readMessageTestCallbackNew(
        double deltaTime,
        std::vector<unsigned char> *message,
        void* userdata
    ) {
        // Audio messages will be processed async
        // Game input messages will be added to synchronous queue

        unsigned int messageSize = message->size();
        IR_ASSERT(messageSize > 0, "Received size 0 midi message");


        for(int i = 0; i < messageSize; i++) {
            IRProfile::engLogInfo("Message byte {}: {}", i, message->at(i));
        }

        auto messageQueue = static_cast<std::queue<IRComponents::C_MidiMessage>*>(
            userdata
        );
        C_MidiMessage newMessage{
            message->at(0),
            messageSize > 1 ? message->at(1) : (unsigned char)0,
            messageSize > 2 ? message->at(2) : (unsigned char)0
        };
        messageQueue->push(newMessage);
    }

} // namespace IRAudio