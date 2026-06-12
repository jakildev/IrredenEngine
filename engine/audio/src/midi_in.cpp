#include <irreden/ir_audio.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/audio/midi_in.hpp>
#include <irreden/update/components/component_lifetime.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_source_port.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
// using namespace IREntity;

namespace IRAudio {

MidiIn::MidiIn()
    : m_rtMidiIn{}
    , m_numberPorts(m_rtMidiIn.getPortCount())
    , m_portNames{}
    , m_ports{}
    , m_frameBuffer{} {
    IRE_LOG_INFO("Descovered {} MIDI input sources", m_numberPorts);
    for (int i = 0; i < m_numberPorts; i++) {
        m_portNames.push_back(m_rtMidiIn.getPortName(i));
        IRE_LOG_INFO("MIDI input source {}: {}", i, m_portNames[i].c_str());
    }
    IRE_LOG_INFO("Created MidiIn");
}

MidiIn::~MidiIn() {}

void MidiIn::tick() {
    clearPreviousMessages();
    processMidiMessageQueue();
}

void MidiIn::clearPreviousMessages() {
    m_frameBuffer.clear();
}

CCData MidiIn::checkCCMessageThisFrame(MidiChannel channel, CCMessage ccNumber) const {
    return m_frameBuffer.checkCC(channel, ccNumber);
}
CCData
MidiIn::checkCCMessageThisFrame(int portIndex, MidiChannel channel, CCMessage ccNumber) const {
    return m_frameBuffer.checkCC(portIndex, channel, ccNumber);
}

const std::vector<C_MidiMessage> &MidiIn::getMidiNotesOnThisFrame(MidiChannel channel) const {
    return m_frameBuffer.notesOn(channel);
}
const std::vector<C_MidiMessage> &MidiIn::getMidiNotesOffThisFrame(MidiChannel channel) const {
    return m_frameBuffer.notesOff(channel);
}
const std::vector<C_MidiMessage> &
MidiIn::getMidiNotesOnThisFrame(int portIndex, MidiChannel channel) const {
    return m_frameBuffer.notesOn(portIndex, channel);
}
const std::vector<C_MidiMessage> &
MidiIn::getMidiNotesOffThisFrame(int portIndex, MidiChannel channel) const {
    return m_frameBuffer.notesOff(portIndex, channel);
}

int MidiIn::openPort(const std::string &portNameSubstring) {
    for (int i = 0; i < m_numberPorts; i++) {
        const std::string_view portName{m_portNames[i]};
        if (portName.find(portNameSubstring) == portName.npos) {
            continue;
        }
        for (const auto &port : m_ports) {
            if (port->portIndex_ == i) {
                IRE_LOG_INFO("MIDI In port {} ({}) already open", i, portName);
                return i;
            }
        }
        auto port = std::make_unique<MidiInPort>();
        port->portIndex_ = i;
        port->name_ = m_portNames[i];
        port->rtMidiIn_ = std::make_unique<RtMidiIn>();
        port->rtMidiIn_->openPort(i);
        port->rtMidiIn_->setCallback(onRtMidiMessage, &port->queue_);
        m_ports.push_back(std::move(port));
        IRE_LOG_INFO("Opened MIDI In port {}: {}", i, portName);
        return i;
    }
    IRE_LOG_WARN(
        "No MIDI input port matching '{}' — {} port(s) available",
        portNameSubstring,
        m_numberPorts
    );
    return -1;
}

int MidiIn::openPort(MidiInInterfaces interface) {
    return openPort(kMidiInInterfaceNames[midiInInterfaceIndex(interface)]);
}

const std::vector<std::string> &MidiIn::getPortNames() const {
    return m_portNames;
}

std::vector<int> MidiIn::getOpenPortIndices() const {
    std::vector<int> indices;
    indices.reserve(m_ports.size());
    for (const auto &port : m_ports) {
        indices.push_back(port->portIndex_);
    }
    return indices;
}

void MidiIn::processMidiMessageQueue() {
    for (auto &port : m_ports) {
        while (!port->queue_.empty()) {
            const C_MidiMessage &message = port->queue_.front();
            IREntity::createEntity(
                C_MidiMessage{message},
                C_MidiIn{},
                C_MidiSourcePort{port->portIndex_},
                C_Lifetime{1}
            );
            port->queue_.pop();
        }
    }
}

void MidiIn::insertNoteOffMessage(MidiChannel channel, const C_MidiMessage &midiMessage) {
    m_frameBuffer.insertNoteOff(channel, midiMessage);
}
void MidiIn::insertNoteOnMessage(MidiChannel channel, const C_MidiMessage &midiMessage) {
    m_frameBuffer.insertNoteOn(channel, midiMessage);
}
void MidiIn::insertCCMessage(MidiChannel channel, const C_MidiMessage &midiMessage) {
    m_frameBuffer.insertCC(channel, midiMessage);
}

void MidiIn::insertNoteOffMessage(
    int portIndex, MidiChannel channel, const C_MidiMessage &midiMessage
) {
    m_frameBuffer.insertNoteOff(portIndex, channel, midiMessage);
}
void MidiIn::insertNoteOnMessage(
    int portIndex, MidiChannel channel, const C_MidiMessage &midiMessage
) {
    m_frameBuffer.insertNoteOn(portIndex, channel, midiMessage);
}
void MidiIn::insertCCMessage(int portIndex, MidiChannel channel, const C_MidiMessage &midiMessage) {
    m_frameBuffer.insertCC(portIndex, channel, midiMessage);
}

//-----------Callback---------//

void onRtMidiMessage(
    double deltaTime, std::vector<unsigned char> *message, void *userdata
) {
    // Audio messages will be processed async
    // Game input messages will be added to synchronous queue

    unsigned int messageSize = message->size();
    IR_ASSERT(messageSize > 0, "Received size 0 midi message");

    for (int i = 0; i < messageSize; i++) {
        IRE_LOG_DEBUG("Message byte {}: {}", i, message->at(i));
    }

    auto messageQueue = static_cast<std::queue<IRComponents::C_MidiMessage> *>(userdata);
    C_MidiMessage newMessage{
        message->at(0),
        messageSize > 1 ? message->at(1) : (unsigned char)0,
        messageSize > 2 ? message->at(2) : (unsigned char)0
    };
    messageQueue->push(newMessage);
}

} // namespace IRAudio
