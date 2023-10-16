/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_midi_message.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MIDI_MESSAGE_H
#define COMPONENT_MIDI_MESSAGE_H

#include "../audio/ir_audio.hpp"

using namespace IRAudio;

namespace IRComponents {

    struct C_MidiMessageData {
        unsigned char data1_;
        unsigned char data2_;

        C_MidiMessageData(
            unsigned char data1,
            unsigned char data2
        )
        :   data1_(data1),
            data2_(data2)
        {

        }

        C_MidiMessageData()
        :   data1_(0),
            data2_(0)
        {

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
    };

    struct C_MidiMessageStatus {
        unsigned char status_;

        C_MidiMessageStatus(
            unsigned char status,
            unsigned char channel
        )
        :   status_(
                (status & kMidiMessageBits_STATUS) |
                (channel & kMidiMessageBits_CHANNEL)
            )
        {

        }

        C_MidiMessageStatus()
        :   status_(0)
        {

        }

        const unsigned char getChannelBits() const {
            return status_ & kMidiMessageBits_CHANNEL;
        }

        const unsigned char getStatusBits() const {
            return status_ & kMidiMessageBits_STATUS;
        }

        void setChannel(unsigned char channel) {
            status_ = getStatusBits() | (channel & kMidiMessageBits_CHANNEL);
        }

        void setStatus(unsigned char status) {
            status_ = getChannelBits() | (status & kMidiMessageBits_STATUS);
        }
    };

    // Catch all message, but going to use individual components future
    struct C_MidiMessage {
        // Potentially add delta time here...
        // float deltaTime_;
        unsigned char status_;
        unsigned char data1_;
        unsigned char data2_;

        C_MidiMessage(
            unsigned char status,
            unsigned char data1,
            unsigned char data2
        )
        :   status_(status),
            data1_(data1),
            data2_(data2)
        {

        }

        C_MidiMessage()
        :   status_(0),
            data1_(0),
            data2_(0)
        {

        }

        const unsigned char getChannelBits() const {
            return status_ & kMidiMessageBits_CHANNEL;
        }

        const unsigned char getStatusBits() const {
            return status_ & kMidiMessageBits_STATUS;
        }

        void setChannel(unsigned char channel) {
            status_ = getStatusBits() | (channel & kMidiMessageBits_CHANNEL);
        }

        void setStatus(unsigned char status) {
            status_ = getChannelBits() | (status & kMidiMessageBits_STATUS);
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
            if(getStatusBits() == kMidiStatus_PROGRAM_CHANGE ||
                getStatusBits() == kMidiStatus_CHANNEL_PRESSURE) {
                return std::vector<unsigned char>{status_, data1_};
            }
            return std::vector<unsigned char>{status_, data1_, data2_};
        }

    };


} // namespace IRComponents

#endif /* COMPONENT_MIDI_MESSAGE_H */
