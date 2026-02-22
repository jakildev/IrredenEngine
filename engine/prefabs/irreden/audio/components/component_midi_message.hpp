#ifndef COMPONENT_MIDI_MESSAGE_H
#define COMPONENT_MIDI_MESSAGE_H

#include <irreden/audio/ir_audio_types.hpp>

#include <vector>

using namespace IRAudio;

namespace IRComponents {

struct C_MidiMessageData {
    unsigned char data1_;
    unsigned char data2_;

    C_MidiMessageData(unsigned char data1, unsigned char data2) : data1_(data1), data2_(data2) {}

    C_MidiMessageData() : data1_(0), data2_(0) {}

    const unsigned char getMidiNoteNumber() const {
        return data1_;
    }

    const unsigned char getMidiNoteVelocity() const {
        return data2_;
    }

    const unsigned char getCCNumber() const {
        return data1_;
    }

    const unsigned char getCCValue() const {
        return data2_;
    }
};

struct C_MidiMessageStatus {
    unsigned char status_;

    C_MidiMessageStatus(unsigned char status, unsigned char channel)
        : status_(buildMidiStatus(status, channel)) {}

    C_MidiMessageStatus() : status_(0) {}

    const unsigned char getChannelBits() const {
        return normalizeMidiChannel(status_);
    }

    const unsigned char getStatusBits() const {
        return normalizeMidiStatus(status_);
    }

    void setChannel(unsigned char channel) {
        status_ = buildMidiStatus(getStatusBits(), channel);
    }

    void setStatus(unsigned char status) {
        status_ = buildMidiStatus(status, getChannelBits());
    }
};

// Catch all message, but going to use individual components future
struct C_MidiMessage {
    // Potentially add delta time here...
    // float deltaTime_;
    unsigned char status_;
    unsigned char data1_;
    unsigned char data2_;

    C_MidiMessage(unsigned char status, unsigned char data1, unsigned char data2)
        : status_(status), data1_(data1), data2_(data2) {}

    C_MidiMessage() : status_(0), data1_(0), data2_(0) {}

    const unsigned char getChannelBits() const {
        return normalizeMidiChannel(status_);
    }

    const unsigned char getStatusBits() const {
        return normalizeMidiStatus(status_);
    }

    void setChannel(unsigned char channel) {
        status_ = buildMidiStatus(getStatusBits(), channel);
    }

    void setStatus(unsigned char status) {
        status_ = buildMidiStatus(status, getChannelBits());
    }

    const unsigned char getMidiNoteNumber() const {
        return data1_;
    }

    const unsigned char getMidiNoteVelocity() const {
        return data2_;
    }

    const unsigned char getCCNumber() const {
        return data1_;
    }

    const unsigned char getCCValue() const {
        return data2_;
    }

    std::vector<unsigned char> toRtMidiMessage() const {
        if (getStatusBits() == kMidiStatus_PROGRAM_CHANGE ||
            getStatusBits() == kMidiStatus_CHANNEL_PRESSURE) {
            return std::vector<unsigned char>{status_, data1_};
        }
        return std::vector<unsigned char>{status_, data1_, data2_};
    }
};

} // namespace IRComponents

#endif /* COMPONENT_MIDI_MESSAGE_H */
