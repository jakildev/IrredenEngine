#ifndef MIDI_IN_H
#define MIDI_IN_H

#include <irreden/ir_profile.hpp>

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/midi_messages.hpp>
#include <irreden/audio/components/component_midi_message.hpp>

#include <RtMidi.h>

#include <set>
#include <queue>
#include <unordered_map>

using IRComponents::C_MidiMessage;

namespace IRAudio {

struct MidiMessageQueues {
    std::queue<MidiMessage<kMidiStatus_NOTE_OFF>> m_messageQueueNoteOff;
    std::queue<MidiMessage<kMidiStatus_NOTE_ON>> m_messageQueueNoteOn;
    std::queue<MidiMessage<kMidiStatus_POLYPHONIC_KEY_PRESSURE>> m_messagePolyKeyPressure;
    std::queue<MidiMessage<kMidiStatus_CONTROL_CHANGE>> m_messageQueueControlChange;
    std::queue<MidiMessage<kMidiStatus_PROGRAM_CHANGE>> m_messageQueueProgramChange;
    std::queue<MidiMessage<kMidiStatus_CHANNEL_PRESSURE>> m_messageQueueChannelPressure;
    std::queue<MidiMessage<kMidiStatus_PITCH_BEND>> m_messageQueuePitchBend;
};

class MidiIn {
  public:
    MidiIn();
    ~MidiIn();

    void tick();

    // void openPort(unsigned int portNumber);

    int openPort(MidiInInterfaces midiInInterface);
    int openPort(const std::string &deviceName);
    void processMidiMessageQueue();
    CCData checkCCMessageThisFrame(MidiChannel channel, CCMessage ccNumber) const;
    const std::vector<C_MidiMessage> &getMidiNotesOnThisFrame(MidiChannel channel) const;
    const std::vector<C_MidiMessage> &getMidiNotesOffThisFrame(MidiChannel channel) const;

    void insertNoteOffMessage(MidiChannel channel, const C_MidiMessage &midiMessage);
    void insertNoteOnMessage(MidiChannel channel, const C_MidiMessage &midiMessage);
    void insertCCMessage(MidiChannel channel, const C_MidiMessage &midiMessage);

  private:
    RtMidiIn m_rtMidiIn;
    std::unordered_map<MidiInInterfaces, RtMidiIn> m_rtMidiInMap;
    unsigned int m_numberPorts;
    std::vector<std::string> m_portNames;
    std::vector<unsigned int> m_openPorts;
    std::string m_portName;
    MidiMessageQueues m_messageQueues;
    std::queue<IRComponents::C_MidiMessage> m_messageQueue;

    std::unordered_map<MidiChannel, std::unordered_map<CCMessage, CCData>> m_ccMessagesThisFrame;
    std::unordered_map<MidiChannel, std::vector<C_MidiMessage>> m_midiNoteOnMessagesThisFrame;
    std::unordered_map<MidiChannel, std::vector<C_MidiMessage>> m_midiNoteOffMessagesThisFrame;

    void clearPreviousMessages();

    void setCallback(RtMidiIn &rtMidiIn,
                     void (*midiInputCallback)(double timeStamp,
                                               std::vector<unsigned char> *message,
                                               void *userData));
};

void readMessageTestCallbackNew(double deltaTime, std::vector<unsigned char> *message,
                                void *userdata);

} // namespace IRAudio

#endif /* MIDI_IN_H */
