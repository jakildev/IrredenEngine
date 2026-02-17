#include <irreden/ir_audio.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/audio/midi_in.hpp>
#include <irreden/update/components/component_lifetime.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
// using namespace IREntity;

namespace IRAudio {

MidiIn::MidiIn()
    : m_rtMidiIn{}, m_numberPorts(m_rtMidiIn.getPortCount()), m_portNames{}, m_openPorts{},
      m_rtMidiInMap{}, m_ccMessagesThisFrame{}, m_midiNoteOffMessagesThisFrame{},
      m_midiNoteOnMessagesThisFrame{} {
    IRE_LOG_INFO("Descovered {} MIDI input sources", m_numberPorts);
    for (int i = 0; i < m_numberPorts; i++) {
        m_portNames.push_back(m_rtMidiIn.getPortName(i));
        IRE_LOG_INFO("MIDI input source {}: {}", i, m_portNames[i].c_str());
    }

    for (unsigned char i = 0; i < kNumMidiChannels; ++i) {
        m_ccMessagesThisFrame.insert({i, std::unordered_map<CCMessage, CCData>{}});
        m_midiNoteOffMessagesThisFrame.insert({i, std::vector<C_MidiMessage>{}});
        m_midiNoteOnMessagesThisFrame.insert({i, std::vector<C_MidiMessage>{}});
    }
    setCallback(m_rtMidiIn, readMessageTestCallbackNew);
    IRE_LOG_INFO("Created MidiIn");
}

MidiIn::~MidiIn() {}

void MidiIn::tick() {
    clearPreviousMessages();
    processMidiMessageQueue();
}

void MidiIn::clearPreviousMessages() {
    for (auto &channelMap : m_ccMessagesThisFrame) {
        channelMap.second.clear();
    }
    for (auto &channelMap : m_midiNoteOnMessagesThisFrame) {
        channelMap.second.clear();
    }
    for (auto &channelMap : m_midiNoteOffMessagesThisFrame) {
        channelMap.second.clear();
    }
}

CCData MidiIn::checkCCMessageThisFrame(MidiChannel channel, CCMessage ccNumber) const {
    if (!m_ccMessagesThisFrame.at(channel).contains(ccNumber)) {
        return kCCFalse;
    }
    return m_ccMessagesThisFrame.at(channel).at(ccNumber);
}

const std::vector<C_MidiMessage> &MidiIn::getMidiNotesOnThisFrame(MidiChannel channel) const {
    return m_midiNoteOnMessagesThisFrame.at(channel);
}
const std::vector<C_MidiMessage> &MidiIn::getMidiNotesOffThisFrame(MidiChannel channel) const {
    return m_midiNoteOffMessagesThisFrame.at(channel);
}

// void MidiIn::openPort(unsigned int portNumber) {
//     IRE_LOG_INFO("Opening MIDI In port {}", portNumber);
//     m_rtMidiIn.openPort(portNumber);
//     m_openPorts.push_back(portNumber);
// }

int MidiIn::openPort(const std::string &portNameSubstring) {
    for (int i = 0; i < m_numberPorts; i++) {
        const std::string_view portName{m_portNames[i]};
        if (portName.find(portNameSubstring) != portName.npos) {
            m_rtMidiIn.openPort(i);
            m_openPorts.push_back(i);
            IRE_LOG_INFO("Opened MIDI In port {}: {}", i, portName);
            return i;
        }
    }
    IR_ASSERT(false, "Attempted to open non-existant MIDI In port by name");
    return -1;
}

int MidiIn::openPort(MidiInInterfaces interface) {
    return openPort(kMidiInInterfaceNames[interface]);
}

void MidiIn::processMidiMessageQueue() {

    while (!m_messageQueue.empty()) {
        const C_MidiMessage &message = m_messageQueue.front();
        IREntity::createEntity(C_MidiMessage{message}, C_MidiIn{}, C_Lifetime{1});
        m_messageQueue.pop();
    }
}

void MidiIn::insertNoteOffMessage(MidiChannel channel, const C_MidiMessage &midiMessage) {
    m_midiNoteOffMessagesThisFrame[channel].push_back(midiMessage);
}

void MidiIn::insertNoteOnMessage(MidiChannel channel, const C_MidiMessage &midiMessage) {
    m_midiNoteOnMessagesThisFrame[channel].push_back(midiMessage);
}

void MidiIn::insertCCMessage(MidiChannel channel, const C_MidiMessage &midiMessage) {
    m_ccMessagesThisFrame[channel][midiMessage.getCCNumber()] = midiMessage.getCCValue();
}

void MidiIn::setCallback(RtMidiIn &rtMidiIn,
                         void (*midiInputCallback)(double timeStamp,
                                                   std::vector<unsigned char> *message,
                                                   void *userData)) {
    rtMidiIn.setCallback(midiInputCallback, &m_messageQueue);
}

//-----------Callback---------//

void readMessageTestCallbackNew(double deltaTime, std::vector<unsigned char> *message,
                                void *userdata) {
    // Audio messages will be processed async
    // Game input messages will be added to synchronous queue

    unsigned int messageSize = message->size();
    IR_ASSERT(messageSize > 0, "Received size 0 midi message");

    for (int i = 0; i < messageSize; i++) {
        IRE_LOG_INFO("Message byte {}: {}", i, message->at(i));
    }

    auto messageQueue = static_cast<std::queue<IRComponents::C_MidiMessage> *>(userdata);
    C_MidiMessage newMessage{message->at(0), messageSize > 1 ? message->at(1) : (unsigned char)0,
                             messageSize > 2 ? message->at(2) : (unsigned char)0};
    messageQueue->push(newMessage);
}

} // namespace IRAudio